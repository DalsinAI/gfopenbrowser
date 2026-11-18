#!/usr/bin/env python3
# Every place that names the browser's release says the same version, RC
# and date: the readme and both installers; the two programs' $VER strings
# say the same version (their date: see below). RC1 to
# RC2 touched five files; one missed would ship an RC that names the old one.
import os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
def read(*p):
    with open(os.path.join(ROOT, *p), encoding='latin-1') as f:
        return f.read()

passed = failed = 0
def ok(cond, what):
    global passed, failed
    if cond: passed += 1
    else:
        failed += 1
        print('FAIL:', what)

inst = read('dist', 'Install-GFOpenBrowser')
m = re.search(r'\(set #version "(\d+\.\d+\.\d+) RC(\d+) \((\d+)\.(\d+)\.(\d{4})\)"\)', inst)
ok(m is not None, 'Install-GFOpenBrowser sets #version to "X.Y.Z RCn (d.m.yyyy)"')
if m is None:
    print(f'release version: {passed} passed, {failed} failed'); sys.exit(1)
ver, rc, d, mo, y = m.groups()
date = f'{d}.{mo}.{y}'
months = ['January', 'February', 'March', 'April', 'May', 'June', 'July',
          'August', 'September', 'October', 'November', 'December']
long_date = f'{d} {months[int(mo) - 1]} {y}'

ok(inst.startswith(f'; $VER: Install-GFOpenBrowser {ver} ({date})'), f'Install-GFOpenBrowser $VER says {ver} ({date})')
sh = read('dist', 'GFOpenBrowser', 'Install')
ok(sh.startswith(f'; Install: GFOpenBrowser {ver} RC{rc} ({date}),'), f'Install (Shell route) says {ver} RC{rc} ({date})')
rd = read('openbrowser.readme')
ok(re.search(r'^Version:\s+' + re.escape(ver) + r'$', rd, re.M) is not None, f'readme Version: is {ver}')
ok(f'release candidate ({ver} RC{rc}, {long_date})' in rd, f'readme names {ver} RC{rc}, {long_date}')
# The programs' $VER carries the version and the date the programs last changed
# for a release. A release candidate that changes only the package (0.9.0 RC4:
# its programs are the ones the lab pass ran, built before the version text was
# set) keeps their date, so the date may be the release's or an earlier one,
# never a later one.
for src in ('ob_launcher.c', 'ob_browser.c'):
    v = re.search(r'"\$VER: GFOpenBrowser ' + re.escape(ver) + r' \((\d+)\.(\d+)\.(\d{4})\)"', read('src', 'webcore', src))
    ok(v is not None, f'{src} $VER says {ver} (d.m.yyyy)')
    if v is not None:
        pd, pm, py = (int(x) for x in v.groups())
        ok((py, pm, pd) <= (int(y), int(mo), int(d)), f'{src} $VER date {pd}.{pm}.{py} is not after the release date {date}')

print(f'release version: {passed} passed, {failed} failed')
sys.exit(1 if failed else 0)
