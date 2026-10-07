import io
import unittest
from unittest import mock
from urllib.error import URLError

from scripts import odb_settings_diff as diff


def leaf(value, tid=8, length=1):
    metadata = {"type": tid}
    if length != 1:
        metadata["num_values"] = length
    return value, metadata


def directory(children):
    data = {}
    for name, (value, meta) in children.items():
        data[name] = value
        data[f"{name}/key"] = meta
    return data, {"type": 15, "num_values": 1}


def snapshot(busy, *, tid=8, length=1, histogram=8):
    rpv = directory({"SingleEventBusyEnabled": leaf(busy, tid, length),
                     "EventCount": leaf(123, 7)})
    settings = directory({"RPV130": rpv})
    status = directory({"Something": leaf(True)})
    variables = directory({"Temperature": leaf(42, 7)})
    vme = directory({"Settings": settings, "Status": status,
                     "Variables": variables})
    equipment = directory({"VME": vme})
    hist = directory({"FillPrescale": leaf(histogram, 7)})
    commands = directory({"Reset": leaf(False)})
    analyzer = directory({"OnlineHistogram": hist, "Commands": commands})
    return {"/Equipment": {"data": equipment[0], "key": equipment[1]},
            "/Analyzer": {"data": analyzer[0], "key": analyzer[1]}}


class OdbSettingsDiffTest(unittest.TestCase):
    def test_requested_busy_difference_is_value(self):
        rows = diff.compare(snapshot(True), snapshot(False))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][:2],
                         ("/Equipment/VME/Settings/RPV130/SingleEventBusyEnabled",
                          ("VALUE",)))
        rendered = diff.format_diff(rows)
        self.assertIn("dev:  true", rendered)
        self.assertIn("prod: false", rendered)

    def test_type_length_and_missing_are_distinct(self):
        dev = snapshot([True, False], length=2, histogram=8)
        prod = snapshot(False, tid=7, length=1, histogram=8)
        prod["/Analyzer"]["data"]["OnlineHistogram"].pop("FillPrescale")
        prod["/Analyzer"]["data"]["OnlineHistogram"].pop("FillPrescale/key")
        rows = diff.compare(dev, prod)
        self.assertEqual(rows[0][1], ("MISSING",))
        self.assertEqual(rows[1][1], ("TYPE", "LENGTH", "VALUE"))
        self.assertIn("length=2", diff.format_diff(rows))
        self.assertIn("<missing>", diff.format_diff(rows))

    def test_runtime_fields_do_not_appear(self):
        left, right = snapshot(True), snapshot(True)
        right["/Equipment"]["data"]["VME"]["Status"]["Something"] = False
        right["/Analyzer"]["data"]["Commands"]["Reset"] = True
        self.assertEqual(diff.compare(left, right), [])

    def test_test_equipment_is_excluded(self):
        left, right = snapshot(True), snapshot(True)
        for name in ("test_bulk", "test_rpc"):
            data, meta = directory({"Settings": directory({"Value": leaf([1] * 128, 7, 128)})})
            left["/Equipment"]["data"][name] = data
            left["/Equipment"]["data"][f"{name}/key"] = meta
        self.assertEqual(diff.compare(left, right), [])

    def test_uniform_array_is_compact(self):
        rendered = diff.format_diff(diff.compare(
            snapshot([512] * 128, tid=7, length=128),
            snapshot([513] * 128, tid=7, length=128)))
        self.assertIn("[512 × 128]", rendered)
        self.assertIn("[513 × 128]", rendered)
        self.assertNotIn("512, 512", rendered)

    def test_sparse_array_shows_changed_indices(self):
        dev = [512] * 128
        prod = dev.copy()
        prod[3], prod[127] = 256, 1024
        rendered = diff.format_diff(diff.compare(
            snapshot(dev, tid=7, length=128),
            snapshot(prod, tid=7, length=128)))
        self.assertIn("2/128 indices differ", rendered)
        self.assertIn("[3] dev=512 prod=256", rendered)
        self.assertIn("[127] dev=512 prod=1024", rendered)
        self.assertNotIn("512, 512", rendered)

    def test_many_changed_indices_are_bounded(self):
        dev = list(range(128))
        prod = [value + 1 for value in dev]
        rendered = diff.format_diff(diff.compare(
            snapshot(dev, tid=7, length=128),
            snapshot(prod, tid=7, length=128)))
        self.assertIn("128/128 indices differ", rendered)
        self.assertIn("+116 more differing indices", rendered)
        self.assertNotIn("[12] dev=", rendered)

    def test_invalid_metadata_fails_instead_of_hiding_difference(self):
        value = snapshot(True)
        del value["/Equipment"]["data"]["VME"]["Settings"]["RPV130"]["SingleEventBusyEnabled/key"]
        with self.assertRaisesRegex(ValueError, "metadata"):
            diff.flatten(value)

    def test_experiment_preflight_checks_both_ports_before_copy(self):
        names = iter(({"status": [1], "data": ["daq-dev"]},
                      {"status": [1], "data": ["daq"]}))
        calls = []

        def rpc(url, method, paths):
            calls.append((url, method, paths))
            return next(names)

        with mock.patch.object(diff.os, "uname") as uname, \
             mock.patch.object(diff, "rpc_call", side_effect=rpc), \
             mock.patch.object(diff, "fetch", side_effect=[snapshot(True), snapshot(False)]) as fetch, \
             mock.patch("sys.stdout", new_callable=io.StringIO) as output:
            uname.return_value.nodename = "nexdaq1"
            self.assertEqual(diff.main([]), 0)
        self.assertEqual(calls, [
            (diff.DEV_URL, "db_get_values", ["/Experiment/Name"]),
            (diff.PROD_URL, "db_get_values", ["/Experiment/Name"]),
        ])
        self.assertEqual([call.args[0] for call in fetch.call_args_list],
                         [diff.DEV_URL, diff.PROD_URL])
        self.assertIn("VALUE /Equipment/VME/Settings/RPV130/SingleEventBusyEnabled",
                      output.getvalue())

    def test_wrong_experiment_stops_before_copy(self):
        for names, expected in [(["other"], "8181"),
                                (["daq-dev", "other"], "8081")]:
            with self.subTest(names=names), \
                 mock.patch.object(diff.os, "uname") as uname, \
                 mock.patch.object(diff, "rpc_call") as rpc, \
                 mock.patch.object(diff, "fetch") as fetch, \
                 mock.patch("sys.stderr", new_callable=io.StringIO) as errors:
                uname.return_value.nodename = "nexdaq1"
                rpc.side_effect = [{"status": [1], "data": [name]} for name in names]
                self.assertEqual(diff.main([]), 2)
                fetch.assert_not_called()
                self.assertIn(expected, errors.getvalue())
                self.assertIn("Unexpected experiment", errors.getvalue())

    def test_connection_failure_is_clear_and_stops_before_copy(self):
        with mock.patch.object(diff.os, "uname") as uname, \
             mock.patch.object(diff, "rpc_call") as rpc, \
             mock.patch.object(diff, "fetch") as fetch, \
             mock.patch("sys.stderr", new_callable=io.StringIO) as errors:
            uname.return_value.nodename = "nexdaq1"
            rpc.side_effect = RuntimeError("Cannot read http://127.0.0.1:8181/?mjsonrpc (db_get_values): connection refused")
            self.assertEqual(diff.main([]), 2)
            fetch.assert_not_called()
            self.assertIn("8181", errors.getvalue())
            self.assertIn("connection refused", errors.getvalue())

    def test_rpc_transport_error_names_endpoint_and_read_method(self):
        with mock.patch.object(diff.urllib.request, "urlopen", side_effect=URLError("refused")):
            with self.assertRaisesRegex(RuntimeError, "8181.*db_get_values.*refused"):
                diff.verify_experiment(diff.DEV_URL, "daq-dev")

    def test_rpc_rejects_write_method_before_http(self):
        with mock.patch.object(diff.urllib.request, "urlopen") as open_url:
            with self.assertRaisesRegex(ValueError, "non-read-only"):
                diff.rpc_call(diff.PROD_URL, "db_paste", ["/Experiment/Name"])
            open_url.assert_not_called()


if __name__ == "__main__":
    unittest.main()
