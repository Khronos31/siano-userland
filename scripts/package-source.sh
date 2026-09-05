#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_dir/package-source.py" "$@"
