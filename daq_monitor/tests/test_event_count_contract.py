"""Check the final Events sent source and the Run Status display contract."""

from pathlib import Path


monitor = Path("daq_monitor.cxx").read_text()
dashboard = Path("web/daq.js").read_text()
runlog_config = Path("../scripts/configure_dev_runlog.py").read_text()

capture = monitor.split("INT capture_runlog_eor(INT run_number, char*) {", 1)[1].split(
    "void maybe_spawn_run_elog(", 1
)[0]
assert "cm_register_transition(TR_STOP, capture_runlog_eor, 700)" in monitor
assert "ObservedVMEEvents" not in monitor
assert "ObservedEASIROCEvents" not in monitor
assert '"/Equipment/VME/Statistics/Events sent"' in capture
assert '"/Equipment/NIM-EASIROC Physics/Statistics/Events sent"' in capture
assert "if (participation.vme)" in capture
assert "if (participation.easiroc)" in capture
assert "!client_health(kVmeClientName).connected" in capture
assert "!client_health(kEasirocClientName).connected" in capture
assert "!client_stop_complete(kVmeClientName)" in capture
assert "!client_stop_complete(kEasirocClientName)" in capture
assert "if (!counts_ok" in capture
assert capture.index('"/DAQ/Status/Runlog/VMEEvents"') < capture.index(
    '"/DAQ/Status/Runlog/EORCompleteRunNumber"'
)
assert capture.index('"/DAQ/Status/Runlog/EASIROCEvents"') < capture.index(
    '"/DAQ/Status/Runlog/EORCompleteRunNumber"'
)

display = dashboard.split("function lastCompletedRun(v)", 1)[1].split(
    "function settleTransition()", 1
)[0]
assert "final<=current" in display  # A failed START keeps the last EOR run.
assert 'run==="STOPPED"' in display
assert "runEventCount(v(finalKey))" in display
assert "runEventCount(v(key))" in display
assert '"VME events", f"{root}/VMEEvents"' in runlog_config
assert '"EASIROC events", f"{root}/EASIROCEvents"' in runlog_config

print("test_event_count_contract: passed")
