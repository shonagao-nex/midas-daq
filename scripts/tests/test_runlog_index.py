"""Checks for the derived, atomically replaced Runlog filename index."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts.update_runlog_index import rebuild_index


class RunlogIndexTest(unittest.TestCase):
    def test_existing_files_only_including_incomplete_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "runlog_000063.json").write_text('{"BOR": {}, "EOR": {}}')
            (directory / "runlog_000058.json").write_text('{"BOR": {},')
            (directory / "runlog_000061.json").write_text("{}")
            (directory / "runlog_000062.json.tmp").write_text("{}")
            (directory / "runlog_000060.json").mkdir()
            self.assertEqual(rebuild_index(directory), [63, 61, 58])
            self.assertEqual(json.loads((directory / "runlog_index.json").read_text()),
                             {"runs": [63, 61, 58]})

            (directory / "runlog_000061.json").unlink()
            (directory / "runlog_000064.json").write_text("{}")
            self.assertEqual(rebuild_index(directory), [64, 63, 58])

    def test_failed_replace_preserves_previous_index(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "runlog_000063.json").write_text("{}")
            rebuild_index(directory)
            previous = (directory / "runlog_index.json").read_bytes()
            (directory / "runlog_000064.json").write_text("{}")
            with patch("scripts.update_runlog_index.os.replace", side_effect=OSError("failed")):
                with self.assertRaises(OSError):
                    rebuild_index(directory)
            self.assertEqual((directory / "runlog_index.json").read_bytes(), previous)
            self.assertEqual(list(directory.glob(".runlog_index.*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
