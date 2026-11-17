#!/usr/bin/env python3
# Every place that names the browser's release says the same version, RC
# and date: the readme, both installers and the two $VER strings. RC1 to
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
for src in ('ob_launcher.c', 'ob_browser.c'):
    ok(f'"$VER: GFOpenBrowser {ver} ({date})"' in read('src', 'webcore', src), f'{src} $VER says {ver} ({date})')

print(f'release version: {passed} passed, {failed} failed')
sys.exit(1 if failed else 0)
