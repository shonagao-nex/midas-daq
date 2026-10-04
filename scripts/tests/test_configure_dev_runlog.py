import unittest
from unittest.mock import patch

from scripts import configure_dev_runlog as config


class SignedSlipsTest(unittest.TestCase):
    def test_migrates_existing_qword_and_preserves_value(self):
        path = "/DAQ/Status/Runlog/EventSlipCount"
        state = {"type": 18, "value": "0x0000000000000011"}
        calls = []

        def get(name):
            self.assertEqual(name, path)
            return (1, state["value"]) if state["type"] else (0, None)

        def rpc(method, params):
            calls.append(method)
            self.assertEqual(params[0]["path"] if method == "db_create" else params["paths"][0], path)
            if method == "db_key":
                return {"status": [1], "keys": [{"type": state["type"]}]}
            if method == "db_delete":
                state.update(type=0, value=None)
            elif method == "db_create":
                state.update(type=params[0]["type"], value=0)
            elif method == "db_paste":
                state["value"] = params["values"][0]
            return {"status": [1]}

        with patch.object(config, "get", get), patch.object(config, "rpc", rpc):
            config.ensure_signed_slips(path)
            config.ensure_signed_slips(path)

        self.assertEqual(state, {"type": 17, "value": 17})
        self.assertEqual(calls, ["db_key", "db_delete", "db_create", "db_paste", "db_key"])


if __name__ == "__main__":
    unittest.main()
