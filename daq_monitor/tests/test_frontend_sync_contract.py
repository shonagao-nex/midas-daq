"""Check that DAQ frontend controls use the MIDAS Programs process API."""

from pathlib import Path


dashboard = Path("web/daq.js").read_text()
monitor = Path("daq_monitor.cxx").read_text()
vme = Path("../vme_frontend/fevme.cxx").read_text()
easiroc = Path("../easiroc_frontend/easiroc_odb.cxx").read_text()

assert 'client:"fevme"' in dashboard
assert 'client:"feeasiroc"' in dashboard
assert "mjsonrpc_start_program(frontend.client)" in dashboard
assert "mjsonrpc_stop_program(frontend.client)" in dashboard
assert "mjsonrpc_cm_exist(client,true)" in dashboard
assert "lastControlRunState!==\"STOPPED\"" in dashboard
assert "const frontendToggleLocked=lastControlRunState===\"RUNNING\"||lastControlRunState===\"PAUSED\"" in dashboard
assert "renderToggle(frontend,v(frontend.connected),frontendToggleLocked)" in dashboard
assert "/Equipment/VME/Settings/FrontendEnabled" not in dashboard
assert "/Equipment/EASIROC/Settings/FrontendEnabled" not in dashboard
assert "/Equipment/VME/Settings/FrontendEnabled" not in monitor
assert "/Equipment/EASIROC/Settings/FrontendEnabled" not in monitor
assert "cm_exist(client_name, TRUE)" in monitor
assert "reset_nonparticipating_run_snapshots" in monitor
assert "/Equipment/VME/RunSnapshot/Metadata/EnabledForRun" in monitor
assert "/Equipment/EASIROC/RunSnapshot/Metadata/EnabledForRun" in monitor
assert "FRONTEND_ENABLED_PATH" not in vme
assert '"FrontendEnabled"' not in easiroc

print("test_frontend_sync_contract: passed")
