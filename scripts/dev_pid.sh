#!/usr/bin/env bash

# Read the identity of one running process from procfs.
dev_pid_identity() {
  local pid=$1 expected=$2 stat rest exe hash
  local -a fields
  [[ $pid =~ ^[1-9][0-9]*$ ]] || return 1
  [[ -r /proc/$pid/stat && -r /proc/$pid/cmdline ]] || return 1
  IFS= read -r stat < "/proc/$pid/stat" || return 1
  [[ $stat == *') '* ]] || return 1
  rest=${stat##*) }
  # After pid and comm, starttime (field 22) is array element 19.
  read -r -a fields <<< "$rest"
  [[ ${fields[0]:-} != Z && ${fields[19]:-} =~ ^[0-9]+$ ]] || return 1
  exe=$(readlink "/proc/$pid/exe") || return 1
  exe=${exe% (deleted)}
  [[ $exe == "$expected" ]] || return 1
  hash=$(sha256sum "/proc/$pid/cmdline") || return 1
  hash=${hash%% *}
  [[ $hash =~ ^[0-9a-f]{64}$ ]] || return 1
  printf '%s\t%s\t%s\t%s\n' "$pid" "${fields[19]}" "$exe" "$hash"
}

# Detect a live process using the expected executable without trusting its PID file.
dev_pid_same_exe() {
  local pid=$1 expected=$2 canonical exe
  [[ $pid =~ ^[1-9][0-9]*$ ]] || return 1
  canonical=$(readlink -f -- "$expected") || return 1
  exe=$(readlink "/proc/$pid/exe") || return 1
  [[ ${exe% (deleted)} == "$canonical" ]]
}

# Accept a PID file only when the original process still has the same identity.
dev_pid_matches() {
  local file=$1 expected=$2 pid start exe hash extra current canonical
  [[ -f $file ]] || return 1
  IFS=$'\t' read -r pid start exe hash extra < "$file" || return 1
  [[ -z $extra && $pid =~ ^[1-9][0-9]*$ && $start =~ ^[0-9]+$ && $hash =~ ^[0-9a-f]{64}$ ]] || return 1
  canonical=$(readlink -f -- "$expected") || return 1
  [[ $exe == "$canonical" ]] || return 1
  current=$(dev_pid_identity "$pid" "$canonical") || return 1
  [[ $current == "$pid"$'\t'"$start"$'\t'"$exe"$'\t'"$hash" ]]
}

# Save the launched process identity after it has execed its expected binary.
dev_pid_record() {
  local file=$1 pid=$2 expected=$3 canonical identity temp
  canonical=$(readlink -f -- "$expected") || return 1
  identity=$(dev_pid_identity "$pid" "$canonical") || return 1
  temp=$(mktemp "$file.XXXXXXXX") || return 1
  if ! printf '%s\n' "$identity" > "$temp" || ! mv -f -- "$temp" "$file"; then
    rm -f -- "$temp"
    return 1
  fi
}

# Stop only the process whose saved identity still matches at signal time.
dev_pid_stop() {
  local name=$1 file=$2 expected=$3 pid
  if [[ ! -f $file ]]; then
    echo "$name: not running"
    return
  fi
  IFS=$'\t' read -r pid _ < "$file" || pid=unknown
  if ! dev_pid_matches "$file" "$expected"; then
    echo "$name: stale PID file ($pid)"
    if dev_pid_same_exe "$pid" "$expected"; then
      echo "  same executable is running; left untouched"
      return 1
    fi
    rm -f -- "$file"
    return
  fi

  echo "Stopping $name (PID $pid)..."
  if ! dev_pid_matches "$file" "$expected"; then
    rm -f -- "$file"
    echo "  stopped"
    return
  fi
  if ! kill "$pid" 2>/dev/null; then
    if dev_pid_matches "$file" "$expected"; then
      echo "  signal failed; process left running" >&2
      return 1
    fi
    rm -f -- "$file"
    echo "  stopped"
    return
  fi
  for _ in {1..20}; do
    if ! dev_pid_matches "$file" "$expected"; then
      rm -f -- "$file"
      echo "  stopped"
      return
    fi
    sleep 0.25
  done
  if dev_pid_matches "$file" "$expected"; then
    echo "  graceful stop timed out; sending SIGKILL"
    if ! kill -KILL "$pid" 2>/dev/null && dev_pid_matches "$file" "$expected"; then
      echo "  SIGKILL failed; process left running" >&2
      return 1
    fi
  fi
  rm -f -- "$file"
}
