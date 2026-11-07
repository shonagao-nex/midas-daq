"""All effects use a memory ODB, recorded services and temporary development files."""
import ast
import copy
import io
import json
import os
from pathlib import Path
import pwd
import re
import shlex
import subprocess
import sys
import tempfile
import time
import types
import unittest
from contextlib import redirect_stdout, ExitStack
from unittest.mock import patch

from scripts.deploy import deploy3_configure_live_odb as schema
from scripts.deploy import deploy4_check_live as smoke
from scripts.deploy import frontend_odb as f

REPO = Path(__file__).resolve().parents[2]


class MemoryRpc:
    def __init__(self, legacy=True):
        self.keys, self.values, self.calls = {}, {}, []
        self.clients = {'fevme': True, 'feeasiroc': False, 'fehul': False}
        self.fail_path = None
        self.fail_paste_path = None
        for path, tid, size, value in schema.specs():
            if '/Programs/fehul' not in path and path != '/Custom/EASI Slow Control':
                self.add(path, tid, value, size=size or 4)
        for path, tid, count, size in schema.daq_settings_specs():
            if legacy or not path.startswith(f.EASI):
                value = False if tid == 8 else '192.168.10.40' if tid == 12 else 600
                if count > 1:
                    value = [value] * count
                self.add(path, tid, value, count=count, size=size or 4)
        for path, value in schema.MANAGED_VALUES.items():
            self.values[path] = value
        for phase, items in (('BOR', schema.BOR), ('EOR', schema.EOR)):
            for name, target in items:
                self.add(f'{schema.JSON_ROOT}/Links {phase}/{name}', schema.LINK, target)
        for path, tid, value in (
                ('/Experiment/Name', 12, 'daq'), ('/Runinfo/State', 7, 1),
                ('/Runinfo/Transition in progress', 7, 0), ('/Runinfo/Run number', 7, 177),
                ('/Logger/Message dir', 12, str(schema.RUNTIME / 'log')),
                ('/Logger/Data dir', 12, str(schema.RUNTIME / 'data')),
                ('/Logger/Elog dir', 12, str(schema.RUNTIME / 'elog')),
                ('/Logger/Write data', 8, True), ('/Experiment/Prevent start on required progs', 8, False),
                ('/Elog/External Elog', 8, False)):
            self.add(path, tid, value, size=256 if tid == 12 else 4)

    def add(self, path, tid, value, count=1, size=4):
        parent = path.rsplit('/', 1)[0]
        if parent and parent not in self.keys:
            self.add(parent, 15, None)
        self.keys[path] = {'type': tid, 'num_values': count, 'item_size': size, 'access_mode': 7}
        self.values[path] = copy.deepcopy(value)

    def key(self, path):
        return copy.deepcopy(self.keys.get(path))

    def value(self, path, missing_ok=False):
        if path == '/System/Clients':
            names = ['mhttpd', 'Logger', 'daq_monitor', 'ana'] + [n for n, v in self.clients.items() if v]
            return {str(i + 10): {'name': n} for i, n in enumerate(names)}
        if path in self.keys and self.keys[path]['type'] == 15:
            return self.copy(path)
        if path not in self.values and not missing_ok:
            raise RuntimeError('Missing mock ODB: ' + path)
        return copy.deepcopy(self.values.get(path))

    def copy(self, path):
        result = {}
        for child in self.keys:
            if not child.startswith(path + '/'):
                continue
            suffix = child[len(path) + 1:]
            if '/' in suffix:
                continue
            value = self.copy(child) if self.keys[child]['type'] == 15 else copy.deepcopy(self.values[child])
            result[suffix] = value
            result[suffix + '/key'] = dict(self.keys[child])
            if self.keys[child]['type'] == schema.LINK:
                result[suffix + '/key']['link'] = value
        return result

    def call(self, method, params):
        if method == 'cm_exist':
            return {'status': 1 if self.clients.get(params['name'], params['name'] in ('daq_monitor', 'ana')) else 103}
        if method in ('cm_shutdown', 'start_program'):
            name = params['name']
            assert name in self.clients
            self.calls.append((method, name))
            self.clients[name] = method == 'start_program'
            return {'status': 1}
        raise AssertionError('Unexpected RPC ' + method)

    def checked(self, method, args):
        self.calls.append((method, copy.deepcopy(args)))
        if method == 'db_create':
            for item in args:
                if item['path'] == self.fail_path:
                    raise RuntimeError('injected ODB failure')
                count = item.get('array_length', 1)
                value = None if item['type'] == 15 else '' if item['type'] == 12 else 0
                self.add(item['path'], item['type'], [value] * count if count > 1 else value,
                         count, item.get('string_length', 4))
        elif method == 'db_paste':
            for path, value in zip(args['paths'], args['values']):
                if path == self.fail_paste_path:
                    raise RuntimeError('injected value write failure')
                self.values[path] = copy.deepcopy(value)
        elif method == 'db_link':
            self.add(args['new_links'][0], 16, args['target_paths'][0])
        else:
            raise AssertionError('Unexpected mutation ' + method)


class ProgramsSchemaTest(unittest.TestCase):
    names = ('fevme', 'feeasiroc', 'fehul')

    def configure(self, rpc):
        with patch.object(schema, 'require_live', lambda r: schema.require_stopped(r)):
            schema.configure(rpc)

    def test_empty_programs_create_complete_midas_records(self):
        rpc = MemoryRpc()
        rpc.keys.clear()
        rpc.values.clear()
        rpc.add('/Runinfo/State', schema.INT32, 1)
        rpc.add('/Runinfo/Transition in progress', schema.INT32, 0)
        # Empty mock Programs tree; these are all nine cm_set_client_info fields.
        fields = {'Required': (schema.BOOL, None),
                  'Watchdog timeout': (schema.INT32, 10000),
                  'Check interval': (schema.UINT32, 180000),
                  'Start command': (schema.STRING, None),
                  'Auto start': (schema.BOOL, False),
                  'Auto stop': (schema.BOOL, False),
                  'Auto restart': (schema.BOOL, False),
                  'Alarm class': (schema.STRING, ''),
                  'First failed': (schema.UINT32, 0)}
        for spec in schema.specs():
            if any(spec[0] == '/Programs/' + name or
                   spec[0].startswith('/Programs/' + name + '/') for name in self.names):
                schema.ensure_key(rpc, *spec)
        for name in self.names:
            prefix = '/Programs/' + name
            self.assertEqual(set(rpc.copy(prefix)) - {key for key in rpc.copy(prefix)
                                                     if key.endswith('/key')}, set(fields))
            for field, (tid, initial) in fields.items():
                path = prefix + '/' + field
                self.assertTrue(schema.inspect_key(rpc, path, tid, 0))
                if initial is not None:
                    self.assertEqual(rpc.value(path), initial, path)
            self.assertEqual(rpc.value(prefix + '/Required'), name != 'fehul')
            self.assertEqual(rpc.value(prefix + '/Start command'),
                             str(schema.ROOT / f'scripts/start_{name}.sh'))

    def test_existing_first_failed_and_other_programs_are_preserved(self):
        for value in (0, 1770000123, 0xffffffff):
            with self.subTest(value=value):
                rpc = MemoryRpc()
                for name in self.names:
                    rpc.add(f'/Programs/{name}/First failed', schema.UINT32, value)
                rpc.add('/Programs/site_only/First failed', schema.UINT32, 123)
                rpc.add('/Programs/site_only/Required', schema.BOOL, True)
                before = {path: (copy.deepcopy(key), rpc.value(path))
                          for path, key in rpc.keys.items()
                          if path.startswith('/Programs/') and key['type'] != schema.KEY}
                other_programs = {name: rpc.copy('/Programs/' + name)
                                  for name in ('fevme', 'feeasiroc', 'ana',
                                               'ana_hist_odb_init', 'site_only')}
                self.configure(rpc)
                for path, data in before.items():
                    self.assertEqual((rpc.key(path), rpc.value(path)), data, path)
                for name, data in other_programs.items():
                    self.assertEqual(rpc.copy('/Programs/' + name), data, name)
                calls = copy.deepcopy(rpc.calls)
                self.configure(rpc)
                self.assertEqual(rpc.calls, calls)
                for name in self.names:
                    path = f'/Programs/{name}/First failed'
                    self.assertNotIn(path, schema.MANAGED_VALUES)
                    self.assertEqual(rpc.value(path), value)

    def test_first_failed_type_or_count_mismatch_blocks_all_writes(self):
        for name in self.names:
            for tid, count in ((schema.INT32, 1), (schema.UINT64, 1),
                               (schema.STRING, 1), (schema.BOOL, 1),
                               (schema.UINT32, 0), (schema.UINT32, 2)):
                with self.subTest(name=name, tid=tid, count=count):
                    rpc = MemoryRpc()
                    path = f'/Programs/{name}/First failed'
                    rpc.add(path, tid, [123, 456] if count == 2 else 123, count)
                    before = copy.deepcopy((rpc.keys, rpc.values))
                    with self.assertRaisesRegex(RuntimeError, path):
                        self.configure(rpc)
                    self.assertEqual((rpc.keys, rpc.values), before)
                    self.assertEqual(rpc.calls, [])

    def test_legacy_programs_missing_first_failed_are_completed(self):
        rpc = MemoryRpc()
        for name in ('fevme', 'feeasiroc'):
            path = f'/Programs/{name}/First failed'
            del rpc.keys[path]
            del rpc.values[path]
        before = copy.deepcopy(rpc.values)
        self.configure(rpc)
        for name in self.names:
            path = f'/Programs/{name}/First failed'
            self.assertTrue(schema.inspect_key(rpc, path, schema.UINT32, 0))
            self.assertEqual(rpc.value(path), 0)
        for path, value in before.items():
            self.assertEqual(rpc.values[path], value, path)

    def test_preview_lists_missing_first_failed_without_writes(self):
        rpc = MemoryRpc()
        with patch.object(schema, 'require_live', lambda r: schema.require_stopped(r)):
            plan = schema.preflight(rpc)
        self.assertIn({'path': '/Programs/fehul/First failed', 'type': schema.UINT32,
                       'string_length': 0, 'value': 0}, plan['fixed_additions'])
        self.assertEqual(rpc.calls, [])

    def test_configure_and_smoke_require_first_failed(self):
        rpc = MemoryRpc()
        self.configure(rpc)
        path = '/Programs/fehul/First failed'
        self.assertEqual(rpc.value(path), 0)
        calls = copy.deepcopy(rpc.calls)
        for replacement in (None, {'type': schema.INT32, 'num_values': 1},
                            {'type': schema.UINT32, 'num_values': 2}):
            with self.subTest(key=replacement):
                if replacement is None:
                    del rpc.keys[path]
                else:
                    rpc.keys[path] = replacement
                with self.assertRaisesRegex(RuntimeError, path):
                    schema.inspect_schema(rpc)
                self.assertEqual(rpc.calls, calls)


class SchemaIntegrationTest(unittest.TestCase):
    def configure(self, rpc):
        with patch.object(schema, 'require_live', lambda r: schema.require_stopped(r)):
            schema.configure(rpc)

    def test_additive_defaults_match_authoritative_cpp_definitions(self):
        # Construct data structs only; no frontend/MIDAS/transport is linked or started.
        hul_source = (REPO / 'hul_frontend/fehul.cxx').read_text()
        hul_struct = re.search(r'struct Settings \{.*?\n\};', hul_source, re.S).group(0)
        code = r'''#include "easiroc_run_settings.h"
#include <iostream>
#include <type_traits>
using DWORD = unsigned;
''' + hul_struct + r'''
int main() {
  easiroc::AsicSlowControlSettings s; s.extras = easiroc::SlowControlExtras{};
  easiroc::visitSlowControlSettings(s, [](const std::string& p, const auto& value) {
    using T=std::decay_t<decltype(value)>;
    std::cout << p;
    if constexpr (std::is_same_v<T,bool> || std::is_same_v<T,int>) std::cout << " " << int(value);
    else for (auto x:value) std::cout << " " << int(x);
    std::cout << "\n"; return true;
  });
  Settings h;
  std::cout << "HUL " << h.ip << " " << h.rbcp_port << " " << h.tcp_port << " "
            << h.enable_block << " " << h.freshness_ms << " " << h.drain_ms << "\n";
}
'''
        with tempfile.TemporaryDirectory(prefix='source-defaults-') as tmp:
            source, binary = Path(tmp) / 'defaults.cxx', Path(tmp) / 'defaults'
            source.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Wpedantic',
                            '-I' + str(REPO / 'easiroc_frontend'), str(source), '-o', str(binary)], check=True)
            output = subprocess.check_output([str(binary)], text=True).splitlines()
        specs = f.easiroc_settings()
        for line in output[:-1]:
            name, *numbers = line.split()
            spec = specs[f.EASI + '/Settings/' + name]
            value = [bool(int(n)) if spec[0] == 8 else int(n) for n in numbers]
            self.assertEqual(spec[3], value if spec[1] > 1 else value[0], name)
        self.assertEqual(output[-1].split()[1:], ['192.168.10.60', '4660', '24', '3', '2000', '2000'])

    def test_empty_frontend_trees_create_only_approved_settings(self):
        rpc = MemoryRpc(legacy=False)
        before = copy.deepcopy(rpc.values)
        self.configure(rpc)
        self.assertEqual(rpc.value(f.EASI + '/Settings/ASIC1/InputDAC'), [480] * 32)
        self.assertEqual(rpc.value(f.EASI + '/Settings/Probe/ASIC2/Signal'), 4)
        self.assertIs(rpc.value(f.HUL + '/Common/Enabled'), False)
        for path in f.frontend_owned():
            self.assertIsNone(rpc.key(path))
        self.assertIsNone(rpc.key(f.EASI + '/Variables/ASICSlowControl/LastApplied'))
        self.assertEqual(rpc.value('/Custom/EASI Slow Control'), 'easiroc.html')
        self.assertEqual(rpc.value('/Programs/fehul/Start command'), str(schema.ROOT / 'scripts/start_fehul.sh'))
        for path in ('Auto start', 'Auto stop', 'Auto restart', 'Required'):
            self.assertIs(rpc.value('/Programs/fehul/' + path), False)
        for path, value in before.items():
            self.assertEqual(rpc.values[path], value, path)
        first_calls = len(rpc.calls)
        rpc.clients['feeasiroc'] = True  # unchanged restart-free recheck can run with an existing client
        self.configure(rpc)
        self.assertEqual(len(rpc.calls), first_calls)

    def test_partial_settings_and_existing_hardware_values_preserved(self):
        rpc = MemoryRpc()
        rpc.values[f.EASI + '/Settings/ASIC1/InputDAC'] = [350] * 32
        rpc.values[f.EASI + '/Settings/Network/IPAddress'] = '192.168.10.49'
        rpc.add(f.EASI + '/Settings/HV/CurrentLimit_uA', 10, 12.5)
        rpc.add(f.EASI + '/Settings/HV/HardwareAccessEnabled', 8, True)
        path = f.EASI + '/Settings/Probe/ASIC1/Channel'
        rpc.add(path, 7, 7)
        rpc.add(f.EASI + '/Settings/ProductionOnly', 7, 123)
        for key, spec in f.frontend_owned().items():
            if key.startswith(f.HUL + '/Settings/'):
                rpc.add(key, spec[0], '192.168.10.69' if key.endswith('IPAddress') else spec[3], size=spec[2] or 4)
        before = copy.deepcopy(rpc.values)
        self.configure(rpc)
        for key, value in before.items():
            self.assertEqual(rpc.values[key], value, key)

    def test_smoke_requires_deploy_owned_extras_but_defers_frontend_owned_keys(self):
        rpc = MemoryRpc(legacy=False)
        self.configure(rpc)
        schema.inspect_schema(rpc)  # HV/HUL Settings may await the first authorized startup.
        del rpc.keys[f.EASI + '/Settings/TriggerWidth']
        with self.assertRaisesRegex(RuntimeError, 'TriggerWidth'):
            schema.inspect_schema(rpc)

    def test_protected_history_never_created_or_rewound(self):
        rpc = MemoryRpc()
        for path, spec in f.protected_history().items():
            if path.endswith('/Extended') or path in f.extras(f.EASI + '/Variables/ASICSlowControl/LastApplied'):
                continue
            value = False if spec[0] == 8 else [350] * spec[1] if spec[1] > 1 else 9
            rpc.add(path, spec[0], value, spec[1])
        rpc.values[f.EASI + '/Variables/ASICSlowControl/LastApplied/Valid'] = True
        rpc.add(f.EASI + '/Commands/ASICSlowControl/ApplyRequestId', 6, 99)
        before = copy.deepcopy(rpc.values)
        self.configure(rpc)
        for path, value in before.items():
            self.assertEqual(rpc.values[path], value, path)

    def test_every_frontend_type_and_array_mismatch_rejected_before_mutation(self):
        for path, spec in {**f.easiroc_settings(), **f.frontend_owned(), **f.protected_history()}.items():
            for tid, count in ((12 if spec[0] != 12 else 7, spec[1]), (spec[0], spec[1] + 1)):
                with self.subTest(path=path, tid=tid, count=count):
                    rpc = MemoryRpc()
                    rpc.add(path, tid, 0, count, spec[2] or 64)
                    with self.assertRaises(RuntimeError):
                        self.configure(rpc)
                    self.assertFalse(rpc.calls)

    def test_parent_and_event_id_conflicts_are_rejected_without_changes(self):
        for path, tid, value in ((f.EASI + '/Settings/Probe', 12, 'wrong parent'),
                                 ('/Equipment/Other/Common/Event ID', 4, 10),
                                 (f.HUL + '/Common/Event ID', 4, 12),
                                 (f.HUL + '/Settings/IPAddress', 12, '192.168.10.40')):
            rpc = MemoryRpc()
            rpc.add(path, tid, value, size=64)
            with self.assertRaises(RuntimeError):
                self.configure(rpc)
            self.assertFalse(rpc.calls)

    def test_other_type_mismatch_is_preflight_not_partial_write(self):
        rpc = MemoryRpc()
        # A later fixed key mismatch must block even though EASIROC extras are missing.
        rpc.keys[schema.RUN_PARAMETERS + '/Comment']['type'] = 7
        with self.assertRaises(RuntimeError):
            self.configure(rpc)
        self.assertFalse(rpc.calls)
        self.assertIsNone(rpc.key(f.EASI + '/Settings/TriggerWidth'))

    def test_failure_then_retry_preserves_existing_values(self):
        rpc = MemoryRpc()
        before = copy.deepcopy(rpc.values)
        rpc.fail_path = f.EASI + '/Settings/TriggerWidth'
        with self.assertRaisesRegex(RuntimeError, 'injected'):
            self.configure(rpc)
        for path, value in before.items():
            self.assertEqual(rpc.values[path], value)
        rpc.fail_path = None
        self.configure(rpc)
        self.assertEqual(rpc.value(f.EASI + '/Settings/TriggerWidth'), 118)

    def test_bad_history_and_custom_start_commands_are_not_repaired(self):
        for path, tid, value in ((f.EASI + '/Variables/ASICSlowControl/LastApplied/Valid', 8, True),
                                 ('/Programs/fehul/Start command', 12, '/other/start'),
                                 ('/Custom/EASI Slow Control', 12, 'other.html')):
            rpc = MemoryRpc()
            rpc.add(path, tid, value, size=256)
            with self.assertRaises(RuntimeError):
                self.configure(rpc)
            self.assertFalse(rpc.calls)

    def test_preview_is_readonly_and_rpc_rejects_mutation(self):
        rpc = MemoryRpc(legacy=False)
        before = copy.deepcopy(rpc.values)
        with patch.object(schema, 'require_live', lambda r: schema.require_stopped(r)):
            plan = schema.preflight(rpc)
        self.assertTrue(plan['frontend']['add'])
        self.assertFalse(rpc.calls)
        self.assertEqual(rpc.values, before)
        with patch.object(schema.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            for method in ('db_create', 'db_paste', 'cm_shutdown', 'start_program'):
                with self.assertRaisesRegex(RuntimeError, 'Read-only'):
                    schema.Rpc(readonly=True).call(method, {})


def orchestrator_namespace():
    source = (REPO / 'scripts/deploy/deploy2_impl.sh').read_text().split("<<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
    tree = ast.parse(source)
    functions = [n for n in tree.body if isinstance(n, ast.FunctionDef)]
    ns = {'os': os, 'sys': sys, 'time': time, 'json': json, 'Path': Path,
          'hashlib': __import__('hashlib'), 'subprocess': __import__('subprocess'),
          'shutil': __import__('shutil'), 'shlex': shlex, 'schema': schema,
          'FRONTENDS': ('fevme', 'feeasiroc', 'fehul'),
          'SERVICES': ('midas-daq-monitor.service', 'midas-analyzer.service'),
          'BINARIES': ('vme_frontend/bin/fevme', 'easiroc_frontend/bin/feeasiroc', 'hul_frontend/bin/fehul',
                       'daq_monitor/bin/daq_monitor', 'analyzer/bin/midas_analyzer')}
    exec(compile(ast.fix_missing_locations(ast.Module(body=functions, type_ignores=[])), 'mock-orchestrator', 'exec'), ns)
    return ns


class WholeDeployTest(unittest.TestCase):
    def setUp(self):
        build = REPO / 'easiroc_frontend/build'
        build.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='whole-deploy-mock-', dir=build)
        self.addCleanup(self.temp.cleanup)
        self.runtime = Path(self.temp.name)
        self.root = self.runtime / 'online'
        self.root.mkdir()
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(schema, 'ROOT', self.root))
        self.stack.enter_context(patch.object(schema, 'RUNTIME', self.runtime))
        # specs computes wrapper paths at call time. Custom fixed owner values have no root path.
        self.rpc = MemoryRpc()
        self.ns = orchestrator_namespace()
        self.ns.update(ROOT=self.root, RUNTIME=self.runtime, DEPLOY=self.root / 'scripts/deploy',
                       JOURNAL=self.runtime / 'deploy-state.json', BACKUPS=self.runtime / 'backups', rpc=self.rpc)
        self.services = {name: True for name in self.ns['SERVICES']}
        self.commands, self.order = [], []
        self.fail_build = False
        for name in self.ns['BINARIES']:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'old binary')
            path.chmod(0o755)
        for name in ('easiroc.html', 'easiroc.js', 'easiroc.css', 'easiroc-mock-data.js', 'daq.html', 'daq.js'):
            target = self.root / 'daq_monitor/web' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((REPO / 'daq_monitor/web' / name).read_bytes())
        self.ns['run'] = self.run_command
        self.ns['active_service'] = lambda name: self.services[name]
        self.ns['verify_installed_units'] = lambda: None
        self.ns['backup_odb'] = lambda commit: self.order.append('backup')
        self.ns['ensure_selection'] = lambda: self.order.append('selection')
        self.ns['smoke'] = lambda *args: self.order.append('smoke')
        self.stack.enter_context(patch.object(schema, 'require_live', lambda r: schema.require_stopped(r)))
        self.stack.enter_context(patch('scripts.deploy.midas_build.link_libraries', return_value=['-lsqlite3']))
        self.stack.enter_context(patch.object(self.ns['shutil'], 'which', return_value='/mock/node'))
        self.stack.enter_context(patch.object(self.ns['subprocess'], 'check_output', return_value='mock-commit'))
        self.stack.enter_context(patch.object(pwd, 'getpwnam', return_value=types.SimpleNamespace(pw_uid=os.geteuid())))
        self.stack.enter_context(patch.object(Path, 'cwd', return_value=self.root))
        lock = (self.runtime / 'lock').open('w')
        self.addCleanup(lock.close)
        self.stack.enter_context(patch.dict(os.environ, {'DEPLOY_LOCK_FD': str(lock.fileno()),
            'DEPLOY_TARGET_SHA': 'mock-commit', 'DEPLOY_START_FEVME': '1', 'DEPLOY_START_FEEASIROC': '0',
            'DEPLOY_START_FEHUL': '0', 'DEPLOY_START_MONITOR': '1', 'DEPLOY_START_ANALYZER': '1'}))

    def run_command(self, *args, env=None):
        args = tuple(map(str, args))
        self.commands.append(args)
        if args[:3] == ('sudo', '-n', 'systemctl'):
            self.services[args[4]] = args[3] == 'start'
            self.order.append(args[3] + ':' + args[4])
        elif args[0] == 'make':
            if self.fail_build and args[2] == 'hul_frontend':
                raise RuntimeError('injected build failure')
            self.order.append('build:' + args[2])
        elif len(args) > 1 and args[1].endswith('deploy3_configure_live_odb.py'):
            schema.configure(self.rpc, self.ns["JOURNAL"])
            self.order.append('configure')

    def invoke(self):
        with redirect_stdout(io.StringIO()):
            self.ns['main']()

    def test_complete_pipeline_new_hul_stays_off_and_existing_services_restore(self):
        self.invoke()
        self.assertEqual(json.loads(self.ns['JOURNAL'].read_text())['phase'], 'complete')
        self.assertTrue(all(self.services.values()))
        self.assertEqual(self.rpc.clients, {'fevme': True, 'feeasiroc': False, 'fehul': False})
        self.assertIs(self.rpc.value(f.HUL + '/Common/Enabled'), False)
        self.assertFalse(any(c == ('start_program', 'fehul') for c in self.rpc.calls))
        self.assertIn(('make', '-C', 'vme_frontend', 'fevme'), self.commands)
        self.assertIn(('make', '-C', 'daq_monitor', 'daq_monitor'), self.commands)
        self.assertTrue(any(c[:3] == ('cmake', '-S', 'analyzer') for c in self.commands))
        self.assertTrue(any(c[:4] == ('make', '-C', 'hul_frontend', 'all') for c in self.commands))
        self.assertLess(self.order.index('backup'), self.order.index('stop:midas-analyzer.service'))
        self.assertLess(self.order.index('configure'), self.order.index('start:midas-analyzer.service'))
        controls = [c for c in self.rpc.calls if c[0] in ('cm_shutdown', 'start_program')]
        self.invoke()
        self.assertEqual(controls, [c for c in self.rpc.calls if c[0] in ('cm_shutdown', 'start_program')])

    def test_midway_failure_retains_original_journal_and_resume(self):
        self.fail_build = True
        with self.assertRaisesRegex(RuntimeError, 'build failure'):
            self.invoke()
        journal = json.loads(self.ns['JOURNAL'].read_text())
        self.assertEqual(journal['phase'], 'in_progress')
        self.assertTrue(journal['started']['frontends']['fevme'])
        self.assertFalse(self.rpc.clients['fevme'])
        self.fail_build = False
        self.invoke()
        self.assertTrue(self.rpc.clients['fevme'])
        self.assertTrue(all(self.services.values()))
        self.assertFalse(self.rpc.clients['fehul'])

    def test_value_write_failure_is_recorded_and_retry_refuses_guessing(self):
        self.rpc.fail_paste_path = f.EASI + '/Settings/TriggerWidth'
        with self.assertRaisesRegex(RuntimeError, 'value write failure'):
            self.invoke()
        journal = json.loads(self.ns['JOURNAL'].read_text())
        self.assertEqual(journal['phase'], 'in_progress')
        self.assertEqual(journal['frontend_initialization'][self.rpc.fail_paste_path]['phase'], 'intent')
        self.rpc.fail_paste_path = None
        before = copy.deepcopy(self.rpc.values)
        with self.assertRaisesRegex(RuntimeError, 'Unfinished frontend initialization'):
            self.invoke()
        self.assertEqual(self.rpc.values, before)
        self.assertFalse(self.rpc.clients['fevme'])  # original running state remains in recovery journal

    def test_already_running_hul_restored_without_setting_changes(self):
        self.rpc.clients['fehul'] = True
        self.stack.enter_context(patch.dict(os.environ, {'DEPLOY_START_FEHUL': '1'}))
        self.rpc.add(f.HUL + '/Common/Enabled', 8, True)
        for path, spec in f.frontend_owned().items():
            if path.startswith(f.HUL + '/Settings/'):
                self.rpc.add(path, spec[0], spec[3], size=spec[2] or 4)
        before = {p: copy.deepcopy(v) for p, v in self.rpc.values.items() if p.startswith(f.HUL + '/Settings/')}
        self.invoke()
        self.assertTrue(self.rpc.clients['fehul'])
        for path, value in before.items():
            self.assertEqual(self.rpc.values[path], value)

    def test_schema_error_happens_before_backup_stop_or_build(self):
        self.rpc.add(f.EASI + '/Settings/Probe/ASIC1/Channel', 12, 'bad', size=32)
        with self.assertRaises(RuntimeError):
            self.invoke()
        self.assertFalse(self.order)
        self.assertFalse(self.commands)
        self.assertFalse(self.rpc.calls)

    def test_readonly_smoke_checks_hul_binary_wrapper_and_actual_gui_bytes(self):
        self.invoke()
        for name in ('start_fevme.sh', 'start_feeasiroc.sh', 'start_fehul.sh'):
            target = self.root / 'scripts' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#!/bin/sh\n')
            target.chmod(0o755)
        runlogs = self.runtime / 'runlogs'
        runlogs.mkdir()
        for name in ('runlog_index.json', 'runlog_selection.json'):
            (runlogs / name).write_text('{"runs":[]}')
        (self.root / 'daq_monitor/web/runlogs').symlink_to(runlogs)
        def command(*args):
            if args[0] == 'git':
                return 'main' if '--abbrev-ref' in args else 'mock-commit'
            return '123'
        def page(url, marker=None):
            if 'page=EASI' in url:
                return b'easiroc.js'
            if 'page=DAQ' in url:
                return b'daq.js'
            if 'page=Run' in url:
                return b'run-summary.js'
            name = url.rsplit('/', 1)[-1]
            if name.endswith('.json'):
                return b'{"runs":[]}'
            target = self.root / 'daq_monitor/web' / name
            return target.read_bytes() if target.exists() else b'run-summary.js'
        with patch.object(smoke, 'ROOT', self.root), patch.object(smoke, 'RUNTIME', self.runtime), \
             patch.object(smoke, 'schema', schema), patch.object(schema, 'Rpc', return_value=self.rpc), \
             patch.object(smoke, 'command', side_effect=command), patch.object(smoke, 'page', side_effect=page), \
             patch.object(smoke, 'service_active', return_value=True), patch.object(smoke, 'executable_matches'), \
             patch.object(smoke.subprocess, 'run'), patch.object(Path, 'is_file', autospec=True, side_effect=lambda p: p.exists()), \
             patch.object(Path, 'read_bytes', autospec=True, wraps=Path.read_bytes) as read:
            # Unit equality needs the actual tracked file contents; mock only installed unit paths.
            original = Path.open
            def bytes_for(path):
                if str(path).startswith('/etc/systemd/system/'):
                    return b'unit'
                if path.parent == self.root / 'systemd':
                    return b'unit'
                with original(path, 'rb') as stream:
                    return stream.read()
            read.side_effect = bytes_for
            for name in smoke.SERVICES[2:]:
                target = self.root / 'systemd' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b'unit')
            with redirect_stdout(io.StringIO()):
                smoke.check('mock-commit', self.rpc.clients)
            good_page = page
            with patch.object(smoke, 'page', side_effect=lambda url, marker=None:
                              b'outdated asset' if url.endswith('/easiroc.js') else good_page(url, marker)):
                with self.assertRaisesRegex(RuntimeError, 'Served EASIROC asset differs'):
                    smoke.check()
            (self.root / 'hul_frontend/bin/fehul').unlink()
            with self.assertRaisesRegex(RuntimeError, 'Missing production binary'):
                smoke.check()


class BootstrapGuardTest(unittest.TestCase):
    def test_check_dispatch_exits_before_any_mutation_stage(self):
        source = (REPO / 'scripts/deploy/deploy1_init.sh').read_text()
        start = source.index('if [[ ${1:-} == --check ]]')
        block = source[start:source.index('\nfi', start) + 3]
        with tempfile.TemporaryDirectory(prefix='bootstrap-check-', dir=REPO / 'easiroc_frontend/build') as tmp:
            root = Path(tmp)
            tool = root / 'python3'
            tool.write_text('#!/bin/sh\necho "PREVIEW $*"\n')
            tool.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + ':/usr/bin:/bin')
            result = subprocess.run(['bash', '-c', 'ROOT=' + shlex.quote(str(root)) + '\n' + block +
                                     '\necho MUTATION_STAGE', 'bootstrap', '--check'], env=env,
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0)
            self.assertIn('deploy3_configure_live_odb.py --check', result.stdout)
            self.assertNotIn('MUTATION_STAGE', result.stdout)
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['python3'])

    def test_dirty_checkout_lists_file_and_refuses(self):
        source = (REPO / 'scripts/deploy/deploy1_init.sh').read_text()
        start = source.index('if ! git -C')
        block = source[start:source.index('\nfi', start) + 3]
        with tempfile.TemporaryDirectory(prefix='bootstrap-git-', dir=REPO / 'easiroc_frontend/build') as tmp:
            root = Path(tmp)
            git = root / 'git'
            git.write_text('#!/bin/sh\ncase "$*" in\n*"diff --quiet"*) test "$MOCK_DIRTY" != worktree ;;\n*"diff --cached --quiet"*) test "$MOCK_DIRTY" != index ;;\n*"status --short"*) echo " M daq_monitor/web/easiroc.js" ;;\nesac\n')
            git.chmod(0o755)
            for dirty in ('worktree', 'index', ''):
                env = dict(os.environ, PATH=str(root) + ':/usr/bin:/bin', MOCK_DIRTY=dirty)
                result = subprocess.run(['bash', '-c', 'ROOT=' + shlex.quote(str(root)) + '\n' + block + '\necho PROCEED'],
                                        env=env, text=True, capture_output=True)
                if dirty:
                    self.assertEqual(result.returncode, 1)
                    self.assertIn('easiroc.js', result.stderr)
                    self.assertIn('tracked local changes', result.stderr)
                    self.assertNotIn('PROCEED', result.stdout)
                else:
                    self.assertEqual(result.returncode, 0)
                    self.assertIn('PROCEED', result.stdout)


if __name__ == '__main__':
    unittest.main()
