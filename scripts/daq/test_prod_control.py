#!/usr/bin/env python3
"""Pure mock checks; no systemd, ODB, or process operations."""

import unittest
from unittest.mock import patch

import prod_control as control


class ControlTests(unittest.TestCase):
    def test_client_names_match_real_midas_registrations(self):
        clients = {"12": {"Name": "Logger"}, "13": {"Name": "ana"}}
        self.assertEqual([12], [p for p, _ in control.clients_for(clients, "Logger")])
        self.assertEqual([13], [p for p, _ in control.clients_for(clients, "ana")])
        self.assertEqual([], control.clients_for(clients, "mlogger"))

    def test_stop_refuses_missing_drop_in(self):
        with patch.object(control, "properties",
                          return_value={"SendSIGKILL": "yes", "KillMode": "control-group"}):
            with self.assertRaisesRegex(control.Unsafe, "SendSIGKILL"):
                control.stop_guard()

    def test_stop_refuses_active_run_without_systemctl_stop(self):
        with patch.object(control, "host_guard"), patch.object(control, "stop_guard"), \
             patch.object(control, "odb_snapshot", return_value=(3, 0, {})), \
             patch.object(control, "systemctl") as systemctl:
            with self.assertRaisesRegex(control.Unsafe, "STOPPED required"):
                control.stop()
            systemctl.assert_not_called()

    def test_status_reports_unknown_when_odb_unavailable(self):
        with patch.object(control, "odb_snapshot", side_effect=OSError("offline")), \
             patch.object(control, "properties", return_value={
                 "ActiveState": "active", "SubState": "running", "MainPID": "12"}), \
             patch.object(control, "proc_identity", return_value=None), \
             patch.object(control, "port_listening", return_value=False), \
             patch("builtins.print") as output:
            control.status()
            self.assertIn("UNKNOWN", str(output.call_args_list))


if __name__ == "__main__":
    unittest.main()
