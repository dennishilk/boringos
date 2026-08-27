#!/bin/sh
set -eu
case "${BORING_QMP_TRANSPORT:-unix}" in
    unix) printf 'unix:%s,server=on,wait=off\n' "$1" ;;
    pipe)
        mkfifo "$1.in" "$1.out"
        printf 'pipe:%s\n' "$1"
        ;;
    *) printf '%s\n' 'BORING_QMP_TRANSPORT must be unix or pipe' >&2; exit 2 ;;
esac
