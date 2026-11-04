# MIT, Copyright (c) 2026 The AmiCradle Team.
# Cuts the Builder class, Operand and the bit operation methods out of
# M68KTemplateJIT.cpp into extracted.inc, for harness.cpp.
import re, sys
L = open(sys.argv[1]).read().split('\n')
def find(pat, start=0):
    for i in range(start, len(L)):
        if re.search(pat, L[i]):
            return i
    raise SystemExit("not found: " + pat)
def block(startpat, start=0):
    j = find(startpat, start); depth = 0; out = []; seen = False
    while True:
        line = L[j]; out.append(line)
        depth += line.count('{') - line.count('}')
        if '{' in line: seen = True
        if seen and depth == 0: break
        if not seen and line.rstrip().endswith(';'): break
        j += 1
    return '\n'.join(out)
b = find(r'^class Builder'); e = b
while L[e] != '};': e += 1
fp = find(r'^class FastPaths')
pats = [r'void slowUnless', r'void bailUnless', r'void useFloatPath', r'void checkInt32', r'void load\(const Operand', r'void storeInt32',
        r'static bool same', r'static bool isConstant\(const Operand', r'static constexpr uint32_t doubleEncodeOffsetHigh', r'void boxDouble',
        r'static constexpr uint64_t doubleTwo32',
        r'void truncateRoutine', r'void truncateToInt32', r'void bitOperation', r'void floatBitOperation']
parts = [block(p, fp) for p in pats]
open(sys.argv[2], 'w').write('\n'.join(L[b:e + 1]) + '\n' + block(r'^struct Operand') + '\n'
    + 'struct FloatPath { bool needed{false}; OpcodeID opcode{op_bitand}; Operand dst, lhs, rhs; unsigned target{0}; Vector<size_t> branches; };\n'
    + 'class Paths {\npublic:\n' + '\n'.join(parts)
    + '\n    Builder& m_hot; Vector<size_t>* m_slow; Vector<size_t>* m_bail; FloatPath* m_float; size_t m_truncateRoutine { SIZE_MAX };\n    Paths(Builder& b) : m_hot(b) { }\n};\n')
