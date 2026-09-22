#!/bin/sh
set -eu

export MIDASSYS=/home/daq/midas/midas_src
export MIDAS_EXPTAB=/home/daq/midas/midas/exptab
export MIDAS_EXPT_NAME=daq
unset MIDAS_SERVER_HOST
unset MIDAS_SERVER_PORT
unset MIDAS_DIR

exec /home/daq/midas/midas_src/bin/odbedit -e daq \
  -c "load /home/daq/midas/midas/online/scripts/frontend_programs.odb"
