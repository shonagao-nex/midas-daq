#!/usr/bin/env python3
"""Independent EASIROC deployment. --check has no mutation capability."""
import sys
sys.dont_write_bytecode = True
import argparse
import contextlib
import fcntl
import hashlib
import ipaddress
import json
import math
import os
from pathlib import Path
import pwd
import shutil
import socket
import stat
import subprocess
import time
import urllib.request

ROOT = Path('/home/daq/midas/midas/online')
RUNTIME = ROOT.parent
MIDAS = Path('/home/daq/midas/midas_src')
BASE = '/Equipment/EASIROC'
BINARY = 'easiroc_frontend/bin/feeasiroc'
FILES = (BINARY,) + tuple('daq_monitor/web/' + n for n in
    ('easiroc.html', 'easiroc.js', 'easiroc.css', 'easiroc-mock-data.js'))
CUSTOM = '/Custom/EASI Slow Control'
READ_METHODS = {'db_get_values', 'db_key', 'db_copy'}
TID_INT, TID_BOOL, TID_DOUBLE, TID_STRING, TID_KEY, TID_DWORD, TID_QWORD = 7, 8, 10, 12, 15, 6, 18


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False).encode()


def number(value):
    if isinstance(value, str):
        return int(value, 16 if value.startswith('0x') else 10)
    require(type(value) in (int, float) and math.isfinite(value), 'Invalid number')
    return value


def plain(value):
    """Discard db_get_values/db_copy metadata, retaining actual values."""
    if isinstance(value, dict):
        return {k.lower(): plain(v) for k, v in value.items() if '/' not in k}
    if isinstance(value, list):
        return [plain(v) for v in value]
    return value


class Rpc:
    def __init__(self, port, writable=False):
        self.url = f'http://127.0.0.1:{port}/?mjsonrpc'
        self.writable = writable

    def call(self, method, params):
        require(self.writable or method in READ_METHODS, 'Read-only RPC: ' + method)
        request = urllib.request.Request(self.url, encoded({
            'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params}),
            {'Content-Type': 'application/json', 'Cache-Control': 'no-cache'})
        with urllib.request.urlopen(request, timeout=8) as response:
            result = json.load(response)
        require('error' not in result and 'result' in result, f'{method}: {result}')
        return result['result']

    def read(self, path, method='db_get_values'):
        r = self.call(method, {'paths': [path]})
        require(r['status'] in ([1], [312]), f'{method} {path}: {r}')
        return None if r['status'] == [312] else r['keys' if method == 'db_key' else 'data'][0]

    def write(self, method, params):
        r = self.call(method, params)
        require(r.get('status') == [1], f'{method}: {r}')


def leaves(tree, base):
    """Flatten db_copy with type/length/access metadata for exact protection."""
    result = {}
    require(isinstance(tree, dict), 'Missing subtree: ' + base)
    for name, value in tree.items():
        if '/' in name:
            continue
        path = base + '/' + name
        meta = tree.get(name + '/key', {})
        require(meta.get('type') != 16, 'ODB links unsupported: ' + path)
        if isinstance(value, dict):
            result.update(leaves(value, path))
        else:
            require('type' in meta, 'No ODB metadata: ' + path)
            result[path] = {'value': value, 'type': meta['type'],
                'count': len(value) if isinstance(value, list) else 1,
                'item_size': meta.get('item_size'), 'access_mode': meta.get('access_mode')}
    return result


def regular(path):
    require(not path.is_symlink() and path.is_file(), 'Expected regular file: ' + str(path))
    return path.read_bytes()


def safe_path(root, relative):
    path = root / relative
    require(root.resolve() == root, 'Root must not be a symlink: ' + str(root))
    for item in [path, *path.parents]:
        if item == root:
            break
        require(not item.is_symlink(), 'Symlink refused: ' + str(item))
    require(path.resolve().is_relative_to(root), 'Path escapes root')
    return path


def client_map(rpc):
    tree = plain(rpc.read('/System/Clients'))
    require(isinstance(tree, dict), 'Cannot read MIDAS clients')
    result = {}
    for pid, client in tree.items():
        name = client.get('name')
        require(name and name not in result, 'Missing/duplicate MIDAS client name')
        result[name] = pid
    return result


def required_schema():
    specs = {'Settings/Enabled': (8, 1), 'Settings/Network/IPAddress': (12, 1),
        'Settings/ASICSlowControl/ApplyAtBOR': (8, 1)}
    for key in ('ADCEnabled', 'TDCEnabled', 'ScalerEnabled'):
        specs['Settings/Acquisition/' + key] = (8, 1)
    applied = 'Variables/ASICSlowControl/LastApplied/'
    specs.update({applied + 'Valid': (8, 1), applied + 'RequestId': (6, 1),
                  applied + 'ApplyUnixTime': (18, 1)})
    for prefix in ('Settings/', applied):
        for chip in ('ASIC1/', 'ASIC2/'):
            for field in ('DiscriminatorDACCode', 'DiscriminatorDACSlope',
                          'HGFeedbackCapacitance', 'LGFeedbackCapacitance',
                          'HGShapingTime', 'LGShapingTime'):
                specs[prefix + chip + field] = (7, 1)
            specs[prefix + chip + 'InputDAC'] = (7, 32)
            specs[prefix + chip + 'ChannelEnabled'] = (8, 32)
    return {BASE + '/' + k: v for k, v in specs.items()}


def extras_schema(prefix):
    # easiroc_run_settings.h:155-164 (visitSlowControlSettings).
    specs = {}
    for chip in ('ASIC1/', 'ASIC2/'):
        specs[prefix + 'Probe/' + chip + 'Enable'] = (8, 1)
        for name in ('Channel', 'Signal'):
            specs[prefix + 'Probe/' + chip + name] = (7, 1)
    for name in ('Pattern', 'OneChannel', 'HitNumThreshold'):
        specs[prefix + 'SelectableLogic/' + name] = (7, 1)
    specs[prefix + 'SelectableLogic/AndChannels'] = (8, 64)
    specs[prefix + 'TriggerWidth'] = (7, 1)
    return specs


def additions():
    # Only missing safe defaults; never infer ASIC/LastApplied or enable HV.
    return {
        BASE + '/Settings/HV/CurrentLimit_uA': (10, 1, 0, 20.0),
        BASE + '/Settings/HV/HardwareAccessEnabled': (8, 1, 0, False),
        BASE + '/Commands/HV/Operation': (12, 1, 32, ''),
        BASE + '/Commands/HV/TargetVoltage_V': (10, 1, 0, 0.0),
        BASE + '/Commands/HV/Enable': (8, 1, 0, False),
        BASE + '/Commands/HV/RequestPayload': (12, 1, 256, ''),
        BASE + '/Commands/HV/RequestId': (6, 1, 0, 0),
        BASE + '/Variables/HV/LastHandledRequestId': (6, 1, 0, 0),
        CUSTOM: (12, 1, 256, 'easiroc.html'),
    }


class Deployment:
    def __init__(self, live, dev, root=ROOT, runtime=RUNTIME, midas=MIDAS, now=time.time):
        self.live, self.dev = live, dev
        self.root, self.runtime, self.midas, self.now = root, runtime, midas, now
        self.backups = runtime / 'backups'

    def stopped(self):
        for path, expected in (('/Experiment/Name', 'daq'), ('/Runinfo/State', 1),
                ('/Runinfo/Transition in progress', 0), ('/Runinfo/Requested transition', 0)):
            require(self.live.read(path) == expected, 'Unsafe live state: ' + path)

    def guard(self, safety):
        self.stopped()
        require(self.dev.read('/Experiment/Name') == 'daq-dev', '8181 is not daq-dev')
        live_clients, dev_clients = client_map(self.live), client_map(self.dev)
        # An enabled flag alone cannot prove absence of diagnostic/HV traffic.
        require('feeasiroc' not in dev_clients, 'Development feeasiroc must be stopped externally')
        if self.root == ROOT:
            host_processes(live_clients)
        settings = plain(self.live.read(BASE + '/Settings'))
        require(isinstance(settings, dict), 'Missing EASIROC Settings')
        ip = settings.get('network', {}).get('ipaddress')
        ipaddress.IPv4Address(ip)
        dev_ip = self.dev.read(BASE + '/Settings/Network/IPAddress')
        require(isinstance(dev_ip, str), 'Cannot identify development IP')
        require(not plain(self.live.read(BASE + '/Devices')) and
                not plain(self.dev.read(BASE + '/Devices')), 'Additional Devices require separate deployment review')
        require(settings.get('hv', {}).get('hardwareaccessenabled', False) is False,
                'Existing HardwareAccessEnabled must already be false (no automatic change)')
        require(isinstance(safety, dict), 'Fresh external HV safety record required')
        require(safety.get('experiment') == 'daq' and safety.get('ip') == ip,
                'Safety record experiment/IP mismatch')
        age = self.now() - number(safety.get('verified_at'))
        require(0 <= age <= 300 and safety.get('hv_off') is True and
                safety.get('starts_inhibited') is True and safety.get('development_restart_inhibited') is True,
                'Safety record expired or missing operational exclusion')
        voltage, current = number(safety.get('voltage_v')), number(safety.get('current_ua'))
        limit = number(settings.get('hv', {}).get('currentlimit_ua', 20))
        # Rollback must remain possible after db_create succeeds but initializing this
        # inhibited new limit fails. Full apply schema validation rejects invalid limits.
        safety_limit = min(20, limit) if 0 < limit < 0.0364 * 65535 else 20
        require(0 <= voltage <= 1 and 0 <= current < safety_limit and
                bool(safety.get('operator')) and bool(safety.get('method')),
                'HV off/low voltage/current confirmation required')
        hv = plain(self.live.read(BASE + '/Variables/HV'))
        if hv and 'feeasiroc' in live_clients:
            require(hv.get('enabledstate') in ('OFF', 'Unknown') and hv.get('inprogress') is False and
                    number(hv.get('activerequestid')) == 0, 'Live HV not idle/OFF')
            # External evidence is still required; stale ODB is never proof of HV safety.
        for command, status, busy in (
                ('ASICSlowControl/ApplyRequestId', 'ASICSlowControl', 'ApplyInProgress'),
                ('BufferClearRequestId', 'BufferClear', 'InProgress'),
                ('HV/RequestId', 'HV', 'InProgress')):
            request = self.live.read(BASE + '/Commands/' + command)
            handled = self.live.read(BASE + '/Variables/' + status + '/LastHandledRequestId')
            active = self.live.read(BASE + '/Variables/' + status + '/ActiveRequestId')
            require(self.live.read(BASE + '/Variables/' + status + '/' + busy) in (None, False)
                    and active in (None, 0, '0x00000000'), 'Operation busy: ' + status)
            require(number(request if request is not None else 0) == number(handled if handled is not None else 0),
                    'Unacknowledged mailbox: ' + status)
        wrapper = self.root / 'scripts/start_feeasiroc.sh'
        require(self.live.read('/Programs/feeasiroc/Start command') == str(wrapper),
                'Unexpected production frontend start command')
        require(os.access(wrapper, os.X_OK), 'Frontend wrapper not executable')
        # Exactly known wrapper behavior; do not execute arbitrary ODB shell text.
        canonical = Path(__file__).resolve().parents[1] / 'start_feeasiroc.sh'
        expected = regular(canonical)
        require(regular(wrapper) in (expected, expected.replace(b' "$@"', b'')), 'Unreviewed frontend wrapper')
        require(self.live.read('/Custom/Path') == str(self.root / 'daq_monitor/web'), 'Unexpected Custom/Path')
        for name in ('Auto start', 'Auto stop', 'Auto restart'):
            require(self.live.read('/Programs/feeasiroc/' + name) is False, 'Automatic frontend lifecycle enabled')
        return {'live_clients': live_clients, 'dev_clients': dev_clients, 'ip': ip, 'development_ip': dev_ip}

    def schema(self):
        tree = self.live.read(BASE, 'db_copy')
        entries = leaves(tree, BASE)
        for path, (tid, count) in required_schema().items():
            item = entries.get(path)
            require(item and (item['type'], item['count']) == (tid, count), 'Incompatible/missing protected schema: ' + path)
        require(entries[BASE + '/Settings/ASICSlowControl/ApplyAtBOR']['value'] is False,
                'ApplyAtBOR is deprecated; requires separate explicit configuration decision')
        # Validate any existing optional extensions using the frontend visitor field schema.
        for prefix in (BASE + '/Settings/', BASE + '/Variables/ASICSlowControl/LastApplied/'):
            extended = entries.get(prefix + 'Extended', {}).get('value')
            extras = [p for p in entries if p.startswith(prefix) and p[len(prefix):].split('/')[0]
                      in ('Probe', 'SelectableLogic', 'TriggerWidth')]
            if prefix + 'Extended' in entries:
                item = entries[prefix + 'Extended']
                require((item['type'], item['count']) == (8, 1), 'Invalid Extended flag')
            if (prefix.endswith('/Settings/') and extras) or extended is True:
                for path, spec in extras_schema(prefix).items():
                    item = entries.get(path)
                    require(item and (item['type'], item['count']) == spec,
                            'Incomplete/invalid extension schema: ' + path)
        pending = {}
        for path, (tid, count, size, value) in additions().items():
            key = self.live.read(path, 'db_key')
            if key is None:
                pending[path] = (tid, count, size, value)
            else:
                require(key['type'] == tid and key['num_values'] == count and
                        (not size or key['item_size'] >= size), 'Incompatible existing key: ' + path)
                if path.endswith('/CurrentLimit_uA'):
                    require(0 < number(self.live.read(path)) < 0.0364 * 65535, 'Invalid existing CurrentLimit')
                if path == CUSTOM:
                    require(self.live.read(path) == value, 'Existing Custom registration differs')
        return tree, entries, pending

    def release(self, directory):
        directory = directory.absolute()
        manifest = json.loads(regular(safe_path(directory, 'manifest.json')))
        require(manifest.get('format') == 1 and set(manifest.get('files', {})) == set(FILES), 'Unexpected release files')
        for relative, sha in manifest['files'].items():
            require(digest(regular(safe_path(directory, relative))) == sha, 'Release hash mismatch: ' + relative)
        for name in ('libmidas.a', 'mfe.o'):
            require(manifest.get('midas', {}).get(name) == digest(regular(self.midas / 'lib' / name)),
                    'Production MIDAS build dependency mismatch: ' + name)
        require(regular(directory / BINARY).startswith(b'\x7fELF'), 'Frontend is not ELF')
        return manifest

    def plan(self, release, safety):
        manifest = self.release(release)
        legacy = self.runtime / 'deploy-state.json'
        if legacy.exists():
            require(json.loads(regular(legacy)).get('phase') != 'in_progress', 'Legacy deployment incomplete')
        for journal in self.backups.glob('easiroc-*/journal.json'):
            require(json.loads(regular(journal)).get('phase') in ('complete', 'rolled_back'),
                    'Incomplete independent deployment; recover first: ' + str(journal.parent))
        state = self.guard(safety)
        tree, entries, pending = self.schema()
        # Query-only permission/capacity checks. No mkdir, lock, temp file or subprocess build.
        safe_path(self.runtime, 'backups')
        require(os.access(self.runtime, os.W_OK | os.X_OK), 'Runtime directory not writable for shared lock')
        require(self.backups.is_dir() and os.access(self.backups, os.W_OK | os.X_OK), 'Writable backups directory required')
        total = 0
        old = {}
        for relative in FILES:
            target = safe_path(self.root, relative)
            require(target.parent.is_dir() and os.access(target.parent, os.W_OK | os.X_OK), 'Unwritable destination: ' + str(target))
            data = regular(target) if target.exists() else None
            if relative == BINARY:
                require(data is not None and os.access(target, os.X_OK), 'Existing frontend binary missing')
            old[relative] = None if data is None else {'sha256': digest(data), 'mode': stat.S_IMODE(target.stat().st_mode)}
            total += len(data or b'') + (release / relative).stat().st_size
        require(shutil.disk_usage(self.backups).free >= max(16 * 1024 * 1024, total * 2), 'Insufficient backup capacity')
        require(shutil.disk_usage(self.root).free >= max(16 * 1024 * 1024, total * 2), 'Insufficient installation capacity')
        for path in pending:
            parent = path.rsplit('/', 1)[0]
            while self.live.read(parent, 'db_key') is None:
                parent = parent.rsplit('/', 1)[0]
            key = self.live.read(parent, 'db_key')
            require(key['type'] == TID_KEY and key['access_mode'] & 2, 'ODB parent not writable: ' + parent)
        state.update(manifest=manifest, old_files=old, odb=tree, protected=entries, pending=pending,
                     startup_sha256=digest(regular(self.root / 'scripts/start_feeasiroc.sh')))
        return state

    def unchanged(self, before):
        self.stopped()
        require(digest(regular(self.root / 'scripts/start_feeasiroc.sh')) == before['startup_sha256'],
                'Frontend wrapper changed')
        current = leaves(self.live.read(BASE, 'db_copy'), BASE)
        for path, item in before['protected'].items():
            # Frontend-owned status may legitimately change, but desired settings and transmission
            # record may not. Compare metadata as well as values for these protected trees.
            if path.startswith(BASE + '/Settings/') or path.startswith(BASE + '/Variables/ASICSlowControl/LastApplied/'):
                require(current.get(path) == item, 'Protected ODB changed: ' + path)
        clients = client_map(self.live)
        require({k: v for k, v in clients.items() if k != 'feeasiroc'} ==
                {k: v for k, v in before['live_clients'].items() if k != 'feeasiroc'}, 'Other MIDAS clients changed')

    def wait(self, running, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if ('feeasiroc' in client_map(self.live)) == running:
                return
            time.sleep(0.2)
        raise RuntimeError('feeasiroc did not ' + ('start' if running else 'stop'))

    def stop(self):
        self.stopped()
        if 'feeasiroc' in client_map(self.live):
            result = self.live.call('cm_shutdown', {'name': 'feeasiroc'})
            require(result.get('status') == 1, 'feeasiroc stop failed; do not replace files')
            self.wait(False)

    def start(self, verify_hv=False):
        self.stopped()
        require('feeasiroc' not in client_map(self.live), 'feeasiroc already running')
        previous = self.live.read(BASE + '/Variables/HV/HeartbeatUnixTime')
        # Existing Programs/start command is validated above. Only this one program is started.
        result = self.live.call('start_program', {'name': 'feeasiroc'})
        require(result.get('status') == 1, 'feeasiroc start failed')
        self.wait(True)
        if verify_hv:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                hv = plain(self.live.read(BASE + '/Variables/HV')) or {}
                heartbeat = hv.get('heartbeatunixtime')
                if (heartbeat is not None and number(heartbeat) > number(previous or 0) and
                        0 <= self.now() - number(heartbeat) <= 5 and
                        hv.get('backendmode') == 'Hardware (inhibited)' and
                        hv.get('operationsavailable') is False):
                    self.wait(True)
                    return
                require('feeasiroc' in client_map(self.live), 'Frontend exited during initialization')
                time.sleep(0.2)
            raise RuntimeError('Frontend registered but inhibited HV heartbeat did not advance')

    def save(self, backup, journal):
        atomic(backup / 'journal.json', encoded(journal), 0o600)

    def backup(self, plan, safety):
        stamp = time.strftime('%Y%m%d-%H%M%S', time.gmtime(self.now()))
        backup = self.backups / f'easiroc-{stamp}-{os.getpid()}'
        backup.mkdir(mode=0o700)
        for relative, item in plan['old_files'].items():
            if item is not None:
                target = backup / 'files' / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                data = regular(safe_path(self.root, relative))
                require(digest(data) == item['sha256'], 'Destination changed before backup')
                atomic(target, data, item['mode'])
        wrapper = self.root / 'scripts/start_feeasiroc.sh'
        wrapper_data = regular(wrapper)
        require(digest(wrapper_data) == plan['startup_sha256'], 'Frontend wrapper changed before backup')
        (backup / 'files/scripts').mkdir(parents=True, exist_ok=True)
        atomic(backup / 'files/scripts/start_feeasiroc.sh', wrapper_data, stat.S_IMODE(wrapper.stat().st_mode))
        odb = {path: self.live.read(path, 'db_copy') for path in
               (BASE, '/Programs/feeasiroc', '/Custom')}
        data = encoded(odb)
        atomic(backup / 'odb.json', data, 0o600)
        journal = {'format': 1, 'phase': 'backed_up', 'created_at': self.now(),
            'root': str(self.root), 'old_files': plan['old_files'], 'release': plan['manifest'],
            'original_running': 'feeasiroc' in plan['live_clients'],
            'protected': plan['protected'], 'live_clients': plan['live_clients'],
            'odb_sha256': digest(data), 'startup_sha256': plan['startup_sha256'],
            'safety': safety, 'actions': [], 'installed': []}
        self.save(backup, journal)
        return backup, journal

    def create_missing(self, path, spec, backup, journal):
        require(self.live.read(path, 'db_key') is None, 'ODB key appeared concurrently: ' + path)
        parent = path.rsplit('/', 1)[0]
        if self.live.read(parent, 'db_key') is None:
            self.create_missing(parent, (15, 1, 0, None), backup, journal)
        tid, count, size, value = spec
        action = {'path': path, 'spec': spec, 'phase': 'intent'}
        journal['actions'].append(action)
        self.save(backup, journal)  # Intent before RPC, including timeout/unknown outcomes.
        self.stopped()
        self.live.write('db_create', [{'path': path, 'type': tid, 'array_length': count,
                                     **({'string_length': size} if size else {})}])
        if tid != TID_KEY:
            self.stopped()
            self.live.write('db_paste', {'paths': [path], 'values': [value]})
        action['phase'] = 'created'
        self.save(backup, journal)

    def apply(self, release, safety):
        plan = self.plan(release, safety)
        backup, journal = self.backup(plan, safety)
        print('Recovery backup:', backup, flush=True)
        try:
            self.guard(safety)
            self.unchanged(plan)
            self.stop()
            self.unchanged(plan)
            journal['phase'] = 'installing'
            self.save(backup, journal)
            for relative in FILES:
                self.guard(safety)
                self.unchanged(plan)
                data = regular(safe_path(release, relative))
                require(digest(data) == plan['manifest']['files'][relative], 'Release changed')
                old = plan['old_files'][relative]
                target = safe_path(self.root, relative)
                require((digest(regular(target)) if target.exists() else None) ==
                        (old['sha256'] if old else None), 'Target changed: ' + relative)
                journal['installed'].append(relative)  # intent before replace
                self.save(backup, journal)
                atomic(target, data, 0o755 if relative == BINARY else 0o644)
                require(digest(regular(target)) == plan['manifest']['files'][relative], 'Installed hash mismatch')
            for path, spec in plan['pending'].items():
                self.guard(safety)
                self.unchanged(plan)
                self.create_missing(path, spec, backup, journal)
            self.unchanged(plan)
            if journal['original_running']:
                self.guard(safety)
                self.start(verify_hv=True)
            self.unchanged(plan)
            journal['phase'] = 'complete'
            self.save(backup, journal)
        except Exception as error:
            journal['phase'] = 'failed'
            journal['error'] = str(error)
            self.save(backup, journal)
            raise RuntimeError(f'{error}; no automatic restart/restore. Use --rollback --backup {backup}') from error
        return backup

    def rollback(self, backup, safety):
        backup = backup.absolute()
        require(backup.parent == self.backups and not backup.is_symlink(), 'Backup must be a direct child of backups')
        journal = json.loads(regular(backup / 'journal.json'))
        require(journal.get('format') == 1 and journal.get('root') == str(self.root), 'Wrong backup/root')
        require(journal.get('phase') != 'rolled_back', 'Backup already rolled back; no restart performed')
        require(set(journal['old_files']) == set(FILES) and set(journal['release']['files']) == set(FILES)
                and set(journal['installed']) <= set(FILES), 'Invalid recovery file list')
        require(digest(regular(backup / 'odb.json')) == journal['odb_sha256'], 'ODB backup checksum mismatch')
        require(digest(regular(backup / 'files/scripts/start_feeasiroc.sh')) == journal['startup_sha256'],
                'Corrupt startup wrapper backup')
        # Validate all recovery material before touching the frontend.
        for relative, item in journal['old_files'].items():
            if item:
                require(digest(regular(safe_path(backup / 'files', relative))) == item['sha256'], 'Corrupt backup: ' + relative)
        self.guard(safety)
        self.unchanged(journal)
        for relative in journal['installed']:
            target = safe_path(self.root, relative)
            sha = digest(regular(target)) if target.exists() else None
            old = journal['old_files'][relative]
            require(sha in (journal['release']['files'][relative], old['sha256'] if old else None),
                    'Modified installed file; refusing overwrite: ' + relative)
        self.stop()
        self.unchanged(journal)
        journal['phase'] = 'rolling_back'
        self.save(backup, journal)
        for relative in reversed(journal['installed']):
            self.guard(safety)
            self.unchanged(journal)
            target = safe_path(self.root, relative)
            old = journal['old_files'][relative]
            if old:
                atomic(target, regular(safe_path(backup / 'files', relative)), old['mode'])
            elif target.exists():
                target.unlink()
        # No subtree load and no rewind of ASIC/LastApplied/request IDs. Remove only unchanged
        # additions. Frontend-updated/status additions are retained and explicitly reported.
        retained = []
        for action in reversed(journal['actions']):
            path, spec = action['path'], action['spec']
            allowed = path in additions() or any(p.startswith(path + '/') for p in additions())
            require(allowed, 'Invalid recovery ODB path')
            key = self.live.read(path, 'db_key')
            if key is None:
                continue
            tid, count, size, value = spec
            actual = plain(self.live.read(path))
            if action['phase'] == 'created' and key['type'] == tid and key['num_values'] == count and (
                    (tid == TID_KEY and actual == {}) or (tid != TID_KEY and actual == value)):
                self.guard(safety)
                self.live.write('db_delete', {'paths': [path]})
            else:
                retained.append(path)
        self.unchanged(journal)
        if journal['original_running']:
            self.guard(safety)
            self.start()
        self.unchanged(journal)
        journal['phase'] = 'rolled_back'
        journal['retained_odb'] = retained
        self.save(backup, journal)
        return retained


def atomic(path, data, mode):
    temporary = path.with_name(path.name + f'.easiroc-{os.getpid()}.tmp')
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), mode)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if temporary.exists():
            temporary.unlink()


def host_processes(clients):
    pid = subprocess.check_output(['systemctl', 'show', 'midas-mhttpd.service',
        '--property=MainPID', '--value'], text=True).strip()
    require(pid.isdigit() and int(pid) > 1, 'Cannot identify live mhttpd PID')
    for ns in ('pid', 'mnt'):
        try:
            same = os.readlink('/proc/self/ns/' + ns) == os.readlink('/proc/' + pid + '/ns/' + ns)
        except OSError as error:
            raise RuntimeError('Cannot inspect host PID/mount namespace; run from actual host shell') from error
        require(same, 'Cannot verify host processes from this PID/mount namespace')
    found = []
    for comm in Path('/proc').glob('[0-9]*/comm'):
        try:
            if comm.read_text().strip() == 'feeasiroc':
                found.append(comm.parent.name)
        except FileNotFoundError:
            pass  # unrelated process exited during the scan
    expected = [clients['feeasiroc']] if 'feeasiroc' in clients else []
    require(sorted(found) == expected, 'Unregistered/duplicate/development feeasiroc process exists')
    if expected:
        executable = Path('/proc/' + expected[0] + '/exe')
        require(executable.resolve() == ROOT / BINARY and
                digest(executable.read_bytes()) == digest(regular(ROOT / BINARY)),
                'Live frontend executable differs from the deployed binary')


def host_only():
    require(os.geteuid() == pwd.getpwnam('daq').pw_uid and os.environ.get('HOME') == '/home/daq'
            and socket.gethostname().split('.')[0] == 'nexdaq1', 'Mutation requires daq@nexdaq1 HOME=/home/daq')
    pid = subprocess.check_output(['systemctl', 'show', 'midas-mhttpd.service',
        '--property=MainPID', '--value'], text=True).strip()
    require(pid.isdigit() and int(pid) > 1, 'Cannot identify live mhttpd PID')
    for ns in ('pid', 'mnt'):
        require(os.readlink('/proc/self/ns/' + ns) == os.readlink('/proc/' + pid + '/ns/' + ns),
                'Must share host mhttpd PID/mount namespace')
    require(Path('/proc/' + pid + '/exe').resolve() == MIDAS / 'bin/mhttpd', 'Wrong live mhttpd executable')


@contextlib.contextmanager
def lock():
    # Share the original deploy1 lock, with no creation/open on --check.
    fd = os.open(RUNTIME / 'deploy.lock', os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        legacy = RUNTIME / 'deploy-state.json'
        if legacy.exists():
            require(json.loads(regular(legacy)).get('phase') != 'in_progress', 'Legacy deployment incomplete')
        yield
    finally:
        os.close(fd)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--apply', action='store_true')
    mode.add_argument('--rollback', action='store_true')
    parser.add_argument('--release', type=Path)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--safety', type=Path, help='Fresh external HV/off + maintenance exclusion evidence JSON')
    args = parser.parse_args(argv)
    require((args.rollback and args.backup is not None and args.release is None) or
            (not args.rollback and args.release is not None and args.backup is None),
            'Use --release for check/apply, --backup for rollback')
    safety = json.loads(regular(args.safety)) if args.safety else None
    deploy = Deployment(Rpc(8081, writable=not args.check), Rpc(8181))
    if args.check:
        # Manifest/hashes are printed even when operational prerequisites block deployment.
        print(json.dumps({'release': deploy.release(args.release)}, indent=2))
        plan = deploy.plan(args.release, safety)
        print(json.dumps({k: v for k, v in plan.items() if k not in ('odb', 'protected')}, indent=2))
        print('CHECK PASSED (read-only; revalidated by --apply)')
    else:
        host_only()
        with lock():
            if args.apply:
                print('APPLIED:', deploy.apply(args.release, safety))
            else:
                print('ROLLED BACK; retained ODB:', deploy.rollback(args.backup, safety))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print('EASIROC deployment refused/failed:', error, file=sys.stderr)
        sys.exit(1)
