#!/bin/sh
set -eu

root=/home/daq/midas/midas/online
programs="$root/scripts/frontend_programs.odb"

check_line() {
  file=$1
  expected=$2
  if ! grep -Fqx "$expected" "$file"; then
    echo "missing expected line in $file: $expected" >&2
    exit 1
  fi
}

for wrapper in "$root/scripts/start_fevme.sh" \
               "$root/scripts/start_feeasiroc.sh"; do
  sh -n "$wrapper"
  check_line "$wrapper" "export MIDASSYS=/home/daq/midas/midas_src"
  check_line "$wrapper" "export MIDAS_EXPTAB=/home/daq/midas/midas/exptab"
  check_line "$wrapper" "export MIDAS_EXPT_NAME=daq"
  check_line "$wrapper" "unset MIDAS_SERVER_HOST"
  check_line "$wrapper" "unset MIDAS_SERVER_PORT"
  check_line "$wrapper" "unset MIDAS_DIR"
done

check_line "$programs" "[/Programs/fevme]"
check_line "$programs" "[/Programs/feeasiroc]"
check_line "$programs" "Start command = STRING : [256] $root/scripts/start_fevme.sh"
check_line "$programs" "Start command = STRING : [256] $root/scripts/start_feeasiroc.sh"

required_count=$(grep -Fxc 'Required = BOOL : y' "$programs")
auto_start_count=$(grep -Fxc 'Auto start = BOOL : n' "$programs")
auto_stop_count=$(grep -Fxc 'Auto stop = BOOL : n' "$programs")
auto_restart_count=$(grep -Fxc 'Auto restart = BOOL : n' "$programs")
if [ "$required_count" -ne 2 ] || [ "$auto_start_count" -ne 2 ] || \
   [ "$auto_stop_count" -ne 2 ] || [ "$auto_restart_count" -ne 2 ]; then
  echo "unexpected Required/Auto flags in $programs" >&2
  exit 1
fi

if grep -Fq '/Experiment/Prevent start on required progs' "$programs"; then
  echo "frontend Programs definitions must not change Prevent start on required progs" >&2
  exit 1
fi

check_line "$root/vme_frontend/fevme.cxx" \
  'const char *frontend_name = "fevme";            // MIDAS frontend/client name'
check_line "$root/easiroc_frontend/feeasiroc.cxx" \
  'const char* frontend_name = "feeasiroc";'
check_line "$root/daq_monitor/daq_monitor.cxx" \
  'constexpr char kVmeClientName[] = "fevme";'
check_line "$root/daq_monitor/daq_monitor.cxx" \
  'constexpr char kEasirocClientName[] = "feeasiroc";'

sh -n "$root/scripts/install_frontend_programs.sh"
echo "frontend Programs static checks passed"
