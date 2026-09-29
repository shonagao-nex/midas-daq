#!/usr/bin/env python3
"""Rebuild the derived Runlog filename index after a completed STOP."""

import argparse
import json
import os
from pathlib import Path
import re
import tempfile


RUNLOG_NAME = re.compile(r"runlog_([0-9]{6,})\.json\Z")
INDEX_NAME = "runlog_index.json"


def existing_runs(directory):
    runs = []
    for entry in directory.iterdir():
        match = RUNLOG_NAME.fullmatch(entry.name)
        if match and entry.is_file():
            number = int(match.group(1))
            if number > 0:
                runs.append(number)
    return sorted(set(runs), reverse=True)


def rebuild_index(directory):
    directory = Path(directory)
    runs = existing_runs(directory)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", dir=directory,
                prefix=".runlog_index.", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            json.dump({"runs": runs}, output, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, directory / INDEX_NAME)
        directory_fd = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return runs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="MIDAS JSON Runlog directory")
    args = parser.parse_args()
    runs = rebuild_index(args.directory)
    print(f"Indexed {len(runs)} Runlogs in {args.directory}")


if __name__ == "__main__":
    main()
