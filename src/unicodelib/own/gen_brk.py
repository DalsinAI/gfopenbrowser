#!/usr/bin/env python3
# unicode.library, our own code: makes the text-breaking tables from the
# Unicode Character Database at build time (PLAN.md step 7, piece 2).
# Copyright (c) 2026 AmiCradle Team. MIT.
#
#   gen_brk.py UCD_DIR OUT_DIR
#
# UCD_DIR   the UCD 17.0.0 (as gen_props.py; every file read is checked
#           against ucd.sha256)
# OUT_DIR   uo_brk_data.c and uo_brk_data.h, for uo_brk.c
#
# Per code point one record: its line-break class (UAX #14, with LB1 done
# here: AI, SG and XX are AL; SA is CM when it is Mn or Mc, else AL;
# quotation marks split by Pi and Pf), its grapheme-cluster, word and
# sentence break classes (UAX #29; the word classes as unicode.library
# breaks words: letters of the scripts written without spaces join as
# letters, Han, kana and Hangul syllables are told apart by flags), its
# Indic conjunct class, its class for title-case breaking, and flags.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_props import UCD, MAXCP, build_trie, c_array, die, norm   # noqa: E402

# line-break classes, ours (LB1 already done)
LB = ["AL", "AP", "AK", "AS", "B2", "BA", "BB", "BK", "CB", "CJ", "CL", "CM", "CP", "CR",
      "EB", "EM", "EX", "GL", "H2", "H3", "HH", "HL", "HY", "ID", "IN", "IS", "JL", "JT", "JV",
      "LF", "NL", "NS", "NU", "OP", "PO", "PR", "QU", "QU_PI", "QU_PF", "RI", "SP", "SY",
      "VF", "VI", "WJ", "ZW", "ZWJ"]
GCB = ["Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend",
       "SpacingMark", "L", "V", "T", "LV", "LVT"]
INCB = ["None", "Consonant", "Extend", "Linker"]
WB = ["Other", "CR", "LF", "Newline", "Extend", "ZWJ", "Regional_Indicator", "Format",
      "Katakana", "Hebrew_Letter", "ALetter", "Single_Quote", "Double_Quote", "MidNumLet",
      "MidLetter", "MidNum", "Numeric", "ExtendNumLet", "WSegSpace"]
SB = ["Other", "CR", "LF", "Extend", "Sep", "Format", "Sp", "Lower", "Upper", "OLetter",
      "Numeric", "ATerm", "SContinue", "STerm", "Close"]
TITLE = ["NotCased", "Cased", "Ignorable"]
# flags
F_EA = 1            # East_Asian_Width F, W or H
F_EXTPICT = 2       # Extended_Pictographic
F_EXTPICT_CN = 4    # Extended_Pictographic and unassigned
F_HIRAGANA = 8      # Script=Hiragana
F_IDEO = 16         # Ideographic
F_KANAKANJI = 32    # Han, Hiragana or Word_Break=Katakana
F_HAN = 64          # Script=Han


def classes(ucd, name, names, default):
    idx = {norm(n): i for i, n in enumerate(names)}
    a = [default] * MAXCP
    for first, last, f in ucd.fields(name, missing=True):
        k = norm(f[0])
        if k not in idx:
            die(f"{name}: unknown value {f[0]}")
        for c in range(first, last + 1):
            a[c] = idx[k]
    return a


def prop_set(ucd, name, prop, value=None):
    s = set()
    for first, last, f in ucd.fields(name):
        if norm(f[0]) != norm(prop):
            continue
        if value is not None and (len(f) < 2 or norm(f[1]) != norm(value)):
            continue
        s.update(range(first, last + 1))
    return s


def main():
    if len(sys.argv) != 3:
        die("usage: gen_brk.py UCD_DIR OUT_DIR")
    ucd = UCD(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)

    gc = ["Cn"] * MAXCP
    for first, last, f in ucd.fields("extracted/DerivedGeneralCategory.txt", missing=True):
        for c in range(first, last + 1):
            gc[c] = f[0]
    ea = ["N"] * MAXCP
    for first, last, f in ucd.fields("EastAsianWidth.txt", missing=True):
        for c in range(first, last + 1):
            ea[c] = f[0]
    sc = [""] * MAXCP
    for first, last, f in ucd.fields("Scripts.txt"):
        for c in range(first, last + 1):
            sc[c] = f[0]
    rawlb = ["XX"] * MAXCP
    for first, last, f in ucd.fields("LineBreak.txt", missing=True):
        for c in range(first, last + 1):
            rawlb[c] = f[0]
    gcb = classes(ucd, "auxiliary/GraphemeBreakProperty.txt", GCB, 0)
    wb = classes(ucd, "auxiliary/WordBreakProperty.txt", WB, 0)
    sb = classes(ucd, "auxiliary/SentenceBreakProperty.txt", SB, 0)
    extpict = prop_set(ucd, "emoji/emoji-data.txt", "Extended_Pictographic")
    ideo = prop_set(ucd, "PropList.txt", "Ideographic")
    upper = prop_set(ucd, "DerivedCoreProperties.txt", "Uppercase")
    lower = prop_set(ucd, "DerivedCoreProperties.txt", "Lowercase")
    incb = [0] * MAXCP
    for v in (1, 2, 3):
        for c in prop_set(ucd, "DerivedCoreProperties.txt", "InCB", INCB[v]):
            incb[c] = v

    lbIdx = {n: i for i, n in enumerate(LB)}
    recIndex, recs = {}, []
    vals = [0] * MAXCP
    for c in range(MAXCP):
        # line: LB1
        x = rawlb[c]
        if x in ("AI", "SG", "XX"):
            x = "AL"
        elif x == "SA":
            x = "CM" if gc[c] in ("Mn", "Mc") else "AL"
        elif x == "QU":
            x = "QU_PI" if gc[c] == "Pi" else "QU_PF" if gc[c] == "Pf" else "QU"
        if x not in lbIdx:
            die(f"LineBreak.txt: class {x} at U+{c:04X} is not ours")
        fl = 0
        if ea[c] in ("F", "W", "H"):
            fl |= F_EA
        if c in extpict:
            fl |= F_EXTPICT
            if gc[c] == "Cn":
                fl |= F_EXTPICT_CN
        if sc[c] == "Hiragana":
            fl |= F_HIRAGANA
        if c in ideo:
            fl |= F_IDEO
        if sc[c] == "Han":
            fl |= F_HAN
        if sc[c] in ("Han", "Hiragana") or WB[wb[c]] == "Katakana":
            fl |= F_KANAKANJI
        # word: Han's Extend characters are Han, not Extend; letters of
        # scripts written without spaces (LineBreak SA) are letters; Han,
        # kana and Hangul syllables are not ALetter (their own rules)
        w = WB[wb[c]]
        hangul = 0xAC00 <= c <= 0xD7A3
        if w == "Extend" and sc[c] == "Han":
            w = "Other"
        if w == "ALetter" and (fl & F_KANAKANJI or hangul):
            w = "Other"
        if rawlb[c] == "SA" and w != "Extend" and GCB[gcb[c]] != "Control":
            if w not in ("Other", "ALetter"):
                die(f"U+{c:04X}: SA with Word_Break {w}: decide how it breaks words")
            w = "ALetter"
        # title-case breaking: case-ignorable and cased as u_strToTitle's
        # iterator sees them
        if gc[c] in ("Mn", "Me", "Cf", "Lm", "Sk") or c in (0x27, 0xAD, 0x2019):
            t = 2
        elif c in upper or c in lower or gc[c] == "Lt":
            t = 1
        else:
            t = 0
        r = (lbIdx[x], gcb[c] | incb[c] << 4 | t << 6, WB.index(w), sb[c], fl)
        if r not in recIndex:
            recIndex[r] = len(recs)
            recs.append(r)
        vals[c] = recIndex[r]

    # a code point of each line-break and word-break class, without flags,
    # for uo_brk.c's tables of the decisions that depend on two classes only
    lbRep, wbRep = [-1] * (len(LB) + 1), [-1] * len(WB)
    for c in range(MAXCP):
        r = recs[vals[c]]
        if r[4] or c == 0x25CC or 0xD800 <= c < 0xE000 or 0xAC00 <= c <= 0xD7A3:
            continue
        if lbRep[r[0]] < 0:
            lbRep[r[0]] = c
        if wbRep[r[2]] < 0:
            wbRep[r[2]] = c
    for i, c in enumerate(lbRep[:-1]):
        if c < 0:
            lbRep[i] = next(x for x in range(MAXCP) if recs[vals[x]][0] == i)
    lbRep[-1] = 0x3005          # loose: the iteration marks
    for i, c in enumerate(wbRep):
        if c < 0:       # Katakana: always flagged, so never looked up by class alone
            wbRep[i] = next(x for x in range(MAXCP) if recs[vals[x]][2] == i)
    b1, b2, t1, t2, leaf, ltype, size = build_trie(vals, "brk")
    with open(os.path.join(out, "uo_brk_data.h"), "w") as f:
        f.write("/* made by gen_brk.py from the UCD: unicode.library's breaking tables */\n")
        f.write("#include <stdint.h>\n\n")
        f.write("struct uo_brk { uint8_t lb, gcb_incb_title, wb, sb, flags; };\n")
        for prefix, names in (("UO_LB_", LB), ("UO_GCB_", GCB), ("UO_INCB_", INCB), ("UO_WB_", WB),
                              ("UO_SB_", SB), ("UO_TITLE_", TITLE)):
            f.write("enum { " + ", ".join(f"{prefix}{n.upper()} = {i}" for i, n in enumerate(names)) + " };\n")
        f.write("enum { UO_F_EA = 1, UO_F_EXTPICT = 2, UO_F_EXTPICT_CN = 4, UO_F_HIRAGANA = 8,\n"
                "       UO_F_IDEO = 16, UO_F_KANAKANJI = 32, UO_F_HAN = 64 };\n")
        f.write(f"#define UO_LB_COUNT {len(LB)}\n")
        f.write(f"extern const uint16_t uo_brk_t1[], uo_brk_t2[];\nextern const {ltype} uo_brk_leaf[];\n")
        f.write("extern const struct uo_brk uo_brks[];\n")
        f.write("extern const int32_t uo_lb_rep[], uo_wb_rep[];   /* a code point of each class */\n")
        f.write(f"extern const {ltype} uo_brk_latin1[256];   /* the records of U+0000..U+00FF, directly */\n")
        f.write(f"#define UO_BRK(c) (&uo_brks[(c) < 256 ? uo_brk_latin1[c] : uo_brk_leaf[uo_brk_t2[uo_brk_t1[(c) >> {b1 + b2}] + "
                f"(((c) >> {b2}) & {(1 << b1) - 1})] + ((c) & {(1 << b2) - 1})]])\n")
    with open(os.path.join(out, "uo_brk_data.c"), "w") as f:
        f.write("/* made by gen_brk.py from the UCD: unicode.library's breaking tables.\n")
        f.write(" * " + ", ".join(ucd.used) + " */\n#include \"uo_brk_data.h\"\n\n")
        c_array(f, "uint16_t", "uo_brk_t1", t1)
        c_array(f, "uint16_t", "uo_brk_t2", t2)
        c_array(f, ltype, "uo_brk_leaf", leaf)
        c_array(f, ltype, "uo_brk_latin1", vals[:256])
        f.write(f"const struct uo_brk uo_brks[{len(recs)}] = {{\n")
        for r in recs:
            f.write("    {%d, %d, %d, %d, %d},\n" % r)
        f.write("};\n\n")
        f.write("const int32_t uo_lb_rep[] = {" + ", ".join("0x%X" % c for c in lbRep) + "};\n")
        f.write("const int32_t uo_wb_rep[] = {" + ", ".join("0x%X" % c for c in wbRep) + "};\n")
    total = size + 5 * len(recs) + 256 * (2 if ltype == "uint16_t" else 1)
    print(f"gen_brk.py: {len(recs)} break records; tables {total} bytes")


if __name__ == "__main__":
    main()
