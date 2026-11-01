#!/bin/sh
# weblayout.library's SDK files (src/weblayout/library/sdk: FD, clib/,
# inline/, pragmas/, proto/ and the LVO equates) from weblayout_lib.sfd,
# with GFOpenSDK's generator (DalsinAI/openamigasdk OpenNDK/tools/ndkgen.py),
# as GFOpenNDK makes its own. Edit the SFD, then run this.
#
#   OPENSDK_SRC   an openamigasdk checkout (default ../openamigasdk)
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK=${OPENSDK_SRC:-"$HERE/../openamigasdk"}
LIB="$HERE/src/weblayout/library"
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$LIB/sdk" "$LIB/weblayout_lib.sfd"
