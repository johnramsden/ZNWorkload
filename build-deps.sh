#!/bin/bash

SCRIPT_DIR="$(dirname -- "$( readlink -f -- "$0"; )")"

libzbd_dir="vendor/libzbd"

declare -a submodules=(
    "${libzbd_dir}"
)

check_submodules() {
    ret=0
    for sm in "${submodules[@]}"; do
      if ! [ -d "${SCRIPT_DIR}/${sm}" ]; then
          echo "Missing submodule: '${libzbd_dir}'" 1>&2
          ret=1
      fi
    done
    if [ "${ret}" -ne 0 ]; then
        echo "Please pull submodules and re-run:" 1>&2
        echo "$ git submodule update --init"
    fi
    return "${ret}"
}

libzbd() {
    echo "Building libzbd, installing to 'vendor/lib'"
    cd "${SCRIPT_DIR}/${libzbd_dir}" || exit 1
    ./autogen.sh
    ./configure --prefix="${SCRIPT_DIR}/vendor/lib"
    make
    make install

    cd "${SCRIPT_DIR}" || exit 1
}

check_submodules || exit 1
libzbd