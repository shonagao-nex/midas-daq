import unittest

from scripts.deploy import deploy3_configure_live_odb as schema


class FakeRpc:
    def __init__(self, tid=None, value=None, run_state=1):
        self.tid = tid
        self.data = value
        self.run_state = run_state
        self.calls = []

    def key(self, path):
        assert path == f"{schema.STATUS}/EventSlipCount"
        return None if self.tid is None else {"type": self.tid, "num_values": 1}

    def value(self, path):
        if path == "/Runinfo/State":
            return self.run_state
        if path == "/Runinfo/Transition in progress":
            return 0
        assert path == f"{schema.STATUS}/EventSlipCount"
        return self.data

    def checked(self, method, params):
        self.calls.append(method)
        if method == "db_delete":
            self.tid, self.data = None, None
        elif method == "db_create":
            self.tid, self.data = params[0]["type"], 0
        elif method == "db_paste":
            self.data = params["values"][0]
        else:
            raise AssertionError(method)


class RunlogSlipSchemaTest(unittest.TestCase):
    def test_only_runlog_slips_change_type(self):
        specs = {path: tid for path, tid, _, _ in schema.specs()}
        self.assertEqual(specs[f"{schema.STATUS}/EventSlipCount"], schema.INT64)
        self.assertEqual(specs[f"{schema.STATUS}/DurationSec"], schema.UINT64)
        self.assertEqual(specs[f"{schema.STATUS}/VMEEvents"], schema.INT64)
        self.assertIn(("EventSlipCount", f"{schema.STATUS}/EventSlipCount"), schema.EOR)

    def test_missing_key_is_created_as_signed(self):
        rpc = FakeRpc()
        schema.ensure_signed_slips(rpc)
        self.assertEqual((rpc.tid, rpc.data), (schema.INT64, 0))
        self.assertEqual(rpc.calls, ["db_create", "db_paste"])

    def test_existing_signed_key_is_unchanged(self):
        rpc = FakeRpc(schema.INT64, -1)
        schema.ensure_signed_slips(rpc)
        self.assertEqual((rpc.tid, rpc.data, rpc.calls), (schema.INT64, -1, []))

    def test_legacy_unsigned_value_is_preserved(self):
        for value in (0, "0x0000000000000011", 0x7FFFFFFFFFFFFFFF):
            with self.subTest(value=value):
                rpc = FakeRpc(schema.UINT64, value)
                schema.ensure_signed_slips(rpc)
                expected = int(value, 16) if isinstance(value, str) else value
                self.assertEqual((rpc.tid, rpc.data), (schema.INT64, expected))
                self.assertEqual(rpc.calls, ["db_delete", "db_create", "db_paste"])
                self.assertTrue(schema.inspect_key(rpc, f"{schema.STATUS}/EventSlipCount", schema.INT64, 0))

    def test_converted_signed_value_is_verified_as_decimal_string(self):
        class DecimalSignedRpc(FakeRpc):
            def value(self, path):
                value = super().value(path)
                if path == f"{schema.STATUS}/EventSlipCount" and self.tid == schema.INT64:
                    return str(value)
                return value

        for value in (0, 17):
            with self.subTest(value=value):
                rpc = DecimalSignedRpc(schema.UINT64, f"0x{value:016x}")
                schema.ensure_signed_slips(rpc)
                self.assertEqual((rpc.tid, rpc.data), (schema.INT64, value))
                self.assertEqual(rpc.value(f"{schema.STATUS}/EventSlipCount"), str(value))
                self.assertEqual(rpc.calls, ["db_delete", "db_create", "db_paste"])

    def test_out_of_range_and_other_types_are_rejected_without_writes(self):
        for tid, value in ((schema.UINT64, "0x8000000000000000"), (schema.INT32, 5)):
            with self.subTest(tid=tid):
                rpc = FakeRpc(tid, value)
                with self.assertRaises(RuntimeError):
                    schema.ensure_signed_slips(rpc)
                self.assertEqual(rpc.calls, [])

    def test_active_run_blocks_conversion_before_writes(self):
        rpc = FakeRpc(schema.UINT64, 0, run_state=3)
        with self.assertRaises(RuntimeError):
            schema.ensure_signed_slips(rpc)
        self.assertEqual((rpc.tid, rpc.data, rpc.calls), (schema.UINT64, 0, []))

    def test_read_only_schema_check_rejects_unsigned_key(self):
        rpc = FakeRpc(schema.UINT64, 0)
        with self.assertRaises(RuntimeError):
            schema.inspect_key(rpc, f"{schema.STATUS}/EventSlipCount", schema.INT64, 0)


class DaqSettingsSchemaTest(unittest.TestCase):
    class ReadOnlyRpc:
        def __init__(self):
            self.keys = {}
            self.children = {}
            for path, tid, count, minimum in schema.daq_settings_specs():
                self.keys[path] = {"type": tid, "num_values": count,
                                   "item_size": minimum or 16}

        def key(self, path):
            return self.keys.get(path)

        def copy(self, path):
            return {name: {} for name in self.children[path]}

        def checked(self, *args):
            raise AssertionError("schema validation attempted a write")

        def add_dir(self, path, children=()):
            self.keys[path] = {"type": schema.KEY, "num_values": 1}
            self.children[path] = children

        def add_histogram(self, group, count=3):
            base = f"/Analyzer/Histograms/{group}"
            self.add_dir(base)
            for field in ("HistName", "Title", "XTitle", "YTitle", "Type",
                          "Expression", "Cut"):
                self.keys[f"{base}/{field}"] = {"type": schema.STRING,
                    "num_values": count, "item_size": 8}
            for field, tid in (("Bins", schema.INT32), ("Min", schema.DOUBLE),
                               ("Max", schema.DOUBLE), ("Enabled", schema.BOOL)):
                self.keys[f"{base}/{field}"] = {"type": tid, "num_values": count}

    def test_all_frontend_specs_and_absent_analyzer_pass_without_writes(self):
        rpc = self.ReadOnlyRpc()
        schema.inspect_daq_settings_schema(rpc)

    def test_frontend_errors_report_path_expected_and_actual(self):
        path = "/Equipment/VME/Settings/V1190/ChannelEnabled"
        for replacement, expected_actual in (
                (None, "missing"),
                ({"type": schema.INT32, "num_values": 128}, "INT[128]"),
                ({"type": schema.BOOL, "num_values": 32}, "BOOL[32]")):
            with self.subTest(actual=expected_actual):
                rpc = self.ReadOnlyRpc()
                if replacement is None:
                    del rpc.keys[path]
                else:
                    rpc.keys[path] = replacement
                with self.assertRaisesRegex(RuntimeError, path) as failure:
                    schema.inspect_daq_settings_schema(rpc)
                self.assertIn("BOOL[128]", str(failure.exception))
                self.assertIn(expected_actual, str(failure.exception))

    def test_string_capacity_is_required(self):
        rpc = self.ReadOnlyRpc()
        path = "/Equipment/EASIROC/Settings/Network/IPAddress"
        rpc.keys[path]["item_size"] = 32
        with self.assertRaisesRegex(RuntimeError, "item_size>=64.*item_size=32"):
            schema.inspect_daq_settings_schema(rpc)

    def test_custom_histogram_and_page_schema(self):
        rpc = self.ReadOnlyRpc()
        rpc.add_dir("/Analyzer")
        rpc.add_dir("/Analyzer/Histograms", ("Custom",))
        rpc.add_histogram("Custom", 3)
        rpc.add_dir("/Analyzer/Pages", ("MyPage",))
        rpc.add_dir("/Analyzer/Pages/MyPage")
        for field in ("Rows", "Columns"):
            rpc.keys[f"/Analyzer/Pages/MyPage/{field}"] = {
                "type": schema.INT32, "num_values": 1}
        schema.inspect_daq_settings_schema(rpc)  # Pad01 is optional.

        path = "/Analyzer/Histograms/Custom/Enabled"
        rpc.keys[path]["num_values"] = 2
        with self.assertRaisesRegex(RuntimeError, "Enabled: expected BOOL\\[3\\], actual BOOL\\[2\\]"):
            schema.inspect_daq_settings_schema(rpc)
        rpc.keys[path]["num_values"] = 3
        del rpc.keys["/Analyzer/Pages/MyPage/Columns"]
        with self.assertRaisesRegex(RuntimeError, "Columns: expected INT\\[1\\], actual missing"):
            schema.inspect_daq_settings_schema(rpc)

    def test_histogram_count_and_analyzer_directory_are_checked(self):
        rpc = self.ReadOnlyRpc()
        rpc.keys["/Analyzer"] = {"type": schema.STRING, "num_values": 1,
                                  "item_size": 8}
        with self.assertRaisesRegex(RuntimeError, "/Analyzer: expected KEY\\[1\\]"):
            schema.inspect_daq_settings_schema(rpc)
        rpc.add_dir("/Analyzer")
        rpc.add_dir("/Analyzer/Histograms", ("Custom",))
        rpc.add_histogram("Custom", 0)
        with self.assertRaisesRegex(RuntimeError, "HistName: expected STRING\\[N\\], N>=1"):
            schema.inspect_daq_settings_schema(rpc)

    def test_configure_rejects_bad_settings_before_owned_writes(self):
        from unittest.mock import patch

        rpc = self.ReadOnlyRpc()
        del rpc.keys["/Equipment/VME/Settings/V775/FullScaleRange"]
        with patch.object(schema, "require_live"):
            with self.assertRaisesRegex(RuntimeError, "FullScaleRange"):
                schema.configure(rpc)

    def test_deploy4_schema_check_runs_same_daq_validation(self):
        from unittest.mock import patch
        from scripts.deploy import deploy4_check_live as deploy4

        rpc = self.ReadOnlyRpc()
        del rpc.keys["/Equipment/EASIROC/Settings/ASIC2/InputDAC"]
        with patch.object(deploy4.schema, "specs", return_value=[]), \
             patch.object(deploy4.schema, "inspect_links"), \
             patch.object(deploy4.schema, "MANAGED_VALUES", {}), \
             patch.object(deploy4.schema, "inspect_live_paths"):
            with self.assertRaisesRegex(RuntimeError, "ASIC2/InputDAC"):
                deploy4.schema.inspect_schema(rpc)


if __name__ == "__main__":
    unittest.main()
