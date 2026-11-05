import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts.deploy import deploy_easiroc as d


class MockRpc:
    def __init__(self, dev=False):
        self.dev = dev
        self.data = {}
        self.calls = []
        self.clients = {'mhttpd': '10', 'Logger': '20', 'fevme': '30', 'daq_monitor': '40', 'ana': '50'}
        if dev:
            self.clients = {'mhttpd': '60'}
        self.fail_method = None
        self.start_fails = False
        self.stop_fails = False
        self.on_start = None

    def add(self, path, value, tid, size=0, mode=7):
        self.data[path] = (copy.deepcopy(value), {'type': tid, 'num_values': len(value) if isinstance(value, list) else 1,
            'item_size': size or 4, 'access_mode': mode})

    def tree(self, path, metadata=False):
        if path in self.data and self.data[path][1]['type'] != 15:
            return copy.deepcopy(self.data[path][0])
        children = [p for p in self.data if p.startswith(path + '/')]
        if not children:
            return {} if path in self.data else None
        result = {}
        for name in sorted({p[len(path)+1:].split('/')[0] for p in children}):
            value = self.tree(path + '/' + name, metadata)
            result[name] = value
            if metadata:
                result[name + '/key'] = copy.deepcopy(self.data.get(path + '/' + name,
                    (None, {'type': 15, 'num_values': 1, 'access_mode': 7}))[1])
        return result

    def read(self, path, method='db_get_values'):
        self.calls.append((method, path))
        if path == '/System/Clients':
            return {pid: {'name': name} for name, pid in self.clients.items()}
        if method == 'db_key':
            if path in self.data:
                return copy.deepcopy(self.data[path][1])
            if self.tree(path) is not None:
                return {'type': 15, 'num_values': 1, 'access_mode': 7, 'item_size': 0}
            return None
        return self.tree(path, method == 'db_copy')

    def call(self, method, params):
        self.calls.append((method, copy.deepcopy(params)))
        if method == self.fail_method:
            self.fail_method = None
            raise RuntimeError('Injected ' + method + ' failure')
        if method == 'cm_shutdown':
            assert params == {'name': 'feeasiroc'}
            if self.stop_fails:
                return {'status': -1}
            self.clients.pop('feeasiroc', None)
            return {'status': 1}
        if method == 'start_program':
            assert params == {'name': 'feeasiroc'}
            if self.start_fails:
                return {'status': -1}
            self.clients['feeasiroc'] = '100'
            for name, value, tid in (('EnabledState', 'Unknown', 12), ('InProgress', False, 8), ('ActiveRequestId', 0, 6), ('HeartbeatUnixTime', 1000, 18),
                                     ('BackendMode', 'Hardware (inhibited)', 12), ('OperationsAvailable', False, 8)):
                self.add(d.BASE + '/Variables/HV/' + name, value, tid)
            if self.on_start:
                self.on_start(self)
            return {'status': 1}
        if method == 'db_create':
            for spec in params:
                self.add(spec['path'], {} if spec['type'] == 15 else '' if spec['type'] == 12 else 0,
                         spec['type'], spec.get('string_length', 0))
            return {'status': [1]}
        if method == 'db_paste':
            path = params['paths'][0]
            value, meta = self.data[path]
            self.data[path] = (copy.deepcopy(params['values'][0]), meta)
            return {'status': [1]}
        if method == 'db_delete':
            path = params['paths'][0]
            for p in list(self.data):
                if p == path or p.startswith(path + '/'):
                    del self.data[p]
            return {'status': [1]}
        raise AssertionError(method)

    def write(self, method, params):
        r = self.call(method, params)
        d.require(r.get('status') == [1], str(r))


class DeploymentTest(unittest.TestCase):
    def setUp(self):
        # All test filesystem changes stay in the development repository.
        build = Path(__file__).resolve().parents[2] / 'easiroc_frontend/build'
        build.mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix='deploy-mock-', dir=build)
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        self.root = self.work / 'online'
        self.root.mkdir()
        self.runtime = self.work
        (self.work / 'backups').mkdir()
        self.release = self.work / 'release'
        self.release.mkdir()
        self.midas = self.work / 'midas'
        (self.midas / 'lib').mkdir(parents=True)
        self.live, self.dev = MockRpc(), MockRpc(True)
        self.clock = 1000
        self.deploy = d.Deployment(self.live, self.dev, self.root, self.runtime, self.midas, lambda: self.clock)
        for relative in d.FILES:
            for root, content in ((self.root, b'old '), (self.release, b'new ')):
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((b'\x7fELF' if relative == d.BINARY else b'') + content + relative.encode())
                target.chmod(0o755 if relative == d.BINARY else 0o644)
        wrapper = self.root / 'scripts/start_feeasiroc.sh'
        wrapper.parent.mkdir()
        wrapper.write_bytes((Path(d.__file__).resolve().parents[1] / 'start_feeasiroc.sh').read_bytes())
        wrapper.chmod(0o755)
        for name in ('libmidas.a', 'mfe.o'):
            (self.midas / 'lib' / name).write_bytes(name.encode())
        self.manifest = {'format': 1, 'files': {f: d.digest((self.release / f).read_bytes()) for f in d.FILES},
            'midas': {n: d.digest((self.midas / 'lib' / n).read_bytes()) for n in ('libmidas.a', 'mfe.o')}}
        self.save_manifest()
        self.live.add('/Experiment/Name', 'daq', 12)
        self.dev.add('/Experiment/Name', 'daq-dev', 12)
        for path, value in (('/Runinfo/State', 1), ('/Runinfo/Transition in progress', 0), ('/Runinfo/Requested transition', 0)):
            self.live.add(path, value, 7)
        self.live.add('/Custom/Path', str(self.root / 'daq_monitor/web'), 12, 256)
        self.live.add('/Programs/feeasiroc/Start command', str(wrapper), 12, 256)
        for name in ('Auto start', 'Auto stop', 'Auto restart'):
            self.live.add('/Programs/feeasiroc/' + name, False, 8)
        for path, (tid, count) in d.required_schema().items():
            value = False if tid == 8 else '192.168.10.40' if tid == 12 else 350
            if count > 1:
                value = [value] * count
            self.live.add(path, value, tid, 64 if tid == 12 else 4)
        self.dev.add(d.BASE + '/Settings/Network/IPAddress', '192.168.10.40', 12)
        self.safety = {'experiment': 'daq', 'ip': '192.168.10.40', 'verified_at': 1000,
            'hv_off': True, 'voltage_v': 0.1, 'current_ua': 0.01, 'operator': 'test operator',
            'method': 'external meter + shutdown verified', 'starts_inhibited': True,
            'development_restart_inhibited': True}
        self.others = [self.root / name for name in ('vme_frontend/bin/fevme',
            'daq_monitor/bin/daq_monitor', 'analyzer/bin/midas_analyzer', 'daq_monitor/web/daq.js')]
        for target in self.others:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b'never changed')
        self.other = self.others[0]
        self.original = copy.deepcopy(self.live.data)

    def save_manifest(self):
        (self.release / 'manifest.json').write_text(json.dumps(self.manifest))

    def writes(self):
        return [c for c in self.live.calls if c[0] not in d.READ_METHODS]

    def files_snapshot(self):
        return {str(p.relative_to(self.work)): (p.read_bytes(), p.stat().st_mode)
                for p in self.work.rglob('*') if p.is_file()}

    def assert_protected(self):
        for path, item in self.original.items():
            self.assertEqual(self.live.data[path], item, path)
        for target in self.others:
            self.assertEqual(target.read_bytes(), b'never changed')

    def test_check_is_read_only_and_lists_hashes(self):
        before = self.files_snapshot()
        plan = self.deploy.plan(self.release, self.safety)
        self.assertEqual(plan['manifest'], self.manifest)
        self.assertEqual(self.files_snapshot(), before)
        self.assertFalse(self.writes())
        self.assertEqual(self.original, self.live.data)
        self.assert_protected()

    def test_rpc_read_only_denies_all_mutating_methods_before_http(self):
        rpc = d.Rpc(8081)
        with patch.object(d.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            for method in ('db_create', 'db_paste', 'db_delete', 'cm_shutdown', 'start_program'):
                with self.subTest(method=method), self.assertRaisesRegex(RuntimeError, 'Read-only RPC'):
                    rpc.call(method, {})

    def test_apply_stopped_frontend_does_not_start_it_and_rollback(self):
        old = {f: (self.root / f).read_bytes() for f in d.FILES}
        backup = self.deploy.apply(self.release, self.safety)
        self.assertNotIn('feeasiroc', self.live.clients)
        self.assert_protected()
        self.assertEqual(self.live.read(d.BASE + '/Settings/HV/HardwareAccessEnabled'), False)
        self.assertEqual(self.live.read(d.CUSTOM), 'easiroc.html')
        for f in d.FILES:
            self.assertEqual((self.root / f).read_bytes(), (self.release / f).read_bytes())
        self.deploy.rollback(backup, self.safety)
        for f in d.FILES:
            self.assertEqual((self.root / f).read_bytes(), old[f])
        self.assertIsNone(self.live.read(d.CUSTOM))
        self.assert_protected()

    def test_only_feeasiroc_restart_and_original_state_restored(self):
        self.live.clients['feeasiroc'] = '90'
        before = {k: v for k, v in self.live.clients.items() if k != 'feeasiroc'}
        backup = self.deploy.apply(self.release, self.safety)
        self.assertIn('feeasiroc', self.live.clients)
        self.deploy.rollback(backup, self.safety)
        self.assertEqual({k: v for k, v in self.live.clients.items() if k != 'feeasiroc'}, before)
        controls = [c for c in self.writes() if c[0] in ('cm_shutdown', 'start_program')]
        self.assertEqual(len(controls), 4)
        self.assertTrue(all(c[1] == {'name': 'feeasiroc'} for c in controls))
        self.assert_protected()

    def test_each_file_failure_is_recoverable(self):
        for failing in d.FILES:
            with self.subTest(file=failing):
                # A new fixture per failure point.
                case = DeploymentTest('test_check_is_read_only_and_lists_hashes')
                case.setUp()
                try:
                    atomic = d.atomic
                    def fail(path, data, mode):
                        if path == case.root / failing:
                            raise RuntimeError('injected replace failure')
                        return atomic(path, data, mode)
                    with patch.object(d, 'atomic', side_effect=fail), self.assertRaises(RuntimeError):
                        case.deploy.apply(case.release, case.safety)
                    backup = next((case.work / 'backups').glob('easiroc-*'))
                    case.deploy.rollback(backup, case.safety)
                    case.assert_protected()
                    self.assertTrue((case.root / d.BINARY).read_bytes().startswith(b'\x7fELFold'))
                finally:
                    case.doCleanups()

    def test_odb_create_failure_intent_is_not_blindly_deleted(self):
        self.live.fail_method = 'db_paste'
        with self.assertRaises(RuntimeError):
            self.deploy.apply(self.release, self.safety)
        backup = next((self.work / 'backups').glob('easiroc-*'))
        retained = self.deploy.rollback(backup, self.safety)
        self.assertTrue(retained)
        self.assert_protected()

    def test_stop_failure_never_replaces_files(self):
        self.live.clients['feeasiroc'] = '90'
        self.live.stop_fails = True
        before = {f: (self.root / f).read_bytes() for f in d.FILES}
        with self.assertRaises(RuntimeError):
            self.deploy.apply(self.release, self.safety)
        self.assertEqual(before, {f: (self.root / f).read_bytes() for f in d.FILES})
        self.assert_protected()

    def test_start_failure_can_rollback_without_other_client_changes(self):
        self.live.clients['feeasiroc'] = '90'
        self.live.start_fails = True
        with self.assertRaises(RuntimeError):
            self.deploy.apply(self.release, self.safety)
        backup = next((self.work / 'backups').glob('easiroc-*'))
        self.live.start_fails = False
        self.deploy.rollback(backup, self.safety)
        self.assertIn('feeasiroc', self.live.clients)
        self.assert_protected()

    def test_changed_added_odb_and_runtime_values_retained_on_rollback(self):
        backup = self.deploy.apply(self.release, self.safety)
        path = d.BASE + '/Settings/HV/CurrentLimit_uA'
        self.live.data[path] = (15.0, self.live.data[path][1])
        retained = self.deploy.rollback(backup, self.safety)
        self.assertIn(path, retained)
        self.assertEqual(self.live.read(path), 15)
        self.assert_protected()

    def test_protected_drift_refuses_rollback_without_changes(self):
        backup = self.deploy.apply(self.release, self.safety)
        path = d.BASE + '/Settings/ASIC1/InputDAC'
        self.live.data[path] = ([400] * 32, self.live.data[path][1])
        self.live.calls.clear()
        before = self.files_snapshot()
        with self.assertRaisesRegex(RuntimeError, 'Protected ODB changed'):
            self.deploy.rollback(backup, self.safety)
        self.assertFalse(self.writes())
        self.assertEqual(before, self.files_snapshot())

    def test_modified_files_or_corrupt_backup_refuse_rollback(self):
        backup = self.deploy.apply(self.release, self.safety)
        target = self.root / d.BINARY
        target.write_bytes(b'operator edit')
        self.live.calls.clear()
        with self.assertRaisesRegex(RuntimeError, 'Modified installed file'):
            self.deploy.rollback(backup, self.safety)
        self.assertFalse(self.writes())
        target.write_bytes((self.release / d.BINARY).read_bytes())
        (backup / 'files' / d.BINARY).write_bytes(b'corrupt')
        with self.assertRaisesRegex(RuntimeError, 'Corrupt backup'):
            self.deploy.rollback(backup, self.safety)
        self.assertFalse(self.writes())

    def test_preconditions_fail_without_writes(self):
        for change in ('running', 'transition', 'requested', 'dev_client', 'unknown_hv', 'stale',
                       'hv_permission', 'pending_apply', 'bad_type', 'bad_count', 'wrong_custom', 'bad_hash',
                       'midas_mismatch', 'extension', 'bad_ip', 'auto_restart'):
            with self.subTest(change=change):
                data, clients, safety = copy.deepcopy(self.live.data), dict(self.dev.clients), copy.deepcopy(self.safety)
                manifest = copy.deepcopy(self.manifest)
                try:
                    if change == 'running': self.live.add('/Runinfo/State', 3, 7)
                    elif change == 'transition': self.live.add('/Runinfo/Transition in progress', 1, 7)
                    elif change == 'requested': self.live.add('/Runinfo/Requested transition', 1, 7)
                    elif change == 'dev_client': self.dev.clients['feeasiroc'] = '91'
                    elif change == 'unknown_hv': safety['hv_off'] = False
                    elif change == 'stale': safety['verified_at'] = 0
                    elif change == 'hv_permission': self.live.add(d.BASE + '/Settings/HV/HardwareAccessEnabled', True, 8)
                    elif change == 'pending_apply': self.live.add(d.BASE + '/Commands/ASICSlowControl/ApplyRequestId', 3, 6)
                    elif change == 'bad_type': self.live.add(d.BASE + '/Settings/ASIC1/InputDAC', [350] * 32, 6)
                    elif change == 'bad_count': self.live.add(d.BASE + '/Settings/ASIC1/InputDAC', [350], 7)
                    elif change == 'wrong_custom': self.live.add('/Custom/Path', '/home/nagao/other', 12)
                    elif change == 'bad_hash': self.manifest['files'][d.BINARY] = 'bad'; self.save_manifest()
                    elif change == 'midas_mismatch': self.manifest['midas']['mfe.o'] = 'bad'; self.save_manifest()
                    elif change == 'extension': self.live.add(d.BASE + '/Settings/Probe/Bad', 1, 7)
                    elif change == 'bad_ip': safety['ip'] = '192.168.10.99'
                    elif change == 'auto_restart': self.live.add('/Programs/feeasiroc/Auto restart', True, 8)
                    self.live.calls.clear()
                    before = self.files_snapshot()
                    with self.assertRaises((RuntimeError, ValueError)):
                        self.deploy.plan(self.release, safety)
                    self.assertFalse(self.writes())
                    self.assertEqual(before, self.files_snapshot())
                finally:
                    self.live.data, self.dev.clients, self.manifest = data, clients, manifest
                    self.save_manifest()

    def test_insufficient_disk_space_and_symlink_refused(self):
        with patch.object(d.shutil, 'disk_usage', return_value=type('Usage', (), {'free': 0})()):
            with self.assertRaisesRegex(RuntimeError, 'capacity'):
                self.deploy.plan(self.release, self.safety)
        target = self.release / 'daq_monitor/web/easiroc.js'
        target.unlink()
        target.symlink_to(self.other)
        with self.assertRaisesRegex(RuntimeError, 'Symlink'):
            self.deploy.plan(self.release, self.safety)
        self.assertFalse(self.writes())

    def test_complete_extensions_preserved_and_incomplete_lastapplied_rejected(self):
        for prefix in (d.BASE + '/Settings/', d.BASE + '/Variables/ASICSlowControl/LastApplied/'):
            for path, (tid, count) in d.extras_schema(prefix).items():
                value = False if tid == 8 else 1
                self.live.add(path, [value] * count if count > 1 else value, tid)
        path = d.BASE + '/Variables/ASICSlowControl/LastApplied/Extended'
        self.live.add(path, True, 8)
        self.original = copy.deepcopy(self.live.data)
        backup = self.deploy.apply(self.release, self.safety)
        self.assert_protected()
        self.deploy.rollback(backup, self.safety)
        self.assert_protected()
        del self.live.data[d.BASE + '/Variables/ASICSlowControl/LastApplied/TriggerWidth']
        self.live.calls.clear()
        with self.assertRaisesRegex(RuntimeError, 'extension schema'):
            self.deploy.schema()
        self.assertFalse(self.writes())

    def test_failed_deployment_blocks_reapply_and_rollback_is_not_restarted_twice(self):
        self.live.fail_method = 'db_create'
        with self.assertRaises(RuntimeError):
            self.deploy.apply(self.release, self.safety)
        backup = next((self.work / 'backups').glob('easiroc-*'))
        self.live.calls.clear()
        with self.assertRaisesRegex(RuntimeError, 'Incomplete independent'):
            self.deploy.plan(self.release, self.safety)
        self.assertFalse(self.writes())
        self.deploy.rollback(backup, self.safety)
        self.live.calls.clear()
        with self.assertRaisesRegex(RuntimeError, 'already rolled back'):
            self.deploy.rollback(backup, self.safety)
        self.assertFalse(self.writes())

    def test_external_safety_record_is_required_even_with_odb_off(self):
        self.live.clients['feeasiroc'] = '90'
        self.live.add(d.BASE + '/Variables/HV/EnabledState', 'OFF', 12)
        self.live.add(d.BASE + '/Variables/HV/InProgress', False, 8)
        self.live.add(d.BASE + '/Variables/HV/ActiveRequestId', 0, 6)
        for safety in (None, dict(self.safety, verified_at=1001), dict(self.safety, voltage_v=5),
                       dict(self.safety, starts_inhibited=False)):
            with self.subTest(safety=safety), self.assertRaises(RuntimeError):
                self.deploy.plan(self.release, safety)
        self.assertFalse(self.writes())

    def test_source_hash_changes_after_backup_are_detected(self):
        original = self.deploy.backup
        def mutate(plan, safety):
            result = original(plan, safety)
            (self.release / d.BINARY).write_bytes(b'changed source')
            return result
        with patch.object(self.deploy, 'backup', side_effect=mutate), self.assertRaisesRegex(RuntimeError, 'Release changed'):
            self.deploy.apply(self.release, self.safety)
        self.assert_protected()
        self.assertTrue((self.root / d.BINARY).read_bytes().startswith(b'\x7fELFold'))
        backup = next((self.work / 'backups').glob('easiroc-*'))
        self.deploy.rollback(backup, self.safety)
        self.assert_protected()

    def test_other_clients_changed_blocks_mutation(self):
        plan = self.deploy.plan(self.release, self.safety)
        self.live.clients['fevme'] = 'replacement pid'
        self.live.calls.clear()
        with self.assertRaisesRegex(RuntimeError, 'Other MIDAS clients changed'):
            self.deploy.unchanged(plan)
        self.assertFalse(self.writes())

    def test_legacy_bootstrap_does_not_consult_independent_journals(self):
        source = (Path(d.__file__).parent / 'deploy1_init.sh').read_text()
        self.assertNotIn('easiroc-*/journal.json', source)
        self.assertNotIn('Recover independent EASIROC', source)
        self.assertIn('git -C "$ROOT" diff --quiet', source)
        self.assertIn('status --short --untracked-files=no', source)

    def test_legacy_incomplete_state_refuses_readonly_plan(self):
        (self.runtime / 'deploy-state.json').write_text(json.dumps({'phase': 'in_progress'}))
        before = self.files_snapshot()
        with self.assertRaisesRegex(RuntimeError, 'Legacy deployment incomplete'):
            self.deploy.plan(self.release, self.safety)
        self.assertEqual(self.files_snapshot(), before)
        self.assertFalse(self.writes())

    def test_installed_cmake_dependencies_used_for_release(self):
        from scripts.deploy import prepare_easiroc_release as prepare
        target = self.midas / 'lib/midas-targets.cmake'
        target.write_text('INTERFACE_LINK_LIBRARIES "/usr/lib64/libz.so;-lcurl;/usr/lib64/libsqlite3.so;-lutil;-lrt;dl"')
        self.assertEqual(prepare.link_libraries(self.midas),
                         ['/usr/lib64/libz.so', '-lcurl', '/usr/lib64/libsqlite3.so', '-lutil', '-lrt', '-ldl'])
        target.write_text('INTERFACE_LINK_LIBRARIES "$(bad shell)"')
        with self.assertRaisesRegex(RuntimeError, 'Unsupported MIDAS link'):
            prepare.link_libraries(self.midas)

    def test_check_cli_has_no_host_lock_or_mutation(self):
        with patch.object(d, 'Deployment', return_value=self.deploy), \
             patch.object(d, 'host_only', side_effect=AssertionError('host mutation guard')), \
             patch.object(d, 'lock', side_effect=AssertionError('lock')):
            safety_file = self.work / 'safety.json'
            safety_file.write_text(json.dumps(self.safety))
            before = self.files_snapshot()
            with patch('builtins.print'):
                self.assertEqual(d.main(['--check', '--release', str(self.release), '--safety', str(safety_file)]), 0)
            self.assertEqual(before, self.files_snapshot())
            self.assertFalse(self.writes())


if __name__ == '__main__':
    unittest.main()
