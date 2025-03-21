#!/bin/bash

file="${1}"
output_dir="${2}"

usage() {
    printf "Usage: %s FILE OUTPUT_DIR\n" "$(basename "$0")"
    exit 1
}

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters $#, should be 1"
    usage
fi

[ -z "${1}" ] && usage

mkdir -p "${output_dir}"

# Get the unique keys, excluding the first line
uniques=$(tail -n +2 "${file}" |cut -d',' -f2 | sort -u)

# Do _EVERY after or we get a mix of _EVERY and non _EVERY
echo "${uniques}" | grep -v _EVERY | while read -r key; do
    grep -v _EVERY "${file}" | grep "${key}" > "${output_dir}/${key}.csv"
done

echo "${uniques}" | sort -u  | grep _EVERY | while read -r key; do
    grep "${key}" "${file}" > "${output_dir}/${key}.csv"
done

echo "Files created in ${output_dir}"
