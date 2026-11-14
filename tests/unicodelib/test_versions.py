#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# unicode.library's version has to move with its calls, and both installers
# have to replace an older one: a program built with more calls than the
# library in LIBS: gets no table and stops (the 0.9.0 pack build stopped at
# "Loading the browser engine" with RC1's unicode.library 1.0, kept by
# Install as "there"). Checks, with no Amiga:
#  - calls.txt's line count is the last line of versions.txt;
#  - unicode_lib.c's LIB_VERSION.LIB_REVISION and both $VER strings are it;
#  - ul_client.h's UL_LIB_VERSION/REVISION are it;
#  - the Shell Install asks Version for it from the file (FILE, so an old one
#    is not loaded), renames an older one and flushes memory after the copy;
#  - the Installer compares getversion with it and flushes after the copy.
#   python3 tests/unicodelib/test_versions.py [REPO]
import os, re, sys

R = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
U = os.path.join(R, 'src', 'unicodelib')
passed = failed = 0


def ok(cond, what):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print('FAIL:', what)


def read(*p):
    with open(os.path.join(R, *p)) as f:
        return f.read()


calls = [l for l in read('src', 'unicodelib', 'calls.txt').splitlines() if l.strip() and not l.lstrip().startswith('#')]
rows = [l.split() for l in read('src', 'unicodelib', 'versions.txt').splitlines() if l.strip() and not l.startswith('#')]
ver, count = rows[-1][0], rows[-1][1]
major, minor = (int(x) for x in ver.split('.'))
ok(count.isdigit() and int(count) == len(calls),
   f'versions.txt ends with {ver} {count}, calls.txt has {len(calls)} calls: add a line with a new revision')
ok(len(set(r[0] for r in rows)) == len(rows), 'each version once in versions.txt')

lib = read('src', 'unicodelib', 'unicode_lib.c')
m1 = re.search(r'#define LIB_VERSION\s+(\d+)', lib)
m2 = re.search(r'#define LIB_REVISION\s+(\d+)', lib)
ok(m1 and m2 and (int(m1.group(1)), int(m2.group(1))) == (major, minor), f'unicode_lib.c is {ver}')
ok(len(re.findall(r'unicode\.library ' + re.escape(ver) + r' \(\d+\.\d+\.\d{4}\)', lib)) == 2, f"both of unicode_lib.c's id strings say {ver}")

cl = read('src', 'unicodelib', 'ul_client.h')
ok(f'#define UL_LIB_VERSION  {major}' in cl and f'#define UL_LIB_REVISION {minor}' in cl and f'"{ver}"' in cl,
   f'ul_client.h asks for {ver}')

sh = read('dist', 'GFOpenBrowser', 'Install')
ok(f'Version LIBS:unicode.library {major} {minor} FILE >NIL:' in sh, f'Install asks Version for the LIBS: file of unicode.library {ver}')
ok(not re.search(r'^Version unicode\.library \d+ \d+ >NIL:\n(?:.*\n){0,3}?If \$vrc', sh, re.M),
   'Install does not open unicode.library to read its version (it would load an old one)')
ok(re.search(r'Installed LIBS:unicode\.library"\n(?:;.*\n)*Avail >NIL: FLUSH\n', sh) is not None, 'Install flushes an old unicode.library out of memory after the copy')
ok('Rename LIBS:unicode.library LIBS:unicode.library.before-' in sh, 'Install renames an older LIBS:unicode.library')
ok('If NOT EXISTS LIBS:unicode.library\n    Copy' not in sh, 'Install no longer copies only when LIBS: has none')

inst = read('dist', 'Install-GFOpenBrowser')
ok(f'(getversion "LIBS:unicode.library") {major * 65536 + minor})' in inst, f'Install-GFOpenBrowser compares getversion with {ver}')
ok('(rename "LIBS:unicode.library" "LIBS:unicode.library.before-' in inst, 'Install-GFOpenBrowser renames an older one')
ok('(run "Avail >NIL: FLUSH")' in inst, 'Install-GFOpenBrowser flushes an old one out of memory after the copy')

print(f'unicodelib versions: {passed} passed, {failed} failed')
sys.exit(1 if failed else 0)
