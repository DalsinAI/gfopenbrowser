#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
"""json.library's core against references, for tests/jsonlib/run.sh.

  json_ref.py gen DIR        writes DIR/script.txt and the files it names
  json_ref.py check DIR OUT  checks json_test's answers (OUT) to that script

The references: hand-written cases with their exact answers (error code
and position included); random documents whose compact form is known as
they are made; random damage to those, judged by Python's json module
(strict, no NaN or Infinity); number texts, judged by Python's correctly
rounded float(); doubles to text, judged by Node's String(x) (ECMAScript
Number::toString), or, without Node, by Python's repr() digits.
Every text is also read by the streaming reader, whole and cut into
random pieces, which must answer exactly as the parser does.
"""
import decimal
import json
import os
import random
import shutil
import struct
import subprocess
import sys

COMMENTS, TRAILING, LATIN1, MULTI = 1, 2, 4, 8
E = dict(OK=0, NOMEM=1, SYNTAX=2, EOF=3, CONTROL=4, ESCAPE=5, UTF8=6, NUMBER=7, DEPTH=8,
         TOOBIG=9, TRAILING=10)

# (text, flags, expected): expected is the compact text, or (code, offset)
HAND = [
    (b'0', 0, '0'), (b'-0', 0, '-0'), (b' 1 ', 0, '1'), (b'-1', 0, '-1'),
    (b'2147483647', 0, '2147483647'), (b'-2147483648', 0, '-2147483648'),
    (b'2147483648', 0, '2147483648'), (b'-2147483649', 0, '-2147483649'),
    (b'1.0', 0, '1.0'), (b'1E5', 0, '1E5'), (b'1e+5', 0, '1e+5'), (b'0.5e-3', 0, '0.5e-3'),
    (b'"\\u00e9\\uD83D\\uDE00"', 0, '"é\U0001F600"'),
    (b'"\\uD83D"', 0, '"�"'), (b'"\\uDE00x"', 0, '"�x"'),
    (b'"\\uD83D\\u0041"', 0, '"�A"'), (b'"\\uD83D\\n"', 0, '"�\\n"'),
    (b'"\\uD83D\\uD83D\\uDE00"', 0, '"�\U0001F600"'),
    (b'"\\u0000"', 0, '"\\u0000"'), (b'"\\/\\b\\f\\n\\r\\t\\"\\\\"', 0, '"/\\b\\f\\n\\r\\t\\"\\\\"'),
    (b'"\x7f"', 0, '"\x7f"'), (b'"\xe2\x80\xa8"', 0, '" "'),
    (b'\xef\xbb\xbf[]', 0, '[]'), (b'[]', 0, '[]'), (b'{}', 0, '{}'), (b'[[]]', 0, '[[]]'),
    (b'{"a":{"b":[]}}', 0, '{"a":{"b":[]}}'), (b'{"a":1,"a":2}', 0, '{"a":1,"a":2}'),
    (b' \t\r\n[ \t\r\n1 \t\r\n, \t\r\n2 \t\r\n] \t\r\n', 0, '[1,2]'),
    (b'"\xe9"', LATIN1, '"é"'), (b'"\xff\\u00ff"', LATIN1, '"ÿÿ"'),
    (b'[1,]', TRAILING, '[1]'), (b'{"a":1,}', TRAILING, '{"a":1}'), (b'[1,,]', TRAILING, (E['SYNTAX'], 3)),
    (b'[,]', TRAILING, (E['SYNTAX'], 1)), (b'{,}', TRAILING, (E['SYNTAX'], 1)),
    (b'// x\n[1, /* y */ 2] // z', COMMENTS, '[1,2]'), (b'/**/1/***/', COMMENTS, '1'),
    (b'[1 /* x', COMMENTS, (E['EOF'], 7)), (b'1 /', COMMENTS, (E['EOF'], 3)),
    (b'1 /x', COMMENTS, (E['SYNTAX'], 3)), (b'// x', COMMENTS, (E['EOF'], 4)),
    (b'', 0, (E['EOF'], 0)), (b'   ', 0, (E['EOF'], 3)), (b'[', 0, (E['EOF'], 1)),
    (b'[1', 0, (E['EOF'], 2)), (b'[1,', 0, (E['EOF'], 3)), (b'{"a"', 0, (E['EOF'], 4)),
    (b'{"a":', 0, (E['EOF'], 5)), (b'"abc', 0, (E['EOF'], 4)), (b'"\\', 0, (E['EOF'], 2)),
    (b'"\\u12', 0, (E['EOF'], 5)), (b'tru', 0, (E['EOF'], 3)), (b'-', 0, (E['EOF'], 1)),
    (b'1.', 0, (E['EOF'], 2)), (b'1e', 0, (E['EOF'], 2)), (b'1e+', 0, (E['EOF'], 3)),
    (b'01', 0, (E['NUMBER'], 1)), (b'-01', 0, (E['NUMBER'], 2)), (b'1.a', 0, (E['NUMBER'], 2)),
    (b'-a', 0, (E['NUMBER'], 1)), (b'1ea', 0, (E['NUMBER'], 2)), (b'.5', 0, (E['SYNTAX'], 0)),
    (b'+1', 0, (E['SYNTAX'], 0)), (b'1 2', 0, (E['TRAILING'], 2)), (b'[1 2]', 0, (E['SYNTAX'], 3)),
    (b'[1,]', 0, (E['SYNTAX'], 3)), (b'{"a" 1}', 0, (E['SYNTAX'], 5)), (b'{a:1}', 0, (E['SYNTAX'], 1)),
    (b'{"a":1,}', 0, (E['SYNTAX'], 7)), (b'[}', 0, (E['SYNTAX'], 1)), (b'{]', 0, (E['SYNTAX'], 1)),
    (b'[1}', 0, (E['SYNTAX'], 2)), (b'truex', 0, (E['TRAILING'], 4)), (b'trUe', 0, (E['SYNTAX'], 2)),
    (b'"a\x01"', 0, (E['CONTROL'], 2)), (b'"\\x"', 0, (E['ESCAPE'], 2)), (b'"\\u12g4"', 0, (E['ESCAPE'], 5)),
    (b'"\x80"', 0, (E['UTF8'], 1)), (b'"\xc0\x80"', 0, (E['UTF8'], 1)), (b'"\xe0\x80\x80"', 0, (E['UTF8'], 2)),
    (b'"\xed\xa0\x80"', 0, (E['UTF8'], 2)), (b'"\xf4\x90\x80\x80"', 0, (E['UTF8'], 2)),
    (b'"\xf5\x80\x80\x80"', 0, (E['UTF8'], 1)), (b'"\xe2\x82"', 0, (E['UTF8'], 3)),
    (b'"\xe2\x82', 0, (E['EOF'], 3)), (b'"\xc3', 0, (E['EOF'], 2)),
    (b'/**/1', 0, (E['SYNTAX'], 0)), (b'NaN', 0, (E['SYNTAX'], 0)), (b'[1,2', 0, (E['EOF'], 4)),
    (b'\n\n  x', 0, (E['SYNTAX'], 4)),
    (b'[' * 1024 + b']' * 1024, 0, '[' * 1024 + ']' * 1024), (b'[' * 1025, 0, (E['DEPTH'], 1024)),
    (b'{"a":' * 1024 + b'1' + b'}' * 1024, 0, '{"a":' * 1024 + '1' + '}' * 1024),
]

# The reader only, with JSONF_MULTI: values one after another
MULTI_CASES = [
    (b'', MULTI, None), (b'  \n', MULTI, None), (b'1 2 3', MULTI, '1\n2\n3'),
    (b'{"a":1}\n{"b":2}\n', MULTI, '{"a":1}\n{"b":2}'), (b'[1]2"x"', MULTI, '[1]\n2\n"x"'),
    (b'1 2 [', MULTI, (E['EOF'], 5)), (b'truefalse', MULTI, 'true\nfalse'),
]

CHARS = (list(' abcdefghijklmnopqrstuvwxyzABCXYZ0123456789!#$%&\'()*+,-./:;<=>?@[]^_`{|}~')
         + ['"', '\\', '/', '\b', '\f', '\n', '\r', '\t', '\x00', '\x01', '\x1f', '\x7f']
         + ['é', 'ÿ', 'Ā', '߿', 'ࠀ', '€', ' ', '￿', '�',
            '\U00010000', '\U0001F600', '\U0010FFFF'])
SHORT = {'"': '\\"', '\\': '\\\\', '\b': '\\b', '\f': '\\f', '\n': '\\n', '\r': '\\r', '\t': '\\t'}


def canon_string(s):
    out = ['"']
    for ch in s:
        if ch in SHORT:
            out.append(SHORT[ch] if ch != '/' else '/')
        elif ord(ch) < 0x20:
            out.append('\\u%04x' % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def text_string(rnd, s):
    out = ['"']
    for ch in s:
        o = ord(ch)
        r = rnd.random()
        if ch == '/' and r < 0.3:
            out.append('\\/')
        elif ch in SHORT and r < 0.8:
            out.append(SHORT[ch])
        elif ch in '"\\' or o < 0x20 or r < 0.15:
            if o >= 0x10000:
                v = o - 0x10000
                out.append('\\u%04X\\u%04x' % (0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF)))
            else:
                out.append(('\\u%04x' if rnd.random() < 0.5 else '\\u%04X') % o)
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def gen_number(rnd):
    k = rnd.random()
    if k < 0.4:
        v = rnd.choice([0, 1, -1, 2**31 - 1, -2**31, 2**31, -2**31 - 1, rnd.randint(-1000, 1000),
                        rnd.randint(-2**40, 2**40)])
        t = str(v)
        return t, (t if -2**31 <= v < 2**31 else t)
    sign = '-' if rnd.random() < 0.3 else ''
    ip = rnd.choice(['0', str(rnd.randint(1, 9)), str(rnd.randint(1, 10**rnd.randint(1, 25)))])
    t = sign + ip
    if rnd.random() < 0.7:
        t += '.' + ''.join(rnd.choice('0123456789') for _ in range(rnd.randint(1, 20)))
    if rnd.random() < 0.5:
        t += rnd.choice('eE') + rnd.choice(['', '+', '-']) + str(rnd.randint(0, 400))
    return t, t


def ws(rnd):
    if rnd.random() < 0.7:
        return ''
    return ''.join(rnd.choice(' \t\r\n') for _ in range(rnd.randint(1, 3)))


def gen_value(rnd, depth):
    """(text, compact form)"""
    k = rnd.random()
    if depth > 6 or k < 0.45:
        c = rnd.random()
        if c < 0.35:
            s = ''.join(rnd.choice(CHARS) for _ in range(rnd.randint(0, 12)))
            return text_string(rnd, s), canon_string(s)
        if c < 0.75:
            t, cn = gen_number(rnd)
            if cn.lstrip('-').isdigit() and cn != '-0':
                cn = str(int(cn))
            return t, cn
        lit = rnd.choice(['true', 'false', 'null'])
        return lit, lit
    n = rnd.randint(0, 5)
    if k < 0.7:
        items = [gen_value(rnd, depth + 1) for _ in range(n)]
        text = '[' + ws(rnd) + (',' + ws(rnd)).join(t + ws(rnd) for t, _ in items) + ']'
        return text, '[' + ','.join(c for _, c in items) + ']'
    keys = [''.join(rnd.choice(CHARS[:40] + CHARS[-6:]) for _ in range(rnd.randint(0, 6))) for _ in range(n)]
    vals = [gen_value(rnd, depth + 1) for _ in range(n)]
    parts = [text_string(rnd, k) + ws(rnd) + ':' + ws(rnd) + v[0] + ws(rnd) for k, v in zip(keys, vals)]
    text = '{' + ws(rnd) + (',' + ws(rnd)).join(parts) + '}'
    return text, '{' + ','.join(canon_string(k) + ':' + v[1] for k, v in zip(keys, vals)) + '}'


def no_constants(name):
    raise ValueError(name)


REFUSED = object()


def py_verdict(b):
    """Python's json: REFUSED, 'skip', or the value"""
    try:
        s = b.decode('utf-8')
        if s.startswith('﻿'):
            return 'skip'
        return json.loads(s, parse_float=str, parse_constant=no_constants)
    except (ValueError, RecursionError):
        return REFUSED


def fix_surrogates(v):
    """our parser turns unpaired surrogate escapes into U+FFFD"""
    if isinstance(v, str):
        return ''.join('�' if 0xD800 <= ord(c) < 0xE000 else c for c in v)
    if isinstance(v, list):
        return [fix_surrogates(x) for x in v]
    if isinstance(v, dict):
        return {fix_surrogates(k): fix_surrogates(x) for k, x in v.items()}
    return v


def mutate(rnd, b):
    b = bytearray(b)
    for _ in range(rnd.randint(1, 3)):
        op = rnd.random()
        pos = rnd.randint(0, len(b))
        if op < 0.35 and b:
            del b[min(pos, len(b) - 1)]
        elif op < 0.7:
            b[pos:pos] = bytes([rnd.choice(b'[]{},:"\\0123456789-+.eEtrufalsn \x00\x1f\x80\xc3\xe2\xff/u')])
        elif b:
            b[min(pos, len(b) - 1)] = rnd.randrange(256)
    return bytes(b)


NUM_SPECIAL = ['0', '-0', '1', '0.1', '0.3', '1e23', '8.98846567431158e307', '1.7976931348623157e308',
               '1.7976931348623158e308', '1.7976931348623159e308', '1e309', '2.2250738585072011e-308',
               '2.2250738585072012e-308', '2.2250738585072014e-308', '4.9406564584124654e-324',
               '2.4703282292062327e-324', '2.4703282292062328e-324', '1e-324', '1e-400', '1e400',
               '9007199254740993', '9007199254740992.5', '123456789012345678901234567890',
               '2147483647', '2147483648', '-2147483648', '-2147483649', '1e9', '1.5e1', '10.0e-1',
               '0.000000000000000000000000000000000000001e40', '100e-2', '1' + '0' * 400 + 'e-400',
               '0.' + '0' * 330 + '1e330', '7.2057594037927933e16', '5e-324', '3e-324',
               '1.' + '0' * 800 + '1', '0.5' + '0' * 800 + '1e-323', '1' * 900 + 'e-600']


def gen_num_text(rnd):
    k = rnd.random()
    if k < 0.25:
        # around halfway points: a double's neighbour midpoint, written exactly
        bits = rnd.getrandbits(63) & ~(0x7FF << 52) | (rnd.randint(0, 2046) << 52)
        x = struct.unpack('>d', struct.pack('>Q', bits))[0]
        y = struct.unpack('>d', struct.pack('>Q', bits + 1))[0]
        if x != x or y != y or abs(y) == float('inf'):
            return '1'
        mid = (decimal.Decimal(x) + decimal.Decimal(y)) / 2
        t = format(mid, 'f') if abs(mid.adjusted()) < 30 else format(mid, 'e')
        if rnd.random() < 0.3:
            t += '1' if '.' in t and 'e' not in t else ''
        return t
    if k < 0.5:
        return repr(struct.unpack('>d', struct.pack('>Q', rnd.getrandbits(63)))[0]).replace('inf', '1').replace('nan', '2')
    digits = ''.join(rnd.choice('0123456789') for _ in range(rnd.randint(1, rnd.choice([5, 19, 30, 60]))))
    digits = digits.lstrip('0') or '0'
    t = digits
    if rnd.random() < 0.5 and len(digits) > 1:
        p = rnd.randint(1, len(digits) - 1)
        t = digits[:p] + '.' + digits[p:]
        if not t.split('.')[0]:
            t = '0' + t
    if rnd.random() < 0.6:
        t += 'e' + str(rnd.randint(-340, 320))
    return ('-' if rnd.random() < 0.3 else '') + t


def gen_bits(rnd):
    k = rnd.random()
    if k < 0.15:
        return rnd.choice([0, 1, 2, 0x000FFFFFFFFFFFFF, 0x0010000000000000, 0x7FEFFFFFFFFFFFFF,
                           0x7FF0000000000000, 0x7FF8000000000000, 0xFFF0000000000000,
                           0x8000000000000000, 0x3FF0000000000000, 0x4330000000000000,
                           0x4340000000000000, 0x4350000000000000, 0x444B1AE4D6E2EF50,
                           0x444B1AE4D6E2EF4F, 0x3EB0C6F7A0B5ED8D, 0x3EB0C6F7A0B5ED8C])
    if k < 0.3:
        v = rnd.randint(-2**60, 2**60)
        return struct.unpack('>Q', struct.pack('>d', float(v)))[0]
    if k < 0.45:
        v = rnd.randint(1, 99999) / 10 ** rnd.randint(0, 10)
        return struct.unpack('>Q', struct.pack('>d', v))[0]
    return rnd.getrandbits(64)


def gen(d):
    rnd = random.Random(20261022)
    os.makedirs(d, exist_ok=True)
    cases = []          # (kind, expected, flags, file)

    def add(b, flags, expected, reader_only=False):
        i = len(cases)
        path = os.path.join(d, 'c%05d.json' % i)
        with open(path, 'wb') as f:
            f.write(b)
        cases.append((path, flags, expected, reader_only))

    for b, fl, ex in HAND:
        add(b, fl, ex)
    for b, fl, ex in MULTI_CASES:
        add(b, fl, ex, True)
    valid = []
    for _ in range(600):
        text, cn = gen_value(rnd, 0)
        b = (ws(rnd) + text + ws(rnd)).encode('utf-8')
        valid.append(b)
        add(b, 0, cn)
    for _ in range(1500):
        b = mutate(rnd, rnd.choice(valid))
        v = py_verdict(b)
        if isinstance(v, str) and v == 'skip':
            continue
        add(b, 0, ('py', None if v is REFUSED else 1))
    with open(os.path.join(d, 'script.txt'), 'w') as f:
        for i, (path, fl, ex, reader_only) in enumerate(cases):
            if not reader_only:
                f.write('parse %d %s\n' % (fl, path))
            f.write('reader %d 0 %s\n' % (fl, path))
            f.write('reader %d %d %s\n' % (fl, 1 + i, path))
        nums = NUM_SPECIAL + [gen_num_text(rnd) for _ in range(3000)]
        for t in nums:
            f.write('num %s\n' % t)
        bits = [gen_bits(rnd) for _ in range(3000)]
        for b in bits:
            f.write('fmt %016x\n' % b)
    with open(os.path.join(d, 'cases.json'), 'w') as f:
        json.dump({'cases': cases, 'nums': nums, 'bits': bits}, f)


def node_strings(bits):
    node = shutil.which('node')
    if not node:
        return None
    prog = ("const b=require('fs').readFileSync(0,'utf8').trim().split('\\n');"
            "const v=new DataView(new ArrayBuffer(8));"
            "console.log(b.map(h=>{v.setBigUint64(0,BigInt('0x'+h));const x=v.getFloat64(0);"
            "return Number.isFinite(x)?String(x):'null';}).join('\\n'));")
    r = subprocess.run([node, '-e', prog], input='\n'.join('%016x' % b for b in bits),
                       capture_output=True, text=True, check=True)
    return r.stdout.split('\n')


def es_string(x):
    """ECMAScript Number::toString from Python's shortest repr digits"""
    if x != x or x in (float('inf'), float('-inf')):
        return 'null'
    if x == 0:
        return '0'
    sign = '-' if x < 0 else ''
    m, _, e = repr(abs(x)).partition('e')
    ip, _, fp = m.partition('.')
    digits = (ip + fp).lstrip('0')
    n = len(ip.lstrip('0')) if ip.strip('0') else -(len(fp) - len(fp.lstrip('0')))
    n += int(e or 0)
    digits = digits.rstrip('0') or '0'
    k = len(digits)
    if k <= n <= 21:
        return sign + digits + '0' * (n - k)
    if 0 < n <= 21:
        return sign + digits[:n] + '.' + digits[n:]
    if -6 < n <= 0:
        return sign + '0.' + '0' * -n + digits
    x10 = n - 1
    return sign + digits[0] + ('.' + digits[1:] if k > 1 else '') + 'e' + ('+' if x10 >= 0 else '-') + str(abs(x10))


def check(d, out):
    with open(os.path.join(d, 'cases.json')) as f:
        data = json.load(f)
    with open(out, encoding='utf-8', errors='surrogateescape') as f:
        lines = f.read().split('\n')
    bad = [l for l in lines if l.startswith('bad')]
    lines = [l for l in lines if l and not l.startswith('bad')]
    fails = list(bad)
    li = 0
    counts = dict(parse=0, reader=0, num=0, fmt=0)
    for path, fl, ex, reader_only in data['cases']:
        answers = []
        for kind in (['reader'] * 2 if reader_only else ['parse', 'reader', 'reader']):
            answers.append(lines[li])
            li += 1
            counts[kind] += 1
        first = answers[0]
        for a in answers[1:]:
            if a != first:
                fails.append('%s: reader %r, parser %r' % (path, a[:120], first[:120]))
        with open(path, 'rb') as f:
            b = f.read()
        if isinstance(ex, list) and ex and ex[0] == 'py':
            if ex[1] is None:
                if not first.startswith('err'):
                    fails.append('%s: we accept what Python refuses: %r' % (path, b[:100]))
            else:
                if not first.startswith('ok '):
                    fails.append('%s: we refuse (%s) what Python takes: %r' % (path, first, b[:100]))
                else:
                    ours = json.loads(first[3:], parse_float=str)
                    theirs = fix_surrogates(json.loads(b.decode('utf-8'), parse_float=str))
                    if ours != theirs:
                        fails.append('%s: value differs: %r' % (path, first[:100]))
        elif isinstance(ex, list):
            code, off = ex
            parts = first.split()
            if parts[0] != 'err' or int(parts[1]) != code or int(parts[2]) != off:
                fails.append('%s: %r: want err %d at %d, got %r' % (path, b[:60], code, off, first[:80]))
            else:
                line = b[:off].count(b'\n') + 1
                col = off - (b[:off].rfind(b'\n') + 1) + 1
                if int(parts[3]) != line or int(parts[4]) != col:
                    fails.append('%s: line/column %s %s, want %d %d' % (path, parts[3], parts[4], line, col))
        elif ex is None:
            if first != 'ok':
                fails.append('%s: want no value, got %r' % (path, first[:80]))
        else:
            want = 'ok ' + ex.replace('\n', ' ')
            if first != want:
                fails.append('%s: %r\n   want %r' % (path, first[:200], want[:200]))
    for t in data['nums']:
        l = lines[li]
        li += 1
        counts['num'] += 1
        parts = l.split()
        x = float(t)
        want = struct.unpack('>Q', struct.pack('>d', x))[0]
        dv = decimal.Decimal(t)
        exact = dv == dv.to_integral_value() and -2**31 <= dv < 2**31
        wl = str(int(dv)) if exact else '-'
        if parts[0] != 'num' or parts[1] == 'err' or int(parts[1], 16) != want or parts[2] != wl:
            fails.append('num %s: got %r, want %016x %s' % (t[:60], l, want, wl))
    ref = node_strings(data['bits'])
    for i, b in enumerate(data['bits']):
        l = lines[li]
        li += 1
        counts['fmt'] += 1
        x = struct.unpack('>d', struct.pack('>Q', b))[0]
        want = ref[i] if ref else es_string(x)
        if want == '-0':
            want = '0'
        if l != 'fmt ' + want:
            fails.append('fmt %016x: got %r, want %r' % (b, l, want))
    for f in fails[:40]:
        print('FAIL', f)
    print('%d parses, %d reader runs, %d numbers, %d doubles formatted (%s), %d failures'
          % (counts['parse'], counts['reader'], counts['num'], counts['fmt'],
             'Node' if ref else 'Python repr', len(fails)))
    return not fails


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        gen(sys.argv[2])
    else:
        sys.exit(0 if check(sys.argv[2], sys.argv[3]) else 1)
