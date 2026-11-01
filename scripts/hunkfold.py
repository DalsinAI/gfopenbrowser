#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
"""hunkfold: fold the duplicate template copies of an Amiga hunk C++ link.

scripts/build-webcore.sh runs it after linking the programs (OB_FOLD).

The problem. With -ffunction-sections every template instance and every
other inline function is a hunk ".text.<mangled>" holding one weak EXT_DEF
(type 0x41) at offset 0. GCC and gas write the calls to it from the same
object as hunk relocations (HUNK_RELOC32, HUNK_RELRELOC32 ...) against that
hunk, not as references to the symbol. Our ld (binutils 2.46, amiga
backend) then keeps the copies:

  * bfd/amigaos.c makes a hunk link-once (duplicates dropped) only when
    every entry of its HUNK_EXT is a weak definition. A hunk that calls
    anything outside its object has EXT_REF entries, so it never qualifies.
  * the hunk relocations of each object point at its own copy, and
    amiga_gc_sections() (amiga_keep_section, amiga_collect) keeps every hunk
    that defines a referenced global or weak NAME, not only the copy the
    symbol resolved to.

In the full engine of 22 October 2026 that kept 7,221 .text names more than
once: 37,208 extra copies, about 6.2 MB.

What fold does, for every hunk whose only symbol is a weak EXT_DEF at
offset 0:

 1. every relocation in the same unit that targets that hunk becomes an
    external reference to the symbol, in the hunk where it sits:
        HUNK_RELOC32      1004 -> EXT_REF32     129
        HUNK_RELOC32SHORT 1020 -> EXT_REF32     129
        HUNK_ABSRELOC16   1022 -> EXT_ABSREF16  138
        HUNK_RELRELOC32   1021 -> EXT_RELREF32  136
        HUNK_RELOC16      1005 -> EXT_RELREF16  131  (pc-relative 16)
        HUNK_RELOC8       1006 -> EXT_RELREF8   132  (pc-relative 8)
    The field in the hunk data is not touched: ld adds the symbol's offset
    in its section to the field in both cases, and the symbol is at 0.
 2. with --winners, the weak EXT_DEF of every copy except the one the
    original link used becomes a local EXT_DEF (0x21). Only the winner
    still defines the name, every reference goes to it, and --gc-sections
    drops the other copies, which nothing references any more.

Step 2 is what makes the link smaller; step 1 alone changes nothing with
our ld. The winner of a section name is the copy under which ld printed the
symbol in the -Map file of the plain link: the first definition loaded, the
one every outside reference already used. Only definitions in losing copies
change, so the same archive members are linked and the same copies run as
before for calls from other objects ("members" checks that).

Left alone, and counted: hunks with more than one symbol, a weak symbol not
at offset 0, hunks targeted by HUNK_DREL* (base-relative), relocations from
.debug_* hunks, and any unit with a block type fold does not know (copied
byte for byte).

A winners file goes stale when the sources change: always take it from the
map of the plain link of the same objects. A winner that no longer defines
the name gives an "undefined reference" at link time; one that defines it
but was not linked before would be pulled in, with its constructors.

The real fix belongs in ld: amiga_collect() should keep only the section the
link hash resolved a name to (and the link-once test should look at
definitions only). Then step 1 alone would be enough, with no map.

usage: hunkfold.py COMMAND ...
  winners MAP OUT        the winners of a link, from its -Map file
  fold IN OUT [--winners W] [--imports I] [--name N] [--quiet]
                         rewrite IN (an object, or a hunk library: a stream
                         of HUNK_UNITs, which is what m68k-amigaos-ar writes)
                         as OUT (IN may be OUT). N is how the file shows in
                         the map (default IN); a .a file's units show as
                         basename(N)(basename(unit name)), for example
                         libWebCore.a(Page.cpp.obj)
                         I: names (the last word of each line) whose every
                         copy here becomes local, as for a losing copy: the
                         names a jsimage client takes from JavaScriptEngine
                         (scripts/jsimage.py)
  members PLAIN FOLDED   the archive members two -Map files linked; fails
                         when FOLDED linked one PLAIN did not
  copies MAP             section names linked more than once, and how many
                         extra copies
  stats IN... [--winners W]  what fold would do, without writing
  dump IN [--hunks]      block and EXT type census
  roundtrip IN...        parse and write unchanged; each must say "ok"
"""
import os
import struct
import sys
from collections import Counter

HUNK_UNIT, HUNK_NAME, HUNK_CODE, HUNK_DATA, HUNK_BSS = 999, 1000, 1001, 1002, 1003
HUNK_RELOC32, HUNK_RELOC16, HUNK_RELOC8 = 1004, 1005, 1006
HUNK_EXT, HUNK_SYMBOL, HUNK_DEBUG, HUNK_END = 1007, 1008, 1009, 1010
HUNK_DREL32, HUNK_DREL16, HUNK_DREL8 = 1015, 1016, 1017
HUNK_RELOC32SHORT, HUNK_RELRELOC32, HUNK_ABSRELOC16 = 1020, 1021, 1022

NAMES = {999: 'UNIT', 1000: 'NAME', 1001: 'CODE', 1002: 'DATA', 1003: 'BSS',
         1004: 'RELOC32', 1005: 'RELRELOC16', 1006: 'RELRELOC8', 1007: 'EXT',
         1008: 'SYMBOL', 1009: 'DEBUG', 1010: 'END', 1015: 'DREL32',
         1016: 'DREL16', 1017: 'DREL8', 1020: 'RELOC32SHORT',
         1021: 'RELRELOC32', 1022: 'ABSRELOC16'}

EXT_SYMB, EXT_DEF, EXT_ABS, EXT_RES = 0, 1, 2, 3
EXT_REF32, EXT_COMMON, EXT_REF16, EXT_REF8 = 129, 130, 131, 132
EXT_DEXT32, EXT_DEXT16, EXT_DEXT8, EXT_RELREF32 = 133, 134, 135, 136
EXT_RELCOMMON, EXT_ABSREF16, EXT_ABSREF8 = 137, 138, 139
EXT_RELREF16, EXT_RELREF8 = EXT_REF16, EXT_REF8   # same codes, pc-relative in ld
EXT_DEXT32COMMON, EXT_DEXT16COMMON, EXT_DEXT8COMMON = 208, 209, 210
EXT_WEAK, EXT_LOCAL = 0x40, 0x20

EXTNAMES = {0: 'SYMB', 1: 'DEF', 2: 'ABS', 3: 'RES', 129: 'REF32', 130: 'COMMON',
            131: 'RELREF16', 132: 'RELREF8', 133: 'DEXT32', 134: 'DEXT16',
            135: 'DEXT8', 136: 'RELREF32', 137: 'RELCOMMON', 138: 'ABSREF16',
            139: 'ABSREF8', 208: 'DEXT32COMMON', 209: 'DEXT16COMMON',
            210: 'DEXT8COMMON'}

DEF_TYPES = (EXT_SYMB, EXT_DEF, EXT_ABS, EXT_RES)
REF_TYPES = (EXT_REF32, EXT_REF16, EXT_REF8, EXT_DEXT32, EXT_DEXT16, EXT_DEXT8,
             EXT_RELREF32, EXT_ABSREF16, EXT_ABSREF8)
COMMON_TYPES = (EXT_COMMON, EXT_RELCOMMON, EXT_DEXT32COMMON, EXT_DEXT16COMMON,
                EXT_DEXT8COMMON)

LONG_RELOCS = (HUNK_RELOC32, HUNK_RELOC16, HUNK_RELOC8, HUNK_DREL32, HUNK_DREL16,
               HUNK_DREL8, HUNK_RELRELOC32, HUNK_ABSRELOC16)
SHORT_RELOCS = (HUNK_RELOC32SHORT,)
DREL_RELOCS = (HUNK_DREL32, HUNK_DREL16, HUNK_DREL8)

# hunk reloc -> symbol reloc with the same howto in bfd/amigaos.c
CONVERT = {HUNK_RELOC32: EXT_REF32, HUNK_RELOC32SHORT: EXT_REF32,
           HUNK_ABSRELOC16: EXT_ABSREF16, HUNK_RELRELOC32: EXT_RELREF32,
           HUNK_RELOC16: EXT_RELREF16, HUNK_RELOC8: EXT_RELREF8}

DEBUG_PREFIXES = (b'.debug_', b'.gnu.linkonce.wi.', b'.stab')


class Unknown(Exception):
    pass


def ext_base(t):
    return t & ~0x60 if t < 200 else t


class Hunk:
    __slots__ = ('index', 'name', 'kind', 'head', 'blocks')

    def __init__(self, index):
        self.index = index
        self.name = None      # bytes
        self.kind = None      # HUNK_CODE/DATA/BSS
        self.head = []        # raw bytes before the reloc/ext blocks (NAME + content)
        self.blocks = []      # [type, payload]; payload: reloc dict / ext list / raw bytes


def parse_name(buf, pos):
    n = struct.unpack_from('>I', buf, pos)[0]
    raw = bytes(buf[pos + 4:pos + 4 + n * 4])
    return raw.rstrip(b'\0'), pos + 4 + n * 4


def parse_unit(buf, pos, end):
    """Parse one HUNK_UNIT starting at pos.  Returns (unit_name, hunks, newpos)."""
    t = struct.unpack_from('>I', buf, pos)[0]
    if t != HUNK_UNIT:
        raise Unknown('expected HUNK_UNIT at 0x%x, got %d' % (pos, t))
    uname, pos = parse_name(buf, pos + 4)
    hunks = []
    cur = None
    while pos < end:
        start = pos
        t = struct.unpack_from('>I', buf, pos)[0]
        tv = t & 0x3fffffff
        pos += 4
        if tv == HUNK_UNIT:
            pos = start
            break
        if cur is None:
            cur = Hunk(len(hunks))
        if tv == HUNK_NAME:
            if cur.kind is not None:
                raise Unknown('HUNK_NAME after content at 0x%x' % start)
            cur.name, pos = parse_name(buf, pos)
            cur.head.append(bytes(buf[start:pos]))
        elif tv in (HUNK_CODE, HUNK_DATA, HUNK_BSS):
            if cur.kind is not None:
                raise Unknown('second content block in hunk at 0x%x' % start)
            cur.kind = tv
            n = struct.unpack_from('>I', buf, pos)[0]
            pos += 4
            if (n >> 30) == 3:
                pos += 4          # memory attributes follow
            if tv != HUNK_BSS:
                pos += (n & 0x3fffffff) * 4
            cur.head.append(bytes(buf[start:pos]))
        elif tv in LONG_RELOCS:
            rel = {}
            while True:
                cnt = struct.unpack_from('>I', buf, pos)[0]
                pos += 4
                if cnt == 0:
                    break
                th = struct.unpack_from('>I', buf, pos)[0]
                offs = struct.unpack_from('>%dI' % cnt, buf, pos + 4)
                pos += 4 + cnt * 4
                rel.setdefault(th, []).extend(offs)
            cur.blocks.append([tv, rel])
        elif tv in SHORT_RELOCS:
            rel = {}
            while True:
                cnt = struct.unpack_from('>H', buf, pos)[0]
                pos += 2
                if cnt == 0:
                    break
                th = struct.unpack_from('>H', buf, pos)[0]
                offs = struct.unpack_from('>%dH' % cnt, buf, pos + 2)
                pos += 2 + cnt * 2
                rel.setdefault(th, []).extend(offs)
            if pos & 2:
                pos += 2
            cur.blocks.append([tv, rel])
        elif tv == HUNK_EXT:
            ents = []
            while True:
                w = struct.unpack_from('>I', buf, pos)[0]
                pos += 4
                if w == 0:
                    break
                et = w >> 24
                ln = w & 0xffffff
                name = bytes(buf[pos:pos + ln * 4]).rstrip(b'\0')
                pos += ln * 4
                b = ext_base(et)
                if b in DEF_TYPES:
                    val = struct.unpack_from('>I', buf, pos)[0]
                    pos += 4
                    ents.append([et, name, val])
                elif b in REF_TYPES or b in COMMON_TYPES:
                    size = None
                    if b in COMMON_TYPES:
                        size = struct.unpack_from('>I', buf, pos)[0]
                        pos += 4
                    cnt = struct.unpack_from('>I', buf, pos)[0]
                    refs = list(struct.unpack_from('>%dI' % cnt, buf, pos + 4))
                    pos += 4 + cnt * 4
                    ents.append([et, name, refs, size])
                else:
                    raise Unknown('EXT type %d at 0x%x' % (et, pos))
            cur.blocks.append([tv, ents])
        elif tv == HUNK_SYMBOL:
            syms = []
            while True:
                ln = struct.unpack_from('>I', buf, pos)[0]
                pos += 4
                if ln == 0:
                    break
                name = bytes(buf[pos:pos + ln * 4]).rstrip(b'\0')
                val = struct.unpack_from('>I', buf, pos + ln * 4)[0]
                pos += ln * 4 + 4
                syms.append((name, val))
            cur.blocks.append([tv, syms])
        elif tv == HUNK_DEBUG:
            n = struct.unpack_from('>I', buf, pos)[0]
            pos += 4 + n * 4
            cur.blocks.append([tv, bytes(buf[start:pos])])
        elif tv == HUNK_END:
            if cur.kind is None:
                raise Unknown('HUNK_END without content at 0x%x' % start)
            hunks.append(cur)
            cur = None
        else:
            raise Unknown('hunk type %d (0x%x) at 0x%x' % (t, t, start))
        if pos > end:
            raise Unknown('block runs past end at 0x%x' % start)
    if cur is not None:
        raise Unknown('unit ends inside a hunk')
    return uname, hunks, pos


def name_longs(name, typ):
    raw = name + b'\0' * ((4 - len(name) % 4) % 4)
    return struct.pack('>I', (typ << 24) | (len(raw) // 4)) + raw


def write_unit(uname, hunks):
    out = [struct.pack('>I', HUNK_UNIT)]
    raw = uname + b'\0' * ((4 - len(uname) % 4) % 4)
    out.append(struct.pack('>I', len(raw) // 4) + raw)
    for h in hunks:
        out.extend(h.head)
        for t, p in h.blocks:
            if t in LONG_RELOCS:
                if not p:
                    continue
                b = [struct.pack('>I', t)]
                for th in p:
                    offs = p[th]
                    if offs:
                        b.append(struct.pack('>II%dI' % len(offs), len(offs), th, *offs))
                if len(b) == 1:
                    continue
                b.append(b'\0\0\0\0')
                out.append(b''.join(b))
            elif t in SHORT_RELOCS:
                b = [struct.pack('>I', t)]
                for th in p:
                    offs = p[th]
                    if offs:
                        b.append(struct.pack('>HH%dH' % len(offs), len(offs), th, *offs))
                if len(b) == 1:
                    continue
                b.append(b'\0\0')
                s = b''.join(b)
                if len(s) & 2:
                    s += b'\0\0'
                out.append(s)
            elif t == HUNK_EXT:
                if not p:
                    continue
                b = [struct.pack('>I', t)]
                for e in p:
                    b.append(name_longs(e[1], e[0]))
                    base = ext_base(e[0])
                    if base in DEF_TYPES:
                        b.append(struct.pack('>I', e[2]))
                    else:
                        if base in COMMON_TYPES:
                            b.append(struct.pack('>I', e[3]))
                        b.append(struct.pack('>I%dI' % len(e[2]), len(e[2]), *e[2]))
                b.append(b'\0\0\0\0')
                out.append(b''.join(b))
            elif t == HUNK_SYMBOL:
                b = [struct.pack('>I', t)]
                for name, val in p:
                    raw = name + b'\0' * ((4 - len(name) % 4) % 4)
                    b.append(struct.pack('>I', len(raw) // 4) + raw + struct.pack('>I', val))
                b.append(b'\0\0\0\0')
                out.append(b''.join(b))
            else:
                out.append(p)
        out.append(struct.pack('>I', HUNK_END))
    return b''.join(out)


# --imports: the names a jsimage client takes from JavaScriptEngine
# (scripts/jsimage.py); none of the program's own copies may win
IMPORTS = set()


def fold_unit(hunks, st, winners=None, key=None):
    """Rewrite relocations in place.  Returns True if anything changed.

    winners: {hunk name: map file key of the copy the original link used};
    key: this unit's map file key.  A weak def in an eligible hunk whose
    winner is another file becomes local."""
    nh = len(hunks)
    # 1. pick the target hunks
    targets = {}   # hunk index -> symbol name
    for h in hunks:
        defs = []
        nsym = 0
        for t, p in h.blocks:
            if t == HUNK_EXT:
                for e in p:
                    if ext_base(e[0]) in DEF_TYPES:
                        defs.append(e)
            elif t == HUNK_SYMBOL:
                nsym += len(p)
        weak = [e for e in defs if e[0] < 200 and (e[0] & EXT_WEAK)]
        if not weak:
            continue
        kind = NAMES.get(h.kind, '?')
        st['weak_hunks_' + kind] += 1
        if len(defs) + nsym > 1:
            st['skip_multi_symbol'] += 1
            continue
        e = weak[0]
        if ext_base(e[0]) != EXT_DEF or e[0] & EXT_LOCAL:
            st['skip_not_ext_def'] += 1
            continue
        if e[2] != 0:
            st['skip_not_offset0'] += 1
            continue
        targets[h.index] = e[1]
    if not targets:
        return False
    # 2. base-relative refs to a target: leave that target alone
    for h in hunks:
        for t, p in h.blocks:
            if t in DREL_RELOCS:
                for th in p:
                    if th in targets and p[th]:
                        st['skip_drel_target'] += 1
                        del targets[th]
    if not targets:
        return False
    # 3. move relocations to EXT refs
    touched = set()
    changed = False
    for h in hunks:
        is_debug = h.name is not None and h.name.startswith(DEBUG_PREFIXES)
        newrefs = {}   # (exttype, name) -> offsets
        for t, p in h.blocks:
            if t not in CONVERT:
                continue
            for th in list(p):
                if th not in targets or not p[th]:
                    continue
                if th >= nh:
                    raise Unknown('reloc to hunk %d of %d' % (th, nh))
                if is_debug:
                    st['relocs_left_debug'] += len(p[th])
                    continue
                rk = (CONVERT[t], targets[th])
                newrefs.setdefault(rk, []).extend(p[th])
                st['relocs_' + NAMES[t]] += len(p[th])
                touched.add(th)
                del p[th]
        if not newrefs:
            continue
        changed = True
        ext = None
        for b in h.blocks:
            if b[0] == HUNK_EXT:
                ext = b
                break
        if ext is None:
            # binutils order: relocs, EXT, SYMBOL, DEBUG
            pos = 0
            for i, b in enumerate(h.blocks):
                if b[0] in LONG_RELOCS or b[0] in SHORT_RELOCS:
                    pos = i + 1
            ext = [HUNK_EXT, []]
            h.blocks.insert(pos, ext)
        for (et, name), offs in newrefs.items():
            for e in ext[1]:
                if e[0] == et and e[1] == name:
                    e[2].extend(offs)
                    break
            else:
                ext[1].append([et, name, sorted(offs), None])
    st['sections_rewritten'] += len(touched)
    st['sections_no_relocs'] += len(targets) - len(touched)
    # 4. non-winning copies: weak def -> local def; with --imports, every
    #    copy of a name the program takes from JavaScriptEngine
    if winners is not None or IMPORTS:
        for th in targets:
            h = hunks[th]
            if targets[th] in IMPORTS:
                for t, p in h.blocks:
                    if t == HUNK_EXT:
                        for e in p:
                            if e[1] == targets[th] and ext_base(e[0]) == EXT_DEF:
                                e[0] = EXT_DEF | EXT_LOCAL
                st['defs_made_local_import'] += 1
                changed = True
                continue
            if winners is None:
                continue
            w = winners.get(h.name)
            if w is None:
                st['defs_kept_no_winner'] += 1
                continue
            if w == key:
                st['defs_kept_winner'] += 1
                continue
            for t, p in h.blocks:
                if t == HUNK_EXT:
                    for e in p:
                        if e[1] == targets[th] and ext_base(e[0]) == EXT_DEF:
                            e[0] = EXT_DEF | EXT_LOCAL
            st['defs_made_local'] += 1
            changed = True
    return changed


def map_key(path, unit_name=None):
    base = path.rsplit('/', 1)[-1]
    if unit_name is None:
        return base
    return '%s(%s)' % (base, unit_name.rsplit('/', 1)[-1])


def map_file_key(field):
    """'lib/libX.a(member.obj)' -> 'libX.a(member.obj)'; 'dir/a.o' -> 'a.o'."""
    if field.endswith(')') and '(' in field:
        arc, mem = field[:-1].split('(', 1)
        return map_key(arc, mem)
    return map_key(field)


def read_winners_from_map(path, out):
    """For every input section name, the file whose copy has a symbol line
    printed under it in the memory map.  Names with 0 or >1 such copies are
    left out."""
    tagged = {}     # name -> set of file keys with a symbol line
    copies = Counter()
    in_map = False
    cur = None      # (name, filekey) of the last input section entry
    with open(path, 'r', errors='replace') as f:
        pending = None
        for line in f:
            if not in_map:
                if line.startswith('Linker script and memory map'):
                    in_map = True
                continue
            if line.startswith(' .') or line.startswith(' COMMON'):
                parts = line.split()
                if len(parts) >= 4 and parts[1].startswith('0x') and parts[2].startswith('0x'):
                    cur = (parts[0], map_file_key(parts[3]))
                    copies[parts[0]] += 1
                    pending = None
                elif len(parts) == 1:
                    pending = parts[0]
                    cur = None
                else:
                    cur = None
                    pending = None
                continue
            if line.startswith('                0x'):
                parts = line.split(None, 2)
                if pending is not None:
                    if len(parts) >= 3 and parts[1].startswith('0x'):
                        cur = (pending, map_file_key(parts[2].strip()))
                        copies[pending] += 1
                    pending = None
                    continue
                if cur is not None and len(parts) >= 2 and not parts[1].startswith('0x'):
                    tagged.setdefault(cur[0], set()).add(cur[1])
                continue
            # output section header, script line, fill: not a symbol of cur
            cur = None
            pending = None
    n_amb = 0
    with open(out, 'w') as o:
        for name in sorted(tagged):
            s = tagged[name]
            if len(s) != 1:
                n_amb += 1
                continue
            o.write('%s\t%s\n' % (name, next(iter(s))))
    dup_names = sum(1 for n, c in copies.items() if c > 1)
    dup_extra = sum(c - 1 for n, c in copies.items() if c > 1)
    print('section names with a winner  %d' % (len(tagged) - n_amb))
    print('ambiguous (left out)         %d' % n_amb)
    print('names linked more than once  %d (%d extra copies)' % (dup_names, dup_extra))


def map_members(path):
    """The archive members a link took in, as libX.a(member): the first part
    of its -Map file, one member per line that does not start with a space."""
    members = set()
    with open(path, 'r', errors='replace') as f:
        first = f.readline()
        if not first.startswith('Archive member included'):
            return members
        for line in f:
            if line.startswith(('Discarded input sections', 'Allocating common symbols',
                                'Memory Configuration', 'Linker script and memory map')):
                break
            if line.strip() and not line[0].isspace():
                # by archive name, as the two links read different folders
                members.add(map_file_key(line.split()[0]))
    return members


def compare_members(plain, folded):
    a = map_members(plain)
    b = map_members(folded)
    new = sorted(b - a)
    gone = sorted(a - b)
    print('archive members: %d in the plain link, %d folded, %d new, %d left out'
          % (len(a), len(b), len(new), len(gone)))
    for m in new[:20]:
        print('  new: ' + m)
    for m in gone[:20]:
        print('  left out: ' + m)
    # A member the plain link did not take brings code and constructors the
    # program never had: the winners do not belong to this link.
    return 1 if new or not a else 0


def count_copies(path):
    copies = Counter()
    in_map = False
    pending = None
    with open(path, 'r', errors='replace') as f:
        for line in f:
            if not in_map:
                in_map = line.startswith('Linker script and memory map')
                continue
            if line.startswith(' .'):
                parts = line.split()
                if len(parts) >= 4 and parts[1].startswith('0x') and parts[2].startswith('0x'):
                    copies[parts[0]] += 1
                    pending = None
                elif len(parts) == 1:
                    pending = parts[0]
                else:
                    pending = None
                continue
            if pending is not None and line.startswith('                0x'):
                parts = line.split(None, 2)
                if len(parts) >= 3 and parts[1].startswith('0x'):
                    copies[pending] += 1
            pending = None
    names = sum(1 for c in copies.values() if c > 1)
    extra = sum(c - 1 for c in copies.values() if c > 1)
    print('names linked more than once  %d (%d extra copies)' % (names, extra))


def load_winners(path):
    w = {}
    with open(path) as f:
        for line in f:
            name, key = line.rstrip('\n').split('\t')
            w[name.encode()] = key
    return w


def process(buf, st, rewrite, winners=None, name=None):
    """Yield output chunks for a whole file (object or hunk library)."""
    is_lib = name is not None and name.endswith('.a')
    pos = 0
    end = len(buf)
    while pos < end:
        start = pos
        try:
            uname, hunks, pos = parse_unit(buf, pos, end)
        except Unknown as ex:
            # cannot parse: copy the rest of the file untouched
            st['units_unparsed'] += 1
            sys.stderr.write('hunkfold: unit at 0x%x left alone: %s\n' % (start, ex))
            if rewrite:
                yield bytes(buf[start:])
            return
        st['units'] += 1
        try:
            key = None
            if name is not None:
                key = map_key(name, uname.decode(errors='replace') if is_lib else None)
            changed = fold_unit(hunks, st, winners, key)
        except Unknown as ex:
            st['units_unparsed'] += 1
            sys.stderr.write('hunkfold: unit %s left alone: %s\n' % (uname.decode(errors='replace'), ex))
            changed = False
        if not rewrite:
            continue
        if changed:
            st['units_changed'] += 1
            yield write_unit(uname, hunks)
        else:
            yield bytes(buf[start:pos])


def roundtrip_check(path):
    """Parse + write every unit unchanged; must give the same bytes."""
    buf = open(path, 'rb').read()
    pos = 0
    while pos < len(buf):
        start = pos
        uname, hunks, pos = parse_unit(buf, pos, len(buf))
        if write_unit(uname, hunks) != buf[start:pos]:
            return 'mismatch in unit %s' % uname.decode(errors='replace')
    return 'ok'


def dump(path, show_hunks):
    buf = open(path, 'rb').read()
    blocks = Counter()
    exts = Counter()
    pos = 0
    nunits = 0
    while pos < len(buf):
        uname, hunks, pos = parse_unit(buf, pos, len(buf))
        nunits += 1
        for h in hunks:
            blocks[NAMES.get(h.kind)] += 1
            for t, p in h.blocks:
                blocks[NAMES.get(t, str(t))] += 1
                if t == HUNK_EXT:
                    for e in p:
                        b = ext_base(e[0])
                        tag = EXTNAMES.get(b, str(b))
                        if e[0] < 200 and e[0] & EXT_WEAK:
                            tag += '+WEAK'
                        if e[0] < 200 and e[0] & EXT_LOCAL:
                            tag += '+LOCAL'
                        exts[tag] += 1
            if show_hunks:
                desc = []
                for t, p in h.blocks:
                    if t in LONG_RELOCS or t in SHORT_RELOCS:
                        desc.append('%s{%s}' % (NAMES[t], ','.join(
                            'h%d:%s' % (k, '/'.join('%x' % o for o in v)) for k, v in sorted(p.items()))))
                    elif t == HUNK_EXT:
                        for e in p:
                            b = ext_base(e[0])
                            v = ('@%x' % e[2]) if b in DEF_TYPES else '/'.join('%x' % o for o in e[2])
                            desc.append('EXT %s(0x%02x) %s %s' % (EXTNAMES.get(b, b), e[0], e[1].decode(), v))
                    else:
                        desc.append(NAMES.get(t, str(t)))
                print('%s h%d %s %s' % (uname.decode(), h.index, NAMES.get(h.kind),
                                        (h.name or b'').decode()))
                for d in desc:
                    print('    ' + d)
    print('units %d' % nunits)
    for k, v in sorted(blocks.items()):
        print('block %-14s %d' % (k, v))
    for k, v in sorted(exts.items()):
        print('ext   %-14s %d' % (k, v))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == 'dump':
        dump(argv[2], '--hunks' in argv)
        return 0
    if cmd == 'roundtrip':
        for p in argv[2:]:
            print(p, roundtrip_check(p))
        return 0
    if cmd == 'winners':
        read_winners_from_map(argv[2], argv[3])
        return 0
    if cmd == 'members':
        return compare_members(argv[2], argv[3])
    if cmd == 'copies':
        count_copies(argv[2])
        return 0
    st = Counter()
    winners = None
    name = None
    args = []
    i = 2
    while i < len(argv):
        if argv[i] == '--winners':
            winners = load_winners(argv[i + 1])
            i += 2
        elif argv[i] == '--imports':
            with open(argv[i + 1]) as f:
                IMPORTS.update(line.split()[-1].encode() for line in f if line.strip())
            i += 2
        elif argv[i] == '--name':
            name = argv[i + 1]
            i += 2
        elif argv[i].startswith('--'):
            i += 1
        else:
            args.append(argv[i])
            i += 1
    if cmd == 'stats':
        for p in args:
            for _ in process(open(p, 'rb').read(), st, False, winners, name or p):
                pass
    elif cmd == 'fold':
        src, dst = args[0], args[1]
        buf = open(src, 'rb').read()
        data = b''.join(process(buf, st, True, winners, name or src))
        with open(dst + '.tmp', 'wb') as f:
            f.write(data)
        os.replace(dst + '.tmp', dst)
    else:
        print(__doc__)
        return 2
    if '--quiet' not in argv:
        for k in sorted(st):
            print('%-28s %d' % (k, st[k]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
