#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Host test of scripts/jsimage.py's demangle(): Amiga names carry one more
# leading underscore, and the target's c++filt takes one off by default.
# Two fake c++filts (one that strips by default, one that does not) must
# both give the same demangled names, which is what lets a cut long name be
# told apart by the -Map's symbol line.
#   python3 tests/jsimage/test_demangle.py
import os, stat, sys, tempfile
HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(HERE, 'scripts'))
import jsimage

# A tiny "demangler": _ZN3JSC2VMC1Ev -> JSC::VM::VM(); anything else as is.
FAKE = r'''#!/usr/bin/env python3
import sys
strip = %s and '-n' not in sys.argv[1:]
table = {'_ZN3JSC2VMC1Ev': 'JSC::VM::VM()', '_ZN3JSC2VMD1Ev': 'JSC::VM::~VM()'}
for line in sys.stdin:
    n = line.rstrip('\n')
    if strip and n.startswith('_'):
        n = n[1:]
    print(table.get(n, line.rstrip('\n')))
'''

passed = failed = 0
def ok(cond, what):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print('FAIL:', what)

with tempfile.TemporaryDirectory() as t:
    for strips in (True, False):
        d = os.path.join(t, 'strip' if strips else 'plain')
        os.mkdir(d)
        f = os.path.join(d, 'm68k-amigaos-c++filt')
        with open(f, 'w') as fh:
            fh.write(FAKE % strips)
        os.chmod(f, os.stat(f).st_mode | stat.S_IXUSR)
        jsimage.NM = os.path.join(d, 'm68k-amigaos-nm')
        got = jsimage.demangle(['__ZN3JSC2VMC1Ev', '__ZN3JSC2VMD1Ev'])
        kind = 'a c++filt that strips' if strips else 'a c++filt that does not strip'
        ok(got.get('__ZN3JSC2VMC1Ev') == 'JSC::VM::VM()', f'constructor demangled with {kind}')
        ok(got.get('__ZN3JSC2VMD1Ev') == 'JSC::VM::~VM()', f'destructor demangled with {kind}')
    ok(jsimage.demangle([]) == {}, 'no names, no run')
    bad = os.path.join(t, 'bad'); os.mkdir(bad)
    f = os.path.join(bad, 'm68k-amigaos-c++filt')
    with open(f, 'w') as fh:
        fh.write('#!/bin/sh\nhead -1\n')
    os.chmod(f, 0o755)
    jsimage.NM = os.path.join(bad, 'm68k-amigaos-nm')
    try:
        jsimage.demangle(['__ZN3JSC2VMC1Ev', '__ZN3JSC2VMD1Ev'])
        ok(False, 'a c++filt that prints fewer lines stops jsimage')
    except SystemExit as e:
        ok('2 names' in str(e), 'a c++filt that prints fewer lines stops jsimage')

    jsimage.NM = os.path.join(t, 'none', 'm68k-amigaos-nm')
    try:
        jsimage.demangle(['__ZN3JSC2VMC1Ev'])
        ok(False, 'no c++filt stops jsimage with a message')
    except SystemExit as e:
        ok('cannot run' in str(e) and 'c++filt' in str(e), 'no c++filt stops jsimage with a message, not a traceback')

print(f'jsimage demangle: {passed} passed, {failed} failed')
sys.exit(1 if failed else 0)
