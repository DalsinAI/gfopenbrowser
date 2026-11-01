#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
#
# src/unicodelib/sdk from sfd/unicode_lib.sfd with GFOpenNDK's ndkgen.py
# (libraries/unicode.h is written by hand). OPENSDK_SRC: an openamigasdk
# checkout (default: beside this repository).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK=${OPENSDK_SRC:-"$HERE/../openamigasdk"}
LIB="$HERE/src/unicodelib"
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$LIB/sdk" "$LIB/sfd/unicode_lib.sfd"
