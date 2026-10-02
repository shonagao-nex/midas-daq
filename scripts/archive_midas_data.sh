#!/bin/bash
set -u

SRC="/home/daq/midas/midas/data"
DST="/mnt/daq-archive/midas"
LOCK="/tmp/midas-archive.lock"

exec 9>"${LOCK}"
flock -n 9 || exit 0

if ! mountpoint -q /mnt/daq-archive; then
    echo "$(date '+%F %T') ERROR: /mnt/daq-archive is not mounted"
    exit 1
fi

mkdir -p "${DST}"

shopt -s nullglob

# run番号を抽出
runs=()
for f in "${SRC}"/run*.mid*; do
    base=$(basename "${f}")
    run=$(echo "${base}" | sed -n 's/^\(run[0-9]\+\)\.mid.*/\1/p')
    [ -n "${run}" ] && runs+=("${run}")
done

# 重複run番号を除去
mapfile -t runs < <(printf '%s\n' "${runs[@]}" | sort -u)

for run in "${runs[@]}"; do
    files=("${SRC}/${run}.mid"*)

    # このrunに属するファイルのどれかが使用中ならrun全体をスキップ
    busy=0
    for src in "${files[@]}"; do
        if fuser "${src}" >/dev/null 2>&1; then
            busy=1
            break
        fi
    done

    if [ "${busy}" -eq 1 ]; then
        continue
    fi

    for src in "${files[@]}"; do
        file=$(basename "${src}")
        dst="${DST}/${file}"
        tmp="${DST}/.${file}.partial"

        if [ -f "${dst}" ]; then
            continue
        fi

        echo "$(date '+%F %T') Archiving ${file}"

        rm -f "${tmp}"

        if rsync -a -- "${src}" "${tmp}"; then
            src_size=$(stat -c '%s' "${src}")
            dst_size=$(stat -c '%s' "${tmp}")

            if [ "${src_size}" -eq "${dst_size}" ]; then
                mv "${tmp}" "${dst}"
                echo "$(date '+%F %T') Archived ${file} (${src_size} bytes)"
            else
                echo "$(date '+%F %T') ERROR: size mismatch for ${file}"
                rm -f "${tmp}"
            fi
        else
            echo "$(date '+%F %T') ERROR: rsync failed for ${file}"
            rm -f "${tmp}"
        fi
    done
done
