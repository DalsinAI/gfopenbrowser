#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# The 68k template JIT's templates (J2, stage T1 prototype).
#
# Reads the 68k interpreter that offlineasm's M68K back end generates
# (DerivedSources/JavaScriptCore/LLIntAssembly.h) and writes each bytecode
# handler a second time as a template: the same code, made so it can be
# copied into a code buffer, one copy per bytecode instruction, and patched.
#
# - Operand reads, loads from (K,a3,a2.l*1) (PB + PC + K), become
#   "move.l #imm,dN" holes. The stitcher writes the operand's value, read
#   from the bytecode with the hole's size and sign, into the immediate.
# - Dispatch (fetch the next opcode, jump through the opcode map) becomes
#   "cmpa.l #next,a2; jeq end; jmp redispatch". The stitcher writes this
#   instruction's PC plus its length as next; when PC moved by exactly one
#   instruction the code falls through into the next copied template, and
#   otherwise (a jump, a slow path that changed PC, an exception) it leaves
#   for _jsc_m68k_tjit_redispatch, which carries on in the interpreter.
# - Branches and calls to code outside the template become absolute, and
#   references to labels inside it PC-relative, so a copy runs anywhere.
# - Local labels get a suffix so templates and interpreter share one file.
#
# Interpreter registers, frames and the register file are untouched, so a
# template and the interpreter can hand over at any bytecode boundary.
#
# usage: m68k_templates.py [--only op_a,op_b] LLIntAssembly.h LLIntTemplates.h [report.txt]

import re
import sys

SUFFIX = "__tpl"
OPTIMISE = True
PLACEHOLDER = "0x55555555"

# Hole kinds, as the stitcher reads them (llint/M68KTemplateJIT.h).
HOLE_U8, HOLE_S8, HOLE_U16, HOLE_S16, HOLE_32, HOLE_NEXT_PC = 1, 2, 3, 4, 5, 6
# The high word of a signed operand (0 or -1), for the peephole pass.
HOLE_HI_S8, HOLE_HI_S16, HOLE_HI_32 = 7, 8, 9

INSTR = re.compile(r'^(\s*)"(.*?)\s*\\n"(.*)$')
LOCAL_DEF = re.compile(r'^\s*OFFLINE_ASM_LOCAL_LABEL\((\w+)\)\s*$')
INLINE_DEF = re.compile(r'^(Lm68k\d+):$')
HANDLER = re.compile(r'^OFFLINE_ASM_(OPCODE_LABEL|GLUE_LABEL|GLOBAL_LABEL|UNALIGNED_GLOBAL_LABEL)\((\w+)\)')
OPERAND_LOAD = re.compile(r'^move\.([bwl]) \((-?\d+),a3,a2\.l\*1\),(d\d)$')
LOCAL_REF = re.compile(r'LOCAL_LABEL_STRING\((\w+)\)')
INLINE_REF = re.compile(r'\b(Lm68k\d+)\b')
GLOBAL_REF = re.compile(r'LABEL_REFERENCE\((\w+)\)')
DISPATCH_LEA = 'LABEL_REFERENCE(os_script_config_storage)'


class Unsupported(Exception):
    pass


def parse(path):
    """Splits LLIntAssembly.h into (name, kind, lines) blocks, one per
    global label, keeping only the 68k OFFLINE_ASM block."""
    blocks = []
    current = None
    for raw in open(path):
        line = raw.rstrip("\n")
        m = HANDLER.match(line)
        if m:
            current = (m.group(2), m.group(1), [])
            blocks.append(current)
            continue
        if line.startswith("OFFLINE_ASM_END"):
            current = None
            continue
        if current is not None:
            current[2].append(line)
    return blocks


def items(lines):
    """Turns a block's lines into ("label", name) and ("insn", text)."""
    out = []
    for line in lines:
        m = LOCAL_DEF.match(line)
        if m:
            out.append(("label", m.group(1)))
            continue
        m = INSTR.match(line)
        if not m:
            continue
        text = m.group(2).strip()
        if text.startswith(".size") or text.startswith(".type"):
            continue
        m2 = INLINE_DEF.match(text)
        if m2:
            out.append(("label", m2.group(1)))
            continue
        out.append(("insn", text))
    return out


def defined_labels(body):
    return {name for kind, name in body if kind == "label"}


def find_dispatch(body, i):
    """body[i] is the "lea os_script_config_storage,a0" of a dispatch.
    Returns (start, end): the opcode fetch before it up to the jmp (a0)."""
    start = i
    # Back over the opcode fetch: [moveq #0,d0] move.b (K,a3,a2.l*1),d0,
    # then t0's write (move.l d0,d4 / clr.l of the high word).
    j = i - 1
    while j >= 0 and body[j][0] == "insn" and not OPERAND_LOAD.match(body[j][1]):
        if i - j > 4:
            raise Unsupported("dispatch without opcode fetch")
        j -= 1
    if j < 0 or body[j][0] != "insn" or not body[j][1].startswith("move.b"):
        raise Unsupported("dispatch without opcode fetch")
    start = j
    if start > 0 and body[start - 1] == ("insn", "moveq #0,d0"):
        start -= 1
    k = i + 1
    while k < len(body) and body[k] != ("insn", "jmp (a0)"):
        if body[k][0] != "insn" or k - i > 6:
            raise Unsupported("dispatch without jmp (a0)")
        k += 1
    if k >= len(body):
        raise Unsupported("dispatch without jmp (a0)")
    if not any(re.match(r'move\.l \(\d+,a0,d\d\.l\*4\),a0', body[x][1]) for x in range(i + 1, k)):
        raise Unsupported("dispatch without opcode map load")
    return start, k


def rename(text, local):
    text = GLOBAL_REF.sub(lambda m: 'LOCAL_LABEL_STRING(%s%s)' % (m.group(1), SUFFIX) if m.group(1) in local else m.group(0), text)
    text = LOCAL_REF.sub(lambda m: 'LOCAL_LABEL_STRING(%s%s)' % (m.group(1), SUFFIX) if m.group(1) in local else m.group(0), text)
    text = INLINE_REF.sub(lambda m: m.group(1) + SUFFIX if m.group(1) in local else m.group(0), text)
    return text


def target_of(text):
    """The label a branch or jump names, and whether it is local to the
    interpreter's text (LOCAL_LABEL_STRING / Lm68k) or a global symbol."""
    m = LOCAL_REF.search(text)
    if m:
        return m.group(1), "local"
    m = INLINE_REF.search(text)
    if m:
        return m.group(1), "local"
    m = GLOBAL_REF.search(text)
    if m:
        return m.group(1), "global"
    return None, None


def is_handler_part(op, name):
    """Global labels the interpreter puts after a handler that still belong
    to it: the return point of a call op (llint_cloop_did_return_from_js_N,
    op_X[_slow]_return_location) and its slow paths (js_trampoline_op_X).
    Taken into the template so calls return into compiled code."""
    m = re.match(r'^(.*?)(_wide16|_wide32)?$', op)
    base, width = re.escape(m.group(1)), re.escape(m.group(2) or "")
    return (re.match(r'llint_cloop_did_return_from_js_\d+$', name) is not None
            or re.match(r'^%s(_\w+)?_return_location%s$' % (base, width), name) is not None
            or re.match(r'^js_trampoline_%s(_\w+)?%s$' % (base, width), name) is not None)


def ref(name, scope):
    return '" LOCAL_LABEL_STRING(%s) "' % name if scope == "local" else '" LABEL_REFERENCE(%s) "' % name


def make_template(name, body):
    local = defined_labels(body)
    out = []          # lines of the template, as asm text (C string bodies)
    holes = []        # (label, bytecode offset, kind)
    stubs = []        # far branch stubs
    counter = [0]

    def newlabel(prefix):
        counter[0] += 1
        return "Ltj_%s_%s_%d" % (prefix, name, counter[0])

    end = "Ltj_end_%s" % name
    windows = {}
    for x, (kind, text) in enumerate(body):
        if kind == "insn" and text.startswith("lea") and DISPATCH_LEA in text:
            start, stop = find_dispatch(body, x)
            windows[start] = stop
    i = 0
    n = len(body)
    while i < n:
        kind, text = body[i]
        if kind == "label":
            if text.startswith("Lm68k"):
                out.append('%s%s:' % (text, SUFFIX))
            else:
                out.append('" LOCAL_LABEL_STRING(%s%s) ":' % (text, SUFFIX))
            i += 1
            continue
        if i in windows:
            hl = newlabel("next")
            out.append('%s:' % hl)
            out.append('cmpa.l #%s,a2' % PLACEHOLDER)
            holes.append((hl, 0, HOLE_NEXT_PC, i))
            out.append('jeq %s' % end)
            out.append('jmp (" LABEL_REFERENCE(jsc_m68k_tjit_redispatch) ").l')
            i = windows[i] + 1
            continue
        m = OPERAND_LOAD.match(text)
        if m:
            size, offset, reg = m.group(1), int(m.group(2)), m.group(3)
            signed = None
            skip_next = False
            if size == "l":
                hkind = HOLE_32
            else:
                nxt = body[i + 1] if i + 1 < n else ("", "")
                ext = "extb.l %s" % reg if size == "b" else "ext.l %s" % reg
                if nxt == ("insn", ext):
                    signed = True
                    skip_next = True
                elif out and out[-1] == "moveq #0,%s" % reg:
                    signed = False
                    out.pop()
                else:
                    # A byte or word load that is neither sign- nor zero-extended
                    # (an opcode compare, for example): keep it as code. It
                    # still reads the bytecode through PB and PC, which stay
                    # valid in compiled code.
                    out.append(text)
                    i += 1
                    continue
                hkind = {("b", True): HOLE_S8, ("b", False): HOLE_U8,
                         ("w", True): HOLE_S16, ("w", False): HOLE_U16}[(size, signed)]
            hl = newlabel("op")
            out.append('%s:' % hl)
            out.append('move.l #%s,%s' % (PLACEHOLDER, reg))
            holes.append((hl, offset, hkind, i))
            i += 2 if skip_next else 1
            continue
        text = rename(text, local)
        op = text.split(" ", 1)[0]
        label, scope = target_of(body[i][1])
        if label is not None and label not in local:
            if op in ("jra", "jmp"):
                out.append('jmp (%s).l' % ref(label, scope))
            elif op == "jsr":
                out.append('jsr (%s).l' % ref(label, scope))
            elif re.match(r'j(eq|ne|cs|cc|hi|ls|lt|le|gt|ge|mi|pl|vs|vc)$', op):
                stub = newlabel("far")
                stubs.append('%s:' % stub)
                stubs.append('jmp (%s).l' % ref(label, scope))
                out.append('%s %s' % (op, stub))
            elif op == "lea" or (op == "move.l" and text.startswith("move.l #")):
                out.append(text)          # an absolute address: fine anywhere
            else:
                raise Unsupported("unknown use of outside label: %s" % text)
        elif label is not None and label in local and op in ("lea", "move.l", "pea"):
            # Absolute reference to a label in this template (a return
            # address): make it PC-relative.
            if op == "lea":
                m2 = re.match(r'lea (.*),(a\d)$', text)
                out.append('lea %s(pc),%s' % (m2.group(1), m2.group(2)))
            else:
                m2 = re.match(r'move\.l #(.*),(.*)$', text)
                if not m2:
                    raise Unsupported("local label as data: %s" % text)
                out.append('pea %s(pc)' % m2.group(1))
                out.append('move.l (sp)+,%s' % m2.group(2))
        else:
            if re.search(r'a3,a2', text):
                raise Unsupported("other bytecode access: %s" % text)
            out.append(text)
        i += 1
    last = [line for line in out if not line.endswith(":")][-1:]
    if not last or not re.match(r'^(jmp|jra|illegal|jsr _abort)', last[0]) or out[-1].endswith(":"):
        raise Unsupported("runs off its end")
    if stubs:
        out.append('jra %s' % end)
        out.extend(stubs)
    if OPTIMISE:
        out, holes = peephole(out, holes, end)
    return out, holes, end


# ---------------------------------------------------------------------------
# Peephole pass (stage T4). The interpreter's handlers are written for any
# instruction and any operand; a template knows it sits at an instruction
# boundary and holds this instruction's operands. A backward liveness pass
# over the template, with what m68k.py guarantees about registers, lets it
# drop and merge instructions:
# - d0-d3, a0, a1, a6, fp7 and the condition codes are scratch inside one
#   offlineasm instruction, so they are dead wherever code leaves the
#   template for an offlineasm label or a C function.
# - At the end of an instruction (the fall-through into the next template)
#   the interpreter's temporaries t0-t12 (d4-d7 and the register file's first
#   13 slots) are dead too: no handler reads one before writing it.

SCRATCH = {"d0", "d1", "d2", "d3", "a0", "a1", "a6", "fp7", "cc", "fcc"}
VRF_WORDS = ["vrf+%d" % k for k in range(0, 28 * 8, 4)]
ALL = ({"d%d" % i for i in range(8)} | {"a%d" % i for i in range(8)} | {"fp%d" % i for i in range(8)}
       | {"cc", "fcc", "mem"} | set(VRF_WORDS))
EXIT_LIVE = frozenset(ALL - SCRATCH)
TEMPS = {"d4", "d5", "d6", "d7"} | {"vrf+%d" % k for k in range(0, 13 * 8, 4)}
DISPATCH_LIVE = frozenset(EXIT_LIVE - TEMPS)
CALL_CLOBBERS = {"d0", "d1", "a0", "a1", "fp0", "fp1", "cc", "fcc"}

REG = re.compile(r'^(d[0-7]|a[0-7]|sp|fp[0-7])$')
VRF_OPERAND = re.compile(r'^_jsc_m68k_vrf\+(\d+)$')
LABEL_LINE = re.compile(r'^(.*):$')
BRANCH_TARGET = re.compile(r'LOCAL_LABEL_STRING\((\w+)\)|\b(Lm68k\d+%s)\b|\b(Ltj_\w+)\b' % SUFFIX)
CC_READERS = re.compile(r'^(j(eq|ne|cs|cc|hi|ls|lt|le|gt|ge|mi|pl|vs|vc)|s(eq|ne|cs|cc|hi|ls|lt|le|gt|ge|mi|pl|vs|vc)|addx|subx|negx)')


def split_operands(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        out.append(cur)
    return [o.strip() for o in out]


def regname(r):
    return "a7" if r == "sp" else r


def operand_regs(o):
    """Registers an addressing mode reads (not the register operand itself)."""
    if o.startswith("#"):
        return set()
    if '"' in o and "(" not in o.replace("LABEL_REFERENCE(", "").replace("LOCAL_LABEL_STRING(", ""):
        return set()
    return {regname(r) for r in re.findall(r'\b(d[0-7]|a[0-7]|sp)\b', o)}


def classify(o, size):
    """('reg', r) | ('vrf', [words]) | ('imm',) | ('mem', regs read, regs updated)."""
    if REG.match(o):
        return ("reg", regname(o))
    if o.startswith("#"):
        return ("imm",)
    m = VRF_OPERAND.match(o)
    if m:
        k = int(m.group(1))
        words = ["vrf+%d" % (k - k % 4)]
        if size in ("d", "x") or (size == "l" and k % 4):
            words.append("vrf+%d" % (k - k % 4 + 4))
        return ("vrf", words)
    regs = operand_regs(o)
    upd = {regname(r) for r in re.findall(r'-\((sp|a[0-7])\)|\((sp|a[0-7])\)\+', o) for r in r if r}
    return ("mem", regs, upd)


def effects(text):
    """(reads, writes, kind, target). kind: 'op', 'branch', 'jump', 'exit',
    'call', 'barrier'. Conservative where unsure."""
    parts = text.split(" ", 1)
    op = parts[0]
    ops = split_operands(parts[1]) if len(parts) > 1 else []
    base, _, size = op.partition(".")
    reads, writes = set(), set()

    def src(o):
        c = classify(o, size)
        if c[0] == "reg":
            reads.add(c[1])
        elif c[0] == "vrf":
            reads.update(c[1])
        elif c[0] == "mem":
            reads.update(c[1])
            reads.add("mem")
            reads.update(c[2])
            writes.update(c[2])

    def dst(o, partial=False):
        c = classify(o, size)
        if c[0] == "reg":
            writes.add(c[1])
            if partial or (size in ("b", "w") and c[1][0] == "d"):
                reads.add(c[1])
        elif c[0] == "vrf":
            writes.update(c[1])
            if partial or size in ("b", "w"):
                reads.update(c[1])
        elif c[0] == "mem":
            reads.update(c[1])
            reads.update(c[2])
            writes.update(c[2])
            writes.add("mem")
        else:
            raise Unsupported("bad destination")

    if base in ("jra", "jmp") or re.match(r'^j(eq|ne|cs|cc|hi|ls|lt|le|gt|ge|mi|pl|vs|vc)$', base) or base.startswith("fb"):
        m = BRANCH_TARGET.search(text)
        target = next(g for g in m.groups() if g) if m else None
        if base.startswith("fb"):
            reads.add("fcc")
        elif base not in ("jra", "jmp"):
            reads.add("cc")
        if base == "jmp" and target is None:
            src(ops[0])     # jmp (a0)
            return reads | EXIT_LIVE, writes, "exit", None
        if base == "jmp" and "LABEL_REFERENCE" in text and "LOCAL_LABEL_STRING" not in text:
            target = None   # outside the template
        kind = "jump" if base in ("jra", "jmp") else "branch"
        return reads, writes, kind, target
    if base == "jsr":
        return EXIT_LIVE | {"mem"}, set(CALL_CLOBBERS), "call", None
    if base in ("illegal",) or base.startswith("."):
        return set(ALL), set(), "barrier", None
    try:
        if base == "move" or base == "movea":
            src(ops[0])
            dst(ops[1])
            if not (classify(ops[1], size)[0] == "reg" and ops[1][0] == "a"):
                writes.add("cc")
        elif base == "moveq":
            dst(ops[1])
            writes.add("cc")
        elif base == "clr":
            dst(ops[0])
            writes.add("cc")
        elif base == "lea":
            src_regs = operand_regs(ops[0])
            reads.update(src_regs)
            writes.add(regname(ops[1]))
        elif base == "pea":
            reads.update(operand_regs(ops[0]))
            reads.add("a7")
            writes.update({"a7", "mem"})
        elif base in ("add", "sub", "and", "or", "eor", "muls", "mulu", "adda", "suba", "lsl", "lsr", "asr", "asl", "addq", "subq", "addx", "subx"):
            src(ops[0])
            dst(ops[1], partial=True)
            if base in ("addx", "subx"):
                reads.add("cc")
            if not (classify(ops[1], size)[0] == "reg" and regname(ops[1])[0] == "a"):
                writes.add("cc")
        elif base in ("cmp", "cmpa", "cmpi"):
            src(ops[0])
            src(ops[1])
            writes.add("cc")
        elif base == "tst":
            src(ops[0])
            writes.add("cc")
        elif base in ("neg", "not", "ext", "extb"):
            dst(ops[0], partial=True)
            writes.add("cc")
        elif re.match(r'^s(eq|ne|cs|cc|hi|ls|lt|le|gt|ge|mi|pl|vs|vc)$', base):
            reads.add("cc")
            dst(ops[0], partial=True)
        elif base.startswith("f"):
            # FPU: model fp registers and memory, keep everything else.
            for o in ops[:-1]:
                src(o) if not REG.match(o) else reads.add(regname(o))
            last = ops[-1] if ops else ""
            if base.startswith("fs"):
                reads.add("fcc")
                dst(last, partial=True)
            elif base == "fcmp":
                src(last) if not REG.match(last) else reads.add(regname(last))
                writes.add("fcc")
            else:
                if REG.match(last):
                    writes.add(regname(last))
                    if base not in ("fmove",):
                        reads.add(regname(last))
                else:
                    dst(last)
                writes.add("fcc")
        else:
            return set(ALL), set(), "barrier", None
    except (Unsupported, IndexError):
        return set(ALL), set(), "barrier", None
    return reads, writes, "op", None


def liveness(nodes, end):
    """nodes: ('label', name) / ('insn', text, marker). Returns live-out sets."""
    n = len(nodes)
    labels = {node[1]: i for i, node in enumerate(nodes) if node[0] == "label"}
    eff = [effects(node[1]) if node[0] == "insn" else None for node in nodes]
    live_in = [set() for _ in range(n + 1)]
    live_out = [set() for _ in range(n)]

    def at(target):
        if target == end:
            return DISPATCH_LIVE
        if target in labels:
            return live_in[labels[target]]
        return EXIT_LIVE

    changed = True
    while changed:
        changed = False
        for i in range(n - 1, -1, -1):
            if nodes[i][0] == "label":
                new = live_in[i + 1] if i + 1 < n else DISPATCH_LIVE
            else:
                reads, writes, kind, target = eff[i]
                if kind in ("jump",):
                    out = at(target)
                elif kind == "branch":
                    out = at(target) | (live_in[i + 1] if i + 1 < n else DISPATCH_LIVE)
                elif kind == "exit":
                    out = set()
                else:
                    out = live_in[i + 1] if i + 1 < n else DISPATCH_LIVE
                out = set(out)
                if out != live_out[i]:
                    live_out[i] = out
                new = (out - writes) | reads if kind != "barrier" else set(ALL)
            if new != live_in[i]:
                live_in[i] = set(new)
                changed = True
    return live_out, eff


def peephole(out, holes, end):
    """Rewrites a template's lines; returns (lines, holes)."""
    marker_names = {h[0] for h in holes}
    hole_kind = {h[0]: h[2] for h in holes}
    nodes = []
    pending = None
    for line in out:
        m = LABEL_LINE.match(line)
        if m and m.group(1) in marker_names:
            pending = m.group(1)
            continue
        if m:
            nodes.append(("label", m.group(1).replace('" LOCAL_LABEL_STRING(', "").replace(') "', "").strip('"').strip()))
            nodes[-1] = ("label", nodes[-1][1], line)
            continue
        nodes.append(("insn", line, pending))
        pending = None
    # Normalise label names to what BRANCH_TARGET extracts.
    nodes = [(n[0], re.sub(r'^"?\s*', "", n[1]), n[2]) for n in nodes]
    new_holes = {h[0]: h for h in holes}

    def drop_marker(mk):
        if mk:
            new_holes.pop(mk, None)

    for _ in range(8):
        live_out, eff = liveness(nodes, end)
        changed = False
        res = []
        i = 0
        while i < len(nodes):
            node = nodes[i]
            if node[0] != "insn":
                res.append(node)
                i += 1
                continue
            text, mk = node[1], node[2]
            reads, writes, kind, _ = eff[i]
            lo = live_out[i]
            parts = text.split(" ", 1)
            ops = split_operands(parts[1]) if len(parts) > 1 else []
            # move.l dN,dN: only sets the flags.
            if parts[0] == "move.l" and len(ops) == 2 and ops[0] == ops[1] and REG.match(ops[0]) and "cc" not in lo:
                changed = True
                i += 1
                continue
            # Dead: writes only registers / register-file words nobody reads.
            if kind == "op" and "mem" not in writes and "a7" not in writes and writes and not (writes & lo):
                drop_marker(mk)
                changed = True
                i += 1
                continue
            nxt = nodes[i + 1] if i + 1 < len(nodes) else None
            if nxt and nxt[0] == "insn" and parts[0] == "move.l" and len(ops) == 2 and REG.match(ops[1]) and ops[1][0] == "d":
                n_parts = nxt[1].split(" ", 1)
                n_ops = split_operands(n_parts[1]) if len(n_parts) > 1 else []
                r = ops[1]
                n_lo = live_out[i + 1]
                # move.l SRC,dS; move.l dS,DST  ->  move.l SRC,DST
                if (n_parts[0] == "move.l" and len(n_ops) == 2 and n_ops[0] == r and n_ops[1] != r
                        and r not in n_lo and nxt[2] is None and r not in operand_regs(n_ops[1])
                        and ("cc" not in n_lo or not (REG.match(n_ops[1]) and n_ops[1][0] == "a"))):
                    res.append(("insn", "move.l %s,%s" % (ops[0], n_ops[1]), mk))
                    changed = True
                    i += 2
                    continue
                # move.l aP,dS; add.l #N,dS; move.l dS,aP  ->  lea N(aP),aP
                n2 = nodes[i + 2] if i + 2 < len(nodes) else None
                if (re.match(r'^a[2-5]$', ops[0]) and n_parts[0] in ("add.l", "addq.l", "sub.l", "subq.l") and len(n_ops) == 2
                        and n_ops[1] == r and re.match(r'^#(0x[0-9a-f]+|-?\d+)$', n_ops[0]) and n2 and n2[0] == "insn"
                        and n2[1] == "move.l %s,%s" % (r, ops[0]) and r not in live_out[i + 2] and "cc" not in live_out[i + 2]
                        and mk is None and nxt[2] is None and n2[2] is None):
                    k = int(n_ops[0][1:], 0)
                    if n_parts[0].startswith("sub"):
                        k = -k
                    if -32768 <= k < 32768:
                        res.append(("insn", "lea %d(%s),%s" % (k, ops[0], ops[0]), None))
                        changed = True
                        i += 3
                        continue
            # move.l #hole,dN; smi dX; extb.l dX; move.l dX,DST  ->  hole's high word straight to DST
            if (mk and hole_kind.get(mk) in (HOLE_S8, HOLE_S16, HOLE_32) and i + 3 < len(nodes)
                    and all(nodes[i + j][0] == "insn" and nodes[i + j][2] is None for j in (1, 2, 3))):
                dx = nodes[i + 1][1][4:] if nodes[i + 1][1].startswith("smi ") else None
                if (dx and nodes[i + 2][1] == "extb.l %s" % dx and nodes[i + 3][1].startswith("move.l %s," % dx)
                        and dx not in live_out[i + 3] and "cc" not in live_out[i + 3]):
                    dest = nodes[i + 3][1][len("move.l %s," % dx):]
                    hl = mk + "_hi"
                    res.append(node)
                    res.append(("insn", "move.l #%s,%s" % (PLACEHOLDER, dest), hl))
                    h = new_holes[mk]
                    new_holes[hl] = (hl, h[1], {HOLE_S8: HOLE_HI_S8, HOLE_S16: HOLE_HI_S16, HOLE_32: HOLE_HI_32}[h[2]], h[3])
                    hole_kind[hl] = new_holes[hl][2]
                    changed = True
                    i += 4
                    continue
            res.append(node)
            i += 1
        nodes = res
        if not changed:
            break

    lines = []
    for node in nodes:
        if node[0] == "label":
            lines.append(node[2])
        else:
            if node[2]:
                lines.append("%s:" % node[2])
            lines.append(node[1])
    holes = [h for h in holes if h[0] in new_holes] + [h for k, h in new_holes.items() if k not in {x[0] for x in holes}]
    return lines, holes


def emit(templates, path):
    with open(path, "w") as f:
        f.write("// Generated by tools/jsjit/m68k_templates.py from LLIntAssembly.h. Do not edit.\n")
        f.write("// The AmiCradle Team. Templates for the 68k template JIT (J2).\n\n")
        f.write('".text\\n"\n".balign 2\\n"\n')
        for name, out, holes, end in templates:
            f.write('"Ltj_start_%s:\\n"\n' % name)
            for line in out:
                f.write('    "%s \\n"\n' % line)
            f.write('"%s:\\n"\n' % end)
        f.write('".data\\n"\n".balign 4\\n"\n')
        for name, out, holes, end in templates:
            f.write('".globl " LABEL_REFERENCE(jsc_m68k_tpl_%s) "\\n"\n' % name)
            f.write('LABEL_REFERENCE(jsc_m68k_tpl_%s) ":\\n"\n' % name)
            f.write('    ".long Ltj_start_%s, %s - Ltj_start_%s, %d\\n"\n' % (name, end, name, len(holes)))
            for hl, offset, kind, _ in holes:
                imm = 2  # the immediate follows a one-word opcode in both forms
                f.write('    ".word %s + %d - Ltj_start_%s, %d, %d\\n"\n' % (hl, imm, name, offset, kind))
        f.write('".text\\n"\n')


def emit_index(templates, path):
    """Writes LLIntTemplateIndex.h beside the templates: M68K_TEMPLATE(op)
    for each opcode whose narrow, wide16 and wide32 handlers all became
    templates. M68KTemplateJIT.cpp builds its opcode table from it."""
    import os
    names = {name for name, _, _, _ in templates}
    index = os.path.join(os.path.dirname(os.path.abspath(path)), "LLIntTemplateIndex.h")
    with open(index, "w") as f:
        f.write("// Generated by m68k_templates.py. Do not edit.\n")
        for name, _, _, _ in templates:
            if name.endswith("_wide16") or name.endswith("_wide32"):
                continue
            if name + "_wide16" in names and name + "_wide32" in names:
                f.write("M68K_TEMPLATE(%s)\n" % name)


def main(argv):
    only = None
    if len(argv) > 2 and argv[1] == "--only":
        only = set(argv[2].split(","))
        argv = argv[:1] + argv[3:]
    if len(argv) < 3:
        sys.stderr.write("usage: m68k_templates.py [--only op_a,op_b] LLIntAssembly.h LLIntTemplates.h [report.txt]\n")
        return 2
    blocks = parse(argv[1])
    templates = []
    report = []
    for b, (name, kind, lines) in enumerate(blocks):
        if kind != "OPCODE_LABEL" or not name.startswith("op_"):
            continue
        if only is not None and name not in only:
            continue
        body = items(lines)
        k = b + 1
        while k < len(blocks) and blocks[k][1] != "OPCODE_LABEL" and is_handler_part(name, blocks[k][0]):
            body.append(("label", blocks[k][0]))
            body.extend(items(blocks[k][2]))
            k += 1
        try:
            out, holes, end = make_template(name, body)
        except Unsupported as e:
            report.append("SKIP %s: %s" % (name, e))
            continue
        templates.append((name, out, holes, end))
        report.append("OK   %s: %d lines, %d holes" % (name, len(out), len(holes)))
    emit(templates, argv[2])
    emit_index(templates, argv[2])
    text = "\n".join(report) + "\n%d templates, %d skipped\n" % (len(templates), sum(1 for r in report if r.startswith("SKIP")))
    if len(argv) > 3:
        open(argv[3], "w").write(text)
    sys.stdout.write(text.splitlines()[-1] + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
