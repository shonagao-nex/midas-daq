#!/bin/sh
set -eu

MIDAS_BASE=$HOME/midas
MIDASSYS=$MIDAS_BASE/midas_src
MIDAS_RUNTIME=$MIDAS_BASE/midas
MIDAS_EXPTAB=$MIDAS_RUNTIME/exptab
MIDAS_EXPT_NAME=${MIDAS_EXPT_NAME:-daq}
export MIDASSYS MIDAS_EXPTAB MIDAS_EXPT_NAME
unset MIDAS_SERVER_HOST
unset MIDAS_SERVER_PORT
unset MIDAS_DIR

frontend=$MIDAS_RUNTIME/online/vme_frontend/bin/fevme
if [ ! -x "$frontend" ]; then
  echo "fevme executable is missing or not executable: $frontend" >&2
  exit 1
fi

exec "$frontend" -e "$MIDAS_EXPT_NAME"
