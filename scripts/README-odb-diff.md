# ODB configuration comparison

`odb_settings_diff.py` reports differences between development (`/home/nagao`,
`daq-dev`, port 8181) and production (`/home/daq`, `daq`, port 8081). Run the
live comparison **only from the actual DAQ host shell**, for example as `nagao`:

```sh
cd /home/nagao/midas/midas/online
python3 scripts/odb_settings_diff.py
```

Before copying settings, the tool reads `/Experiment/Name` on both ports with
`db_get_values` and requires `daq-dev` on 8181 and `daq` on 8081. A wrong name,
missing key, or connection failure stops the comparison with an endpoint-specific
error. It then calls only MIDAS `db_copy` and `db_key` through HTTP JSON-RPC.
It never writes ODB, starts clients, or fixes differences. The Python process
does not attach to the shared-memory ODB; the already-running mhttpd serves the
requests. The hostname check does not prove PID/mount namespace identity, so
the actual-host-shell requirement still applies. It prints one
record per differing key, with its absolute path, `dev` and `prod` values, MIDAS
types, and array lengths. Uniform arrays are shortened, for example
`[512 × 128]` means 128 elements all equal to 512. For equal-size arrays with
nonuniform changes, the output gives differing indices and their dev/prod
values. It shows at most 12 indices and reports how many more differ. Indexes
are zero based. `VALUE`, `TYPE`, `LENGTH`, and `MISSING` are separate
labels; more than one label may apply to a key. A difference exits with status
0 because different values can be intentional. An unreadable endpoint, bad
snapshot, or malformed metadata exits with status 2.

The comparison covers leaves below `/Equipment/*/Settings` and `/Analyzer`.
`/Equipment/test_bulk` and `/Equipment/test_rpc` are excluded by default.
`Status`, `Variables`, `Commands`, `RunSnapshot`, `Runinfo`, counter keys, and
`/Analyzer/HistogramPdf` runtime requests are excluded. The tool does not
judge whether a value is safe for hardware; each frontend or analyzer must
validate its own settings when used. Inspect any `TYPE`, `LENGTH`, or `MISSING`
difference before deployment, and review `VALUE` differences against the run
plan.

For an offline comparison, pass two saved JSON snapshot files in the script's
format (`/Equipment` and `/Analyzer` objects, each containing `data` from
`db_copy` and `key` from `db_key`):

```sh
python3 scripts/odb_settings_diff.py \
  --dev-snapshot /path/to/dev.json --prod-snapshot /path/to/prod.json
```

Offline mode never connects to MIDAS and can run in the development sandbox.
Do not use an old snapshot as evidence of the current production ODB. The
RPV130 `SingleEventBusyEnabled` comparison is covered by
`scripts/tests/test_odb_settings_diff.py`; that test uses fabricated values.
To explain its presence or absence in a *current* diff, read
`/Equipment/VME/Settings/RPV130/SingleEventBusyEnabled` from both ports on the
actual DAQ host shell. A key present on both sides with equal type, length,
and value is omitted.

Run the standalone test with:

```sh
python3 -m unittest scripts.tests.test_odb_settings_diff -v
```

The deploy schema scope is documented separately in
[`deploy/README.md`](deploy/README.md). Deploy validates the VME/EASIROC
Settings and existing Analyzer schema without syncing values or repairing
schema. This comparison tool reports value differences; it is not a deploy
schema check.
