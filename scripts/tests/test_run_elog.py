import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_elog


class FakeOdb:
    def __init__(self, enabled=True):
        root = run_elog.ROOT
        self.values = {
            f"{root}/Enabled": enabled,
            f"{root}/Last Run": 0,
            f"{root}/Last Attempt Run": 0,
            "/DAQ/Status/Runlog/EORCompleteRunNumber": 53,
            f"{root}/Last Status": "DISABLED",
            f"{root}/Last Error": "",
            f"{root}/Author": "DAQ",
            f"{root}/Type": "Routine",
            f"{root}/System": "DAQ",
        }
        self.errors = []

    def integer(self, path):
        return self.values[path]

    def boolean(self, path):
        return self.values[path]

    def string(self, path):
        return self.values[path]

    def set_integer(self, path, value):
        self.values[path] = value

    def set_string(self, path, value):
        self.values[path] = value

    def error_message(self, run, reason):
        self.errors.append((run, reason))


class RunElogTest(unittest.TestCase):
    def setUp(self):
        self.record = {
            "BOR": {"Run number": 53, "Start time": "Tue Sep 29 12:00:00 2026", "Type": "Test"},
            "EOR": {"Stop time": "Tue Sep 29 12:00:07 2026", "Comment": "   ",
                    "Duration": "0x0000000000000007",
                    "VME events": "10", "EASIROC events": "-1",
                    "HUL events": "-1", "Scaler 64ch": {},
                    "DAQ Status": "WARNING", "DAQ Summary": "VME disconnected",
                    "EventSlipCount": "0x0000000000000002"},
        }

    def test_format_uses_runlog_values(self):
        subject, body = run_elog.format_entry(53, self.record)
        self.assertEqual(subject, "Run #53 - Test - WARNING")
        for expected in ("Comment    : (empty)", "Duration   : 7 s",
                         "Start      : 2026/09/29 12:00:00",
                         "Stop       : 2026/09/29 12:00:07",
                         "VME      : 10", "EASI     : N/A (not participating)",
                         "HUL      : N/A (not participating)", "Scaler     : N/A",
                         "DAQ Summary: VME disconnected", "Event Slip : 2"):
            self.assertIn(expected, body)

    def test_scaler_displays_only_present_channels_in_order(self):
        self.record["EOR"]["HUL events"] = "12"
        self.record["EOR"]["Scaler 64ch"] = {
            "ch63": "0x0000000a", "ch01": 7, "ch00": 0}
        _, body = run_elog.format_entry(53, self.record)
        self.assertIn("  HUL      : 12\n\nScaler\n"
                      "  ch00 : 0\n  ch01 : 7\n  ch63 : 0x0000000a\n", body)
        self.assertNotIn("ch02", body)

    def test_format_uses_final_eor_comment(self):
        self.record["BOR"]["Comment"] = "old BOR comment"
        self.record["EOR"]["Comment"] = "During run: detector adjusted"
        _, body = run_elog.format_entry(53, self.record)
        self.assertIn("Comment    : During run: detector adjusted", body)
        self.assertNotIn("old BOR comment", body)

    def test_disabled_and_duplicate_attempt(self):
        odb = FakeOdb(enabled=False)
        self.assertEqual(run_elog.process_run(odb, 53), "DISABLED")
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Status"], "DISABLED")
        self.assertEqual(run_elog.process_run(odb, 53), "ALREADY_ATTEMPTED")

    def test_incomplete_eor_rejected_with_and_without_retry(self):
        odb = FakeOdb()
        odb.values["/DAQ/Status/Runlog/EORCompleteRunNumber"] = 57
        with patch.object(run_elog, "post_melog") as post, \
             patch.object(run_elog.time, "sleep") as sleep:
            for retry in (False, True):
                with self.assertRaisesRegex(RuntimeError, "EOR is not complete for run 53"):
                    run_elog.process_run(odb, 53, retry=retry)
            post.assert_not_called()
            sleep.assert_not_called()
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Attempt Run"], 0)

    def test_bor_only_times_out_without_posting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runlog.json"
            path.write_text(json.dumps({"BOR": self.record["BOR"]}))
            with patch.object(run_elog, "runlog_path", return_value=path), \
                 patch.object(run_elog, "post_melog") as post:
                for retry in (False, True):
                    odb = FakeOdb()
                    self.assertEqual(run_elog.process_run(
                        odb, 53, retry=retry, runlog_wait_seconds=0.02,
                        runlog_retry_interval_seconds=0.002), "ERROR")
                    self.assertIn("Logger JSON did not become complete within 0.02 seconds",
                                  odb.values[f"{run_elog.ROOT}/Last Error"])
                    self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Run"], 0)
                    self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Status"], "ERROR")
                    self.assertEqual(len(odb.errors), 1)
                post.assert_not_called()

    def test_bor_only_then_eor_retries_and_posts(self):
        self.assert_transient_runlog_posts(json.dumps({"BOR": self.record["BOR"]}))

    def test_invalid_json_then_complete_retries_and_posts(self):
        self.assert_transient_runlog_posts('{"BOR":')

    def test_missing_file_then_complete_retries_and_posts(self):
        self.assert_transient_runlog_posts(None)

    def assert_transient_runlog_posts(self, initial_text):
        odb = FakeOdb()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runlog.json"
            if initial_text is not None:
                path.write_text(initial_text)
            elog = Path(directory) / "elog.log"

            def complete_runlog(_):
                path.write_text(json.dumps(self.record))

            def submit(run, subject, body, *_):
                elog.write_text(f"$Start$\nRun: {run}\nSubject: {subject}\n{body}$End$\n")

            with patch.object(run_elog, "runlog_path", return_value=path), \
                 patch.object(run_elog, "elog_file", return_value=elog), \
                 patch.object(run_elog.time, "sleep", side_effect=complete_runlog) as sleep, \
                 patch.object(run_elog, "post_melog", side_effect=submit) as post:
                self.assertEqual(run_elog.process_run(
                    odb, 53, runlog_wait_seconds=0.5,
                    runlog_retry_interval_seconds=0.001), "OK")
                sleep.assert_called_once()
                post.assert_called_once()
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Run"], 53)

    def test_wrong_bor_run_fails_without_waiting(self):
        odb = FakeOdb()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runlog.json"
            wrong = {"BOR": dict(self.record["BOR"], **{"Run number": 54})}
            path.write_text(json.dumps(wrong))
            with patch.object(run_elog, "runlog_path", return_value=path), \
                 patch.object(run_elog.time, "sleep") as sleep, \
                 patch.object(run_elog, "post_melog") as post:
                self.assertEqual(run_elog.process_run(odb, 53), "ERROR")
                sleep.assert_not_called()
                post.assert_not_called()
        self.assertIn("BOR run number does not match", odb.values[f"{run_elog.ROOT}/Last Error"])

    def test_success_and_duplicate_success(self):
        odb = FakeOdb()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runlog.json"
            path.write_text(json.dumps(self.record))
            elog = Path(directory) / "elog.log"
            def submit(run, subject, body, *_):
                elog.write_text(f"$Start$\nRun: {run}\nSubject: {subject}\n{body}$End$\n")
            with patch.object(run_elog, "runlog_path", return_value=path), \
                 patch.object(run_elog, "elog_file", return_value=elog), \
                 patch.object(run_elog.time, "sleep") as sleep, \
                 patch.object(run_elog, "post_melog", side_effect=submit) as post:
                self.assertEqual(run_elog.process_run(odb, 53), "OK")
                self.assertEqual(run_elog.process_run(odb, 53), "ALREADY_POSTED")
                sleep.assert_not_called()
                post.assert_called_once()
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Run"], 53)
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Status"], "OK")
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Error"], "")

    def test_failure_records_one_error(self):
        odb = FakeOdb()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runlog.json"
            path.write_text(json.dumps(self.record))
            with patch.object(run_elog, "runlog_path", return_value=path), \
                 patch.object(run_elog, "elog_file", return_value=Path(directory) / "elog.log"), \
                 patch.object(run_elog.time, "sleep") as sleep, \
                 patch.object(run_elog, "post_melog",
                              side_effect=RuntimeError("connection refused")) as post:
                self.assertEqual(run_elog.process_run(odb, 53), "ERROR")
                self.assertEqual(run_elog.process_run(odb, 53), "ALREADY_ATTEMPTED")
                sleep.assert_not_called()
                post.assert_called_once()
        self.assertEqual(len(odb.errors), 1)
        self.assertIn("connection refused", odb.values[f"{run_elog.ROOT}/Last Error"])
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Status"], "ERROR")

    def test_timeout_records_error_without_marking_run_posted(self):
        odb = FakeOdb()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runlog.json"
            path.write_text(json.dumps(self.record))
            with patch.object(run_elog, "runlog_path", return_value=path), \
                 patch.object(run_elog, "elog_file", return_value=Path(directory) / "elog.log"), \
                 patch.object(run_elog, "post_melog",
                              side_effect=subprocess.TimeoutExpired("melog", 10)):
                self.assertEqual(run_elog.process_run(odb, 53), "ERROR")
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Run"], 0)
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Status"], "ERROR")
        self.assertEqual(odb.values[f"{run_elog.ROOT}/Last Error"],
                         "melog timed out after 10 s")
        self.assertEqual(len(odb.errors), 1)

    def test_cli_reports_post_failure(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(run_elog, "LOCK", str(Path(directory) / "lock")), \
             patch.object(run_elog, "Odb") as odb_class, \
             patch.object(run_elog, "process_run", return_value="ERROR"), \
             patch.object(sys, "argv", ["run_elog.py", "--run", "53"]):
            self.assertEqual(run_elog.main(), 1)
            odb_class.return_value.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
