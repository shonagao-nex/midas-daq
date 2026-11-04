#!/usr/bin/env python3
"""Bounded, passive EASIROC capture. Run on nexdaq1 in the host namespaces."""
import argparse
import datetime
import fcntl
import json
import os
from pathlib import Path
import pwd
import selectors
import shutil
import signal
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/network-diagnostics'
NIC = 'enp5s0f0'
IP = '192.168.10.40'
# ICMP errors may have a router as outer source/destination; match quoted IPv4
# endpoints too. Target ARP only, never unrelated broadcast ARP.
FILTER = ('(arp and (src host 192.168.10.40 or dst host 192.168.10.40)) '
          'or (host 192.168.10.40 and (tcp or udp port 4660 or icmp)) '
          'or (icmp and (icmp[0] = 3 or icmp[0] = 11 or icmp[0] = 12) '
          'and (icmp[20:4] = 0xc0a80a28 or icmp[24:4] = 0xc0a80a28))')
JST = datetime.timezone(datetime.timedelta(hours=9))

def stamp():
    return datetime.datetime.now(JST).isoformat()

class BoundedLog:
    """At most two 4 MB segments per stream; keep newest records."""
    def __init__(self, path):
        self.path = path
    def write(self, data):
        data = (stamp() + ' ' + data.decode(errors='replace').rstrip() + '\n').encode()
        data = data[:65536]
        if self.path.exists() and self.path.stat().st_size + len(data) > 4_000_000:
            self.path.replace(self.path.with_suffix(self.path.suffix + '.1'))
        with self.path.open('ab') as f:
            f.write(data)

def snapshot(directory, label):
    commands = [['ip', '-details', 'route', 'get', IP], ['ip', 'neigh', 'show'],
                ['ip', '-s', '-s', 'link', 'show', 'dev', NIC],
                ['ethtool', '-S', NIC], ['ethtool', NIC],
                ['ps', '-C', 'feeasiroc', '-o', 'pid,ppid,stat,etime,args']]
    entries = []
    for cmd in commands:
        try:
            r = subprocess.run(cmd, capture_output=True, timeout=8)
            entries.append({'command': cmd, 'status': r.returncode,
                            'output': (r.stdout + r.stderr).decode(errors='replace')[:65536]})
        except (OSError, subprocess.TimeoutExpired) as e:
            entries.append({'command': cmd, 'error': str(e)})
    (directory / (label + '.json')).write_text(json.dumps(
        {'jst': stamp(), 'observations': entries}, indent=2) + '\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=14400)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 14400:
        parser.error('--seconds must be 1..14400')
    if socket.gethostname().split('.')[0] != 'nexdaq1':
        parser.error('run on nexdaq1 host shell')
    for tool in ['tcpdump', 'ip', 'tail', 'ethtool', 'ps']:
        if not shutil.which(tool):
            parser.error('missing tool: ' + tool)
    BASE.mkdir(parents=True, exist_ok=True)
    lock = (BASE / 'monitor.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        parser.error('another instance holds monitor.lock')
    if shutil.disk_usage(BASE).free < 250_000_000:
        parser.error('less than 250 MB free')
    directory = BASE / datetime.datetime.now(JST).strftime('%Y%m%dT%H%M%S%z')
    directory.mkdir()
    owner = ROOT.stat().st_uid
    # tcpdump drops privilege after opening the interface. Allow its output dir.
    if os.geteuid() == 0:
        os.chown(directory, owner, ROOT.stat().st_gid)
    capture = ['tcpdump', '-p', '-i', NIC, '-nn', '-s', '0', '-U',
               '-C', '10', '-W', '10', '-w', str(directory / 'capture.pcap')]
    if os.geteuid() == 0:
        capture += ['-Z', pwd.getpwuid(owner).pw_name]
    capture += [FILTER]
    meta = {'start_jst': stamp(), 'pid': os.getpid(), 'seconds': args.seconds,
            'filter': FILTER, 'capture_command': capture, 'directory': str(directory),
            'pcap_limit': '10,000,000 bytes x 10; one-packet rotation overshoot possible'}
    (directory / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    children = []
    child_names = {}
    logs = {name: BoundedLog(directory / (name + '.log'))
            for name in ['capture', 'network', 'frontend']}
    stop = False
    def stopping(signum, frame):
        nonlocal stop
        stop = True
    for sig in [signal.SIGTERM, signal.SIGINT, signal.SIGHUP]:
        signal.signal(sig, stopping)
    sel = selectors.DefaultSelector()
    # Reserve shutdown time so capture and children stop within four hours.
    deadline = time.monotonic() + max(0, args.seconds - 10)
    frontend_buffer = b''
    reason = 'initialization failed'
    def start(cmd, name):
        child = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 start_new_session=True, env={**os.environ, 'TZ': 'Asia/Tokyo'})
        children.append(child)
        child_names[child.pid] = name
        os.set_blocking(child.stdout.fileno(), False)
        sel.register(child.stdout, selectors.EVENT_READ, name)
        return child
    print('Directory:', directory, flush=True)
    try:
        snapshot(directory, 'initial')
        tcp = start(capture, 'capture')
        time.sleep(0.5)
        if tcp.poll() is not None:
            reason = 'capture could not start; check capture.log (permissions)'
            raise RuntimeError(reason)
        start(['ip', '-ts', 'monitor', 'link', 'route', 'neigh'], 'network')
        # Follow existing frontend messages only. This does not trigger diagnosis.
        start(['tail', '-n', '0', '-F', '/home/nagao/midas/midas/log/midas.log'], 'frontend')
        meta['children'] = [p.pid for p in children]
        meta['capture_started_jst'] = stamp()
        (directory / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
        reason = 'maximum duration reached'
        print('Running:', json.dumps(meta), flush=True)
        while time.monotonic() < deadline and not stop:
            for key, _ in sel.select(timeout=min(1, max(0, deadline-time.monotonic()))):
                data = os.read(key.fileobj.fileno(), 65536)
                if not data:
                    sel.unregister(key.fileobj)
                elif key.data != 'frontend':
                    logs[key.data].write(data)
                else:
                    frontend_buffer += data
                    lines = frontend_buffer.split(b'\n')
                    frontend_buffer = lines.pop()[-65536:]
                    for line in lines:
                        if b'[feeasiroc,' in line:
                            logs['frontend'].write(line)
            if any(p.poll() is not None for p in children):
                reason = 'child exited unexpectedly'
                break
            if shutil.disk_usage(directory).free < 150_000_000:
                reason = 'free space below 150 MB'
                break
        if stop:
            reason = 'stop signal received'
    finally:
        # Stop only process groups created by this script; flush pcap via SIGINT.
        for p in children:
            if p.poll() is None:
                os.killpg(p.pid, signal.SIGINT)
        cleanup_deadline = time.monotonic() + 5
        for p in children:
            try:
                p.wait(timeout=max(0.05, cleanup_deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
            remaining = p.stdout.read()
            if remaining:
                name = child_names[p.pid]
                if name == 'frontend':
                    for line in (frontend_buffer + remaining).splitlines():
                        if b'[feeasiroc,' in line:
                            logs[name].write(line)
                else:
                    logs[name].write(remaining)
        sel.close()
        snapshot(directory, 'final')
        (directory / 'finished.json').write_text(json.dumps(
            {'end_jst': stamp(), 'reason': reason,
             'children': [{'pid': p.pid, 'status': p.returncode} for p in children]}, indent=2) + '\n')
        print('Finished:', reason, flush=True)
    return 0 if reason in ['maximum duration reached', 'stop signal received'] else 1

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(str(error), flush=True)
        raise SystemExit(1)
