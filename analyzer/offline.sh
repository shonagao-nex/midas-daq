#!/usr/bin/env bash

usage() {
  cat <<'EOF'
Usage:
  ./offline.sh RUN_NUMBER

Example:
  ./offline.sh 125

Input:
  data/run00125.mid.lz4

Output:
  rootfiles/run00125.root
EOF
}

if [[ $# -ne 1 || ! $1 =~ ^[0-9]+$ ]]; then
  usage >&2
  exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
ANALYZER_DIR="$SCRIPT_DIR"
run_decimal="${1#"${1%%[!0]*}"}"
run_decimal="${run_decimal:-0}"
if ((${#run_decimal} > 18)); then
  echo "ERROR: run number is too large: $1" >&2
  exit 2
fi
printf -v run_number '%05d' "$run_decimal"

input="$ANALYZER_DIR/data/run${run_number}.mid.lz4"
output="$ANALYZER_DIR/rootfiles/run${run_number}.root"
analyzer="$ANALYZER_DIR/bin/midas_analyzer"

if [[ ! -f "$input" ]]; then
  echo "ERROR: input MIDAS file does not exist: $input" >&2
  exit 1
fi
if [[ ! -x "$analyzer" ]]; then
  echo "ERROR: analyzer executable does not exist or is not executable: $analyzer" >&2
  exit 1
fi
if [[ ! -d "$ANALYZER_DIR/rootfiles" ]]; then
  echo "ERROR: rootfiles directory does not exist: $ANALYZER_DIR/rootfiles" >&2
  exit 1
fi
if [[ -e "$output" || -L "$output" ]]; then
  echo "ERROR: output ROOT file already exists; move it aside before decoding: $output" >&2
  exit 1
fi
if [[ ! -w "$ANALYZER_DIR/rootfiles" ]]; then
  echo "ERROR: rootfiles directory is not writable: $ANALYZER_DIR/rootfiles" >&2
  exit 1
fi

echo "Input:  $input"
echo "Output: $output"
"$analyzer" -f "$input" -w "$output"
exit $?
