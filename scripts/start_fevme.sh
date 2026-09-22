#!/bin/sh
set -eu

export MIDASSYS=/home/daq/midas/midas_src
export MIDAS_EXPTAB=/home/daq/midas/midas/exptab
export MIDAS_EXPT_NAME=daq
unset MIDAS_SERVER_HOST
unset MIDAS_SERVER_PORT
unset MIDAS_DIR

frontend=/home/daq/midas/midas/online/vme_frontend/bin/fevme
if [ ! -x "$frontend" ]; then
  echo "fevme executable is missing or not executable: $frontend" >&2
  exit 1
fi

exec "$frontend" -e daq
