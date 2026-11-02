#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# ICU 78.3 for AmigaOS 3.x with the os32-gcc16 stove: static libraries with the
# data linked in, and the same data as a file (share/icudt78b.dat).
#
#   ICU_SRC         extracted icu4c-78.3 sources ("icu" folder), with
#                   icu/patches/0001-icu78-amigaos3.patch applied
#   ICU_HOST_BUILD  a host (Linux) ICU 78.3 build folder for cross builds,
#                   the one holding config/icucross.mk and bin/icupkg
#   OS32_GCC16      stove root (prefix/, compat/)
#   OAB_DEPS        output root; installs into $OAB_DEPS/icu78-m68k-amigaos
#   ICU_DATA_KEEP   list of the data items to keep (default icu/data-keep.lst:
#                   the root and English locales; "all" keeps everything)
#   JOBS            parallel jobs (default 2)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
: "${ICU_SRC:?set ICU_SRC to the patched icu folder}"
: "${ICU_HOST_BUILD:?set ICU_HOST_BUILD to a host ICU 78.3 build}"
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P=$S/prefix
OUT=${OAB_DEPS:-"$HOME/openbrowser-deps"}
JOBS=${JOBS:-2}
WORK=$OUT/icu-build
F="-O2 -fno-delete-null-pointer-checks $(echo "${OS32_CPU_FLAGS:--m68040 -m68881}" | sed "s/-mcrt=[a-z0-9]*//")"

rm -rf "$WORK" "$OUT/icu78-m68k-amigaos"
mkdir -p "$WORK"
cd "$WORK"
CC=$P/bin/m68k-amigaos-gcc CXX=$P/bin/m68k-amigaos-g++ AR=$P/bin/m68k-amigaos-ar RANLIB=$P/bin/m68k-amigaos-ranlib \
CPPFLAGS="-DU_TIMEZONE=_timezone -DU_ENABLE_DYLOAD=0 -DHAVE_DLOPEN=0" CFLAGS="$F" CXXFLAGS="$F" LDFLAGS="$F" \
LIBS="-lpthread -lstdc++ -L$S/compat -los32compat" \
"$ICU_SRC/source/configure" --host=m68k-amigaos --build=x86_64-pc-linux-gnu --with-cross-build="$ICU_HOST_BUILD" \
  --enable-static --disable-shared --disable-tools --disable-tests --disable-samples --disable-extras \
  --disable-icuio --disable-layoutex --with-data-packaging=static --prefix="$OUT/icu78-m68k-amigaos" > configure.log 2>&1

# The libraries build first; data packaging then fails (pkgdata wants ELF objects).
gmake -j"$JOBS" > build.log 2>&1 || true
sed -i 's/^GENCCODE_ASSEMBLY_TYPE=$/GENCCODE_ASSEMBLY_TYPE=-a gcc-cygwin/' data/icupkg.inc
sed -i 's/^GENCCODE_ASSEMBLY = $/GENCCODE_ASSEMBLY = -a gcc-cygwin/' icudefs.mk
gmake -j"$JOBS" >> build.log 2>&1
gmake install > install.log 2>&1

# genccode writes .long words in host (little-endian) order, which swaps them
# on m68k; include the big-endian .dat byte for byte instead.
D=$WORK/icudata-incbin
mkdir -p "$D"
cp data/out/icudt78b.dat "$D/"
# Keep only the data GFOpenBrowser uses: every program carries it, and loading
# it is part of each start.
KEEP=${ICU_DATA_KEEP:-"$ROOT/icu/data-keep.lst"}
if [ "$KEEP" != all ]; then
    rm -rf "$WORK/icudata-items" && mkdir -p "$WORK/icudata-items"
    LD_LIBRARY_PATH="$ICU_HOST_BUILD/lib" "$ICU_HOST_BUILD/bin/icupkg" -x '*' -d "$WORK/icudata-items" data/out/icudt78b.dat
    rm -f "$D/icudt78b.dat"
    LD_LIBRARY_PATH="$ICU_HOST_BUILD/lib" "$ICU_HOST_BUILD/bin/icupkg" --ignore-deps -tb -s "$WORK/icudata-items" -a "$KEEP" new "$D/icudt78b.dat"
fi
cat > "$D/icudt78_dat.s" <<'S'
	.globl	_icudt78_dat
	.data
	.balign	8
_icudt78_dat:
	.incbin	"icudt78b.dat"
S
(cd "$D" && "$P/bin/m68k-amigaos-as" -m68040 icudt78_dat.s -o icudt78_dat.o && rm -f libicudata.a && "$P/bin/m68k-amigaos-ar" rcs libicudata.a icudt78_dat.o)
cp "$D/libicudata.a" "$OUT/icu78-m68k-amigaos/lib/libicudata.a"
# The same data as a file, for programs that read it at start
# (scripts/build-webcore.sh, OB_ICU_DATA=file).
mkdir -p "$OUT/icu78-m68k-amigaos/share"
cp "$D/icudt78b.dat" "$OUT/icu78-m68k-amigaos/share/icudt78b.dat"
echo "ICU_OK $OUT/icu78-m68k-amigaos"
