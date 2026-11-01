#!/usr/bin/env python3
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE at the repository root).
"""openjson_lib.c's vector table against sfd/json_lib.sfd: the same calls in
the same order, and each slot's LVO comment right. The SDK headers are made
from the SFD by GFOpenNDK's ndkgen.py (scripts/gen-jsonlib-sdk.sh)."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    sfd, bias = [], 30
    for line in open(os.path.join(HERE, 'sfd', 'json_lib.sfd')):
        line = line.strip()
        if line.startswith('==bias'):
            bias = int(line.split()[1])
        if not line or line[0] in '*=':
            continue
        m = re.match(r'.*?\b(\w+)\(.*\)\s*\([^)]*\)$', line)
        if not m:
            sys.exit('cannot read the SFD line: ' + line)
        sfd.append((m.group(1), bias))
        bias += 6
    src = open(os.path.join(HERE, 'openjson_lib.c')).read()
    table = src[src.index('lib_vectors[] = {'):]
    table = table[:table.index('};')]
    shell = [(n, int(o)) for n, o in re.findall(r'\(APTR\)L_(\w+),\s*/\*\s*-\s*(\d+)\s*\*/', table)]
    bad = False
    for i in range(max(len(sfd), len(shell))):
        a = sfd[i] if i < len(sfd) else ('(none)', 30 + 6 * i)
        b = shell[i] if i < len(shell) else ('(none)', 0)
        if a[0] != b[0] or a[1] != b[1]:
            print('slot -%d: the SFD has %s, openjson_lib.c has %s at -%d' % (a[1], a[0], b[0], b[1]))
            bad = True
    if bad:
        sys.exit(1)
    print('%d calls, -30 to -%d: the vector table follows the SFD' % (len(sfd), sfd[-1][1]))


if __name__ == '__main__':
    main()
