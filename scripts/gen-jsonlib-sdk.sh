#!/bin/sh
# json.library's SDK files (src/jsonlib/sdk: FD, clib/, inline/, pragmas/,
# proto/ and the LVO equates) from src/jsonlib/sfd/json_lib.sfd, with
# GFOpenSDK's generator (DalsinAI/gfopensdk OpenNDK/tools/ndkgen.py), as
# GFOpenNDK makes its own. sdk/Include_H/libraries/json.h is written by hand.
# Edit the SFD (new calls at the end), then run this.
#
#   OPENSDK_SRC   a gfopensdk checkout (default ../gfopensdk)
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK=${OPENSDK_SRC:-"$HERE/../gfopensdk"}
LIB="$HERE/src/jsonlib"
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$LIB/sdk" "$LIB/sfd/json_lib.sfd"
python3 "$LIB/check_vectors.py"
