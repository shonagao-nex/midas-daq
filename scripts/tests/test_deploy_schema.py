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


if __name__ == "__main__":
    unittest.main()
