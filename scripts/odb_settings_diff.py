#!/usr/bin/env python3
"""Read-only comparison of development and production MIDAS configuration."""

import argparse
import json
import os
from pathlib import Path
import sys
import urllib.request

DEV_URL = "http://127.0.0.1:8181/?mjsonrpc"
PROD_URL = "http://127.0.0.1:8081/?mjsonrpc"
ROOTS = ("/Equipment", "/Analyzer")
READ_METHODS = frozenset({"db_get_values", "db_copy", "db_key"})
SUCCESS, MISSING = 1, 312
TYPES = {1: "BYTE", 2: "SBYTE", 3: "CHAR", 4: "WORD", 5: "SHORT",
         6: "DWORD", 7: "INT", 8: "BOOL", 9: "FLOAT", 10: "DOUBLE",
         11: "BITFIELD", 12: "STRING", 13: "ARRAY", 14: "STRUCT",
         15: "KEY", 16: "LINK", 17: "INT64", 18: "UINT64"}
EXCLUDED = {"status", "variables", "commands", "runsnapshot", "runinfo",
            "histogrampdf", "counters", "counter"}
EXCLUDED_EQUIPMENT = {"test_bulk", "test_rpc"}
MAX_ARRAY_CHANGES = 12


def included(path):
    parts = path.strip("/").split("/")
    if any(part.casefold() in EXCLUDED or part.casefold().endswith("counter")
           or part.casefold().endswith("count") for part in parts):
        return False
    if parts[0] == "Equipment":
        return (len(parts) >= 3 and parts[1].casefold() not in EXCLUDED_EQUIPMENT
                and parts[2] == "Settings")
    return parts[0] == "Analyzer"


def flatten(snapshot):
    """Flatten MIDAS db_copy JSON, retaining its /key type and array metadata."""
    result = {}

    def visit(path, data, meta=None):
        # MIDAS save JSON omits /key metadata for nested directories.
        if isinstance(data, dict) and meta is None:
            meta = {"type": 15}
        if not isinstance(meta, dict) or "type" not in meta:
            raise ValueError(f"Missing ODB /key metadata: {path}")
        tid = meta["type"]
        if tid == 15:
            if not isinstance(data, dict):
                raise ValueError(f"ODB directory is not an object: {path}")
            for name, value in data.items():
                if name.endswith("/key"):
                    continue
                visit(f"{path}/{name}", value, data.get(f"{name}/key"))
        elif included(path):
            length = meta.get("num_values", 1)
            if not isinstance(length, int) or length < 1:
                raise ValueError(f"Invalid ODB array length: {path}")
            result[path] = (tid, length, data)

    for root in ROOTS:
        entry = snapshot.get(root)
        if entry is not None:
            if not isinstance(entry, dict) or "data" not in entry or "key" not in entry:
                raise ValueError(f"Invalid snapshot root: {root}")
            visit(root, entry["data"], entry["key"])
    return result


def rpc_call(url, method, paths):
    if method not in READ_METHODS:
        raise ValueError(f"Refusing non-read-only RPC method: {method}")
    payload = json.dumps({"jsonrpc": "2.0", "method": method,
                          "params": {"paths": paths}, "id": 1}).encode()
    request = urllib.request.Request(url, payload,
                                     {"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=8) as response:
            reply = json.load(response)
    except (OSError, ValueError) as error:
        raise RuntimeError(f"Cannot read {url} ({method}): {error}") from error
    if not isinstance(reply, dict) or "result" not in reply:
        detail = reply.get("error") if isinstance(reply, dict) else reply
        raise RuntimeError(f"Invalid {method} response from {url}: {detail}")
    return reply["result"]


def verify_experiment(url, expected):
    result = rpc_call(url, "db_get_values", ["/Experiment/Name"])
    try:
        status, actual = result["status"][0], result["data"][0]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(f"Cannot identify experiment at {url}: malformed response") from error
    if status != SUCCESS:
        raise RuntimeError(f"Cannot identify experiment at {url}: ODB status {status}")
    if actual != expected:
        raise RuntimeError(f"Unexpected experiment at {url}: expected {expected!r}, got {actual!r}")


def fetch(url):
    snapshot = {}
    for root in ROOTS:
        result = rpc_call(url, "db_copy", [root])
        status = result["status"][0]
        if status == MISSING:
            continue
        if status != SUCCESS:
            raise RuntimeError(f"{root}: db_copy status {status}")
        data = result["data"][0]
        # db_copy includes the root's children and their /key metadata. db_key
        # supplies metadata for the root itself.
        key_result = rpc_call(url, "db_key", [root])
        if key_result["status"][0] != SUCCESS:
            raise RuntimeError(f"{root}: db_key failed")
        snapshot[root] = {"data": data, "key": key_result["keys"][0]}
    return snapshot


def compare(dev, prod):
    left, right = flatten(dev), flatten(prod)
    differences = []
    for path in sorted(left.keys() | right.keys()):
        a, b = left.get(path), right.get(path)
        if a is None or b is None:
            kinds = ("MISSING",)
        else:
            kinds = tuple(kind for kind, different in
                          (("TYPE", a[0] != b[0]), ("LENGTH", a[1] != b[1]),
                           ("VALUE", a[2] != b[2])) if different)
        if kinds:
            differences.append((path, kinds, a, b))
    return differences


def describe(entry):
    if entry is None:
        return "<missing>"
    tid, length, value = entry
    if isinstance(value, list) and value:
        if all(item == value[0] for item in value):
            shown = f"[{json.dumps(value[0], ensure_ascii=False)} × {len(value)}]"
        else:
            preview = ", ".join(json.dumps(item, ensure_ascii=False)
                                for item in value[:MAX_ARRAY_CHANGES])
            remainder = f", … +{len(value) - MAX_ARRAY_CHANGES} more" if len(value) > MAX_ARRAY_CHANGES else ""
            shown = f"[{preview}{remainder}]"
    else:
        shown = json.dumps(value, ensure_ascii=False, sort_keys=True)
    return f"{shown} [type={TYPES.get(tid, tid)}, length={length}]"


def format_diff(differences):
    if not differences:
        return "No configuration differences."
    lines = []
    for path, kinds, dev, prod in differences:
        lines.append(f"{'/'.join(kinds)} {path}")
        if (dev is not None and prod is not None and dev[:2] == prod[:2]
                and isinstance(dev[2], list) and isinstance(prod[2], list)
                and len(dev[2]) == len(prod[2]) and len(dev[2]) > 1):
            changes = [(i, a, b) for i, (a, b) in
                       enumerate(zip(dev[2], prod[2])) if a != b]
            both_uniform = (all(item == dev[2][0] for item in dev[2])
                            and all(item == prod[2][0] for item in prod[2]))
            if changes and not both_uniform:
                lines.append(f"  {len(changes)}/{len(dev[2])} indices differ [type={TYPES.get(dev[0], dev[0])}]")
                lines.extend(f"  [{i}] dev={json.dumps(a, ensure_ascii=False)} prod={json.dumps(b, ensure_ascii=False)}"
                             for i, a, b in changes[:MAX_ARRAY_CHANGES])
                if len(changes) > MAX_ARRAY_CHANGES:
                    lines.append(f"  … +{len(changes) - MAX_ARRAY_CHANGES} more differing indices")
                continue
        lines.extend((f"  dev:  {describe(dev)}", f"  prod: {describe(prod)}"))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dev-snapshot", type=Path,
                        help="saved db_copy JSON snapshot for offline comparison")
    parser.add_argument("--prod-snapshot", type=Path,
                        help="saved db_copy JSON snapshot for offline comparison")
    args = parser.parse_args(argv)
    if bool(args.dev_snapshot) != bool(args.prod_snapshot):
        parser.error("supply both snapshot paths or neither")
    try:
        if args.dev_snapshot:
            dev = json.loads(args.dev_snapshot.read_text(encoding="utf-8"))
            prod = json.loads(args.prod_snapshot.read_text(encoding="utf-8"))
        else:
            if os.uname().nodename.split(".")[0] != "nexdaq1":
                raise RuntimeError("Live comparison must run on nexdaq1")
            verify_experiment(DEV_URL, "daq-dev")
            verify_experiment(PROD_URL, "daq")
            dev, prod = fetch(DEV_URL), fetch(PROD_URL)
        print(format_diff(compare(dev, prod)))
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(f"odb_settings_diff: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
