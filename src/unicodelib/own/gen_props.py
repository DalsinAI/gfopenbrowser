#!/usr/bin/env python3
# unicode.library, our own code: makes the property and case tables from
# the Unicode Character Database at build time.
# Copyright (c) 2026 AmiCradle Team. MIT.
#
#   gen_props.py UCD_DIR ICU_INCLUDE OUT_DIR
#
# UCD_DIR      the UCD of the Unicode version ICU's headers name (17.0.0
#              for ICU 78), unpacked from UCD.zip; every file read is
#              checked against ucd.sha256 beside this script
# ICU_INCLUDE  ICU's public headers (unicode/uchar.h, uscript.h, utypes.h):
#              the programs call ICU's interface, so the numbers of its
#              enums (UProperty, UBlockCode, UScriptCode, ...) are ours too
# OUT_DIR      uo_data.c (the tables) and uo_data.h (what uo_*.c use)
#
# Per code point two tries (each block stored once): the property record
# (general category, bidi class, combining class, script, East Asian width,
# line break, vertical orientation, numeric type, the set of binary
# properties, and the index of the record of the rest: decomposition type,
# grapheme, word and sentence break, Indic categories, quick checks,
# normalization-inert and segment-starter flags, identifier types), and the
# case record (simple mappings as deltas, case-folding delta, flags). Age,
# numeric values and lead/trail combining classes are runs and lists.
# Besides UCD.zip's files: Unicode's emoji/emoji-sequences.txt and
# security/IdentifierType.txt of the same version (scripts/fetch-ucd.sh).
import hashlib
import os
import re
import sys

MAXCP = 0x110000
HERE = os.path.dirname(os.path.abspath(__file__))


def die(msg):
    sys.exit("gen_props.py: " + msg)


# ---- reading the UCD -----------------------------------------------------------

class UCD:
    def __init__(self, root):
        self.root = root
        self.sums = {}
        for line in open(os.path.join(HERE, "ucd.sha256")):
            line = line.split("#", 1)[0].split()
            if len(line) == 2:
                self.sums[line[1]] = line[0]
        self.used = []

    def lines(self, name):
        path = os.path.join(self.root, name)
        data = open(path, "rb").read()
        want = self.sums.get(name)
        if not want:
            die(f"{name} is not in ucd.sha256")
        if hashlib.sha256(data).hexdigest() != want:
            die(f"{path}: sha256 differs from ucd.sha256 (another Unicode version?)")
        self.used.append(name)
        return data.decode("utf-8").split("\n")

    def fields(self, name, missing=False):
        """Yields (first, last, [fields]) per data line; with missing=True the
        @missing lines too, first, as their defaults."""
        out, miss = [], []
        for line in self.lines(name):
            m = re.match(r"#\s*@missing:\s*(.*)", line)
            if m:
                miss.append(m.group(1))
                continue
            line = line.split("#", 1)[0].strip()
            if line:
                out.append(line)
        for line in (miss if missing else []) + out:
            f = [x.strip() for x in line.split(";")]
            r = f[0].split("..")
            yield int(r[0], 16), int(r[-1], 16), f[1:]


def ranges_to_array(ucd, name, default, conv=lambda v: v, field=0):
    a = [default] * MAXCP
    for first, last, f in ucd.fields(name, missing=True):
        v = conv(f[field])
        for c in range(first, last + 1):
            a[c] = v
    return a


# ---- ICU's enums from its headers ----------------------------------------------

def parse_enum(text, typename):
    m = re.search(r"typedef\s+enum\s+" + typename + r"\s*\{(.*?)\}\s*" + typename + r"\s*;", text, re.S)
    if not m:
        m = re.search(r"enum\s+" + typename + r"\s*\{(.*?)\}\s*;", text, re.S)
    if not m:
        die(f"no enum {typename} in ICU's headers")
    body = m.group(1)
    names, tags, value = {}, {}, -1
    for line in body.split("\n"):
        s = line.strip()
        if s.startswith("#"):
            continue
        mm = re.match(r"([A-Z][A-Z0-9_]*)\s*(?:=\s*([^,/]+?))?\s*,?\s*(/\*.*)?$", s)
        if not mm:
            continue
        name, expr, comment = mm.group(1), mm.group(2), mm.group(3) or ""
        if expr is None:
            value += 1
        else:
            expr = expr.strip()
            if re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", expr):
                value = int(expr, 0)
            elif expr in names:
                value = names[expr]
            elif re.fullmatch(r"([A-Z][A-Z0-9_]*)\s*\+\s*(\d+)", expr):
                a, b = re.fullmatch(r"([A-Z][A-Z0-9_]*)\s*\+\s*(\d+)", expr).groups()
                value = names[a] + int(b)
            else:
                die(f"{typename}: cannot read {name} = {expr}")
        names[name] = value
        t = re.match(r"/\*\s*\[?([A-Za-z0-9_]+)\]?\s*\*/", comment)
        if t:
            tags[name] = t.group(1)
    return names, tags


def norm(s):
    return re.sub(r"[-_ ]", "", s).lower()


# ---- the tries -------------------------------------------------------------------

def build_trie(values, name):
    """The smallest of a few shapes: t1[c >> (B1 + B2)], t2 with 2^B1 entries a
    block, leaves of 2^B2 values (8 bits when every value fits)."""
    best = None
    wide = max(values) > 0xFF
    for b1, b2 in ((5, 5), (5, 4), (6, 4), (4, 4), (4, 5), (6, 3), (5, 3)):
        leaves, leafIndex, mids, midIndex, t1 = [], {}, [], {}, []
        n2, n1 = 1 << b2, 1 << b1
        for hi in range(MAXCP >> (b1 + b2)):
            mid = []
            for m in range(n1):
                base = (hi << (b1 + b2)) | (m << b2)
                block = tuple(values[base:base + n2])
                if block not in leafIndex:
                    leafIndex[block] = len(leaves) * n2
                    leaves.append(block)
                mid.append(leafIndex[block])
            mid = tuple(mid)
            if mid not in midIndex:
                midIndex[mid] = len(mids) * n1
                mids.append(mid)
            t1.append(midIndex[mid])
        leaf = [v for b in leaves for v in b]
        t2 = [v for b in mids for v in b]
        if max(t2) > 0xFFFF or max(t1) > 0xFFFF:
            continue
        size = 2 * (len(t1) + len(t2)) + (2 if wide else 1) * len(leaf)
        if not best or size < best[0]:
            best = (size, b1, b2, t1, t2, leaf)
    if not best:
        die(f"trie {name} does not fit 16 bits")
    return best[1:] + ("uint16_t" if wide else "uint8_t", best[0])


def c_array(f, ctype, name, values, per=16):
    f.write(f"const {ctype} {name}[{len(values)}] = {{\n")
    for i in range(0, len(values), per):
        f.write("    " + ", ".join(str(v) for v in values[i:i + per]) + ",\n")
    f.write("};\n\n")


# ---- the properties ICU's uchar.cpp and uprops.cpp give besides those above ----

def enum_values(ucd, pva, uchar, typename, prefix, prop):
    """UCD value name of property prop (any alias) -> the number of ICU's
    enum typename, matched by the enum names (prefix stripped) and their
    [tag] comments against PropertyValueAliases.txt"""
    names, tags = parse_enum(uchar, typename)
    m = {}
    for n, v in names.items():
        if not n.startswith(prefix) or n.endswith("_COUNT") or n.endswith("_LIMIT"):
            continue
        keys = {norm(n[len(prefix):])}
        if n in tags:
            keys.add(norm(tags[n]))
        for row in pva[prop]:
            if keys & {norm(x) for x in row}:
                for x in row:
                    m.setdefault(norm(x), v)
                break
        else:
            die(f"{typename}: {n} is not a value of {prop} in PropertyValueAliases.txt")
    for row in pva[prop]:
        if not any(norm(x) in m for x in row):
            die(f"{prop}={row[0]} has no {typename}")
    return m


def prop_array(ucd, name, prop, default):
    """A property of a file with "cp; Property; Value" lines (and @missing)."""
    a = [default] * MAXCP
    for first, last, f in ucd.fields(name, missing=True):
        if len(f) >= 2 and norm(f[0]) == norm(prop):
            for c in range(first, last + 1):
                a[c] = f[1]
    return a


def more_props(ucd, pva, uchar, unorm2, gc, gcEnum, ccc, sets, aliases):
    out = {}

    def via_map(m, what):
        def conv(v):
            if norm(v) not in m:
                die(f"{what} value {v!r} has no ICU number")
            return m[norm(v)]
        return conv

    def arr(name, typename, prefix, prop, default):
        m = enum_values(ucd, pva, uchar, typename, prefix, prop)
        return ranges_to_array(ucd, name, m[norm(default)], via_map(m, prop))

    dt = arr("extracted/DerivedDecompositionType.txt", "UDecompositionType", "U_DT_", "dt", "None")
    hst = arr("HangulSyllableType.txt", "UHangulSyllableType", "U_HST_", "hst", "NA")
    gcb = arr("auxiliary/GraphemeBreakProperty.txt", "UGraphemeClusterBreak", "U_GCB_", "GCB", "XX")
    sb = arr("auxiliary/SentenceBreakProperty.txt", "USentenceBreak", "U_SB_", "SB", "XX")
    wb = arr("auxiliary/WordBreakProperty.txt", "UWordBreakValues", "U_WB_", "WB", "XX")
    inpc = arr("IndicPositionalCategory.txt", "UIndicPositionalCategory", "U_INPC_", "InPC", "NA")
    insc = arr("IndicSyllabicCategory.txt", "UIndicSyllabicCategory", "U_INSC_", "InSC", "Other")
    incbMap = enum_values(ucd, pva, uchar, "UIndicConjunctBreak", "U_INCB_", "InCB")
    incb = [incbMap[norm(v)] for v in prop_array(ucd, "DerivedCoreProperties.txt", "InCB", "None")]
    # quick checks: UNORM_NO, UNORM_YES, UNORM_MAYBE (unorm2.h)
    qcEnum, _ = parse_enum(unorm2, "UNormalizationCheckResult")
    qcOf = {"n": qcEnum["UNORM_NO"], "y": qcEnum["UNORM_YES"], "m": qcEnum["UNORM_MAYBE"],
            "no": qcEnum["UNORM_NO"], "yes": qcEnum["UNORM_YES"], "maybe": qcEnum["UNORM_MAYBE"]}
    qc = {}
    for p in ("NFD_QC", "NFKD_QC", "NFC_QC", "NFKC_QC"):
        qc[p] = [qcOf[norm(v)] for v in prop_array(ucd, "DerivedNormalizationProps.txt", p, "Y")]
    Y, M = qcEnum["UNORM_YES"], qcEnum["UNORM_MAYBE"]

    # decompositions (UnicodeData.txt), full and in canonical order
    raw, rawCompat = {}, {}
    for first, last, f in ucd.fields("UnicodeData.txt"):
        if f[4]:
            parts = f[4].split()
            if parts[0].startswith("<"):
                rawCompat[first] = [int(x, 16) for x in parts[1:]]
            else:
                raw[first] = [int(x, 16) for x in parts]
    SBASE, LBASE, VBASE, TBASE, NCOUNT, TCOUNT = 0xAC00, 0x1100, 0x1161, 0x11A7, 588, 28

    def hangul(c):
        s = c - SBASE
        if 0 <= s < 11172:
            d = [LBASE + s // NCOUNT, VBASE + (s % NCOUNT) // TCOUNT]
            if s % TCOUNT:
                d.append(TBASE + s % TCOUNT)
            return d
        return None

    def full(c, compat):
        h = hangul(c)
        if h:
            return h
        m = raw.get(c) or (compat and rawCompat.get(c))
        if not m:
            return [c]
        r = []
        for x in m:
            r += full(x, compat)
        return r

    def reorder(s):
        s = list(s)
        for i in range(1, len(s)):
            j = i
            while j > 0 and ccc[s[j]] and ccc[s[j - 1]] > ccc[s[j]]:
                s[j - 1], s[j] = s[j], s[j - 1]
                j -= 1
        return s

    nfd, nfkd = {}, {}
    for c in set(raw) | set(rawCompat) | set(range(SBASE, SBASE + 11172)):
        if c in raw or hangul(c):
            nfd[c] = reorder(full(c, False))
        nfkd[c] = reorder(full(c, True))
    # lead and trail combining classes (ICU's FCD values)
    lccc, tccc = list(ccc), list(ccc)
    for c, d in nfd.items():
        lccc[c], tccc[c] = ccc[d[0]], ccc[d[-1]]

    # canonical compositions: the pairs of two-character mappings that are
    # not excluded; Hangul composes by its own rules
    excl = sets[aliases.get("compex", "fullcompositionexclusion")]
    pairs = {}                          # lead -> {trail: composite}
    back = set()
    for c, m in raw.items():
        if len(m) == 2 and c not in excl:
            pairs.setdefault(m[0], {})[m[1]] = c
            back.add(m[1])

    def fwd(c):
        return c in pairs

    def jamoL(c):
        return 0x1100 <= c <= 0x1112

    def jamoV(c):
        return 0x1161 <= c <= 0x1175

    def jamo(c):
        return 0x1100 <= c <= 0x11FF

    def combines_with_cc_between(lead, low, high):
        if high - low >= 2:
            for t in pairs.get(lead, {}):
                if low < ccc[t] < high:
                    return True
        return False

    def boundary_after(buf):
        """ICU's gennorm2 mappingHasCompBoundaryAfter() for a round-trip mapping"""
        last = max((i for i, x in enumerate(buf) if ccc[x] == 0), default=-1)
        if last < 0:
            return False
        starter = buf[last]
        if last == 0 and starter in back:
            return False
        if jamoL(starter) or (jamoV(starter) and last > 0 and jamoL(buf[last - 1])):
            return last != len(buf) - 1
        i = last
        while i > 0 and ccc[buf[i - 1]] == 0 and not jamo(buf[i - 1]):
            starter = buf[i - 1]
            i -= 1
        if i == last and not fwd(starter):
            return True
        lead = starter                  # its composition pairs (may be none)
        prevCC = 0
        i += 1
        while i < len(buf):
            cc = ccc[buf[i]]
            if i > last and combines_with_cc_between(lead, prevCC, cc):
                return False
            x = buf[i]
            comp = pairs.get(lead, {}).get(x) if (prevCC < cc or prevCC == 0) and x in back else None
            if comp is not None:
                lead = comp
                if i >= last and not fwd(lead):
                    return True
            elif cc == 0:
                lead = x
                if i == last and not fwd(lead):
                    return True
                prevCC = 0
            else:
                prevCC = cc
            i += 1
        if prevCC == 0:
            return False
        return not combines_with_cc_between(lead, prevCC, 256)

    def comp_inert(c, q, decomp):
        if q[c] != Y or ccc[c] != 0:
            return False
        s = c - SBASE
        if 0 <= s < 11172:
            return s % TCOUNT != 0      # LVT yes, LV combines with a T
        if fwd(c) or jamoL(c):
            return False
        m = decomp.get(c)
        if m is None or m == [c]:
            return c not in back
        return c not in back and boundary_after(m)

    # segment starters (ICU's canonical iterator data)
    notStarter = set()
    for c in range(MAXCP):
        if ccc[c] != 0 or (qc["NFC_QC"][c] == M and c not in raw):
            notStarter.add(c)
    for c, d in nfd.items():
        if qc["NFC_QC"][c] == qcEnum["UNORM_NO"]:
            notStarter.update(d[1:])

    # identifier status and types (UTS #39 data)
    idtNames, _ = parse_enum(uchar, "UIdentifierType")
    idtMap = enum_values(ucd, pva, uchar, "UIdentifierType", "U_ID_TYPE_", "Identifier_Type") \
        if "Identifier_Type" in pva else {norm(n[10:]): v for n, v in idtNames.items() if n.startswith("U_ID_TYPE_")}
    idTypes = [1 << idtMap["notcharacter"]] * MAXCP
    for first, last, f in ucd.fields("security/IdentifierType.txt"):
        bits = 0
        for t in f[0].split():
            bits |= 1 << idtMap[norm(t)]
        for c in range(first, last + 1):
            idTypes[c] = bits

    # Basic_Emoji (and so RGI_Emoji) of single code points
    basicEmoji = set()
    for line in ucd.lines("emoji/emoji-sequences.txt"):
        f = [x.strip() for x in line.split("#", 1)[0].split(";")]
        if len(f) > 1 and f[1] == "Basic_Emoji" and " " not in f[0]:
            r = f[0].split("..")        # a sequence is a string, not a code point
            basicEmoji.update(range(int(r[0], 16), int(r[-1], 16) + 1))

    # age: major << 2 | minor, 0 unassigned (as ICU keeps it)
    age = [0] * MAXCP
    for first, last, f in ucd.fields("DerivedAge.txt"):
        a, b = f[0].split(".")
        if int(a) > 63 or int(b) > 3:
            die(f"age {f[0]} does not fit a byte")
        for c in range(first, last + 1):
            age[c] = int(a) << 2 | int(b)
    # numeric values
    numVals = []
    numIndex = {}
    numv = [0] * MAXCP                  # 0: none, else index + 1
    for first, last, f in ucd.fields("extracted/DerivedNumericValues.txt"):
        r = f[2].strip()
        if "/" in r:
            p, q = r.split("/")
            key = (int(p), int(q))
        else:
            key = (int(r), 1)
        if key not in numIndex:
            numIndex[key] = len(numVals)
            numVals.append(key)
        for c in range(first, last + 1):
            numv[c] = numIndex[key] + 1

    # Case_Sensitive (ICU's): in a case mapping or folding, either side
    caseSensitive = set()
    for first, last, f in ucd.fields("UnicodeData.txt"):
        for i in (11, 12, 13):
            if f[i] and int(f[i], 16) != first:
                caseSensitive.update((first, int(f[i], 16)))
    for first, _, f in ucd.fields("CaseFolding.txt"):
        to = [int(x, 16) for x in f[1].split()]
        caseSensitive.add(first)
        caseSensitive.update(to)
    for first, _, f in ucd.fields("SpecialCasing.txt"):
        if len(f) > 4 and f[3].strip():
            continue                    # conditional: language or context
        for i in range(3):
            to = [int(x, 16) for x in f[i].split()]
            if to != [first]:
                caseSensitive.add(first)
                caseSensitive.update(to)

    # records: per code point
    flagsOf = []
    for c in range(MAXCP):
        fl = 0
        if qc["NFD_QC"][c] == Y and ccc[c] == 0:
            fl |= 1
        if qc["NFKD_QC"][c] == Y and ccc[c] == 0:
            fl |= 2
        if comp_inert(c, qc["NFC_QC"], nfd):
            fl |= 4
        if comp_inert(c, qc["NFKC_QC"], nfkd):
            fl |= 8
        if c not in notStarter:
            fl |= 16
        if c in basicEmoji:
            fl |= 32
        if c in caseSensitive:
            fl |= 64
        flagsOf.append(fl)
    out.update(dt=dt, hst=hst, gcb=gcb, sb=sb, wb=wb, inpc=inpc, insc=insc, incb=incb, qc=qc,
               lccc=lccc, tccc=tccc, flags=flagsOf, idTypes=idTypes, age=age, numv=numv,
               numVals=numVals, qcEnum=qcEnum, idtMap=idtMap)
    return out


# ---- main ------------------------------------------------------------------------

def main():
    if len(sys.argv) != 4:
        die("usage: gen_props.py UCD_DIR ICU_INCLUDE OUT_DIR")
    ucd = UCD(sys.argv[1])
    inc, out = sys.argv[2], sys.argv[3]
    os.makedirs(out, exist_ok=True)
    uchar = open(os.path.join(inc, "unicode", "uchar.h")).read()
    uscript = open(os.path.join(inc, "unicode", "uscript.h")).read()
    utypes = open(os.path.join(inc, "unicode", "utypes.h")).read()

    # the Unicode version: ICU's and the UCD's must be the same
    icuver = re.search(r'#define U_UNICODE_VERSION "([0-9.]+)"', uchar).group(1)
    readme = "\n".join(ucd.lines("ReadMe.txt"))
    m = re.search(r"Version (\d+\.\d+\.\d+)|/Public/(\d+\.\d+\.\d+)/", readme)
    derived = "\n".join(ucd.lines("DerivedAge.txt")[:3])
    ucdver = re.search(r"DerivedAge-(\d+\.\d+\.\d+)\.txt", derived).group(1)
    if not ucdver.startswith(icuver + "."):
        die(f"the UCD is {ucdver}, ICU's headers want {icuver}")

    prop, _ = parse_enum(uchar, "UProperty")
    gcEnum, _ = parse_enum(uchar, "UCharCategory")
    dirEnum, _ = parse_enum(uchar, "UCharDirection")
    blockEnum, blockTags = parse_enum(uchar, "UBlockCode")
    eaEnum, eaTags = parse_enum(uchar, "UEastAsianWidth")
    lbEnum, lbTags = parse_enum(uchar, "ULineBreak")
    ntEnum, ntTags = parse_enum(uchar, "UNumericType")
    voEnum, _ = parse_enum(uchar, "UVerticalOrientation")
    scEnum, scTags = parse_enum(uscript, "UScriptCode")
    errEnum, _ = parse_enum(utypes, "UErrorCode")
    jtEnum, jtTags = parse_enum(uchar, "UJoiningType")
    jgEnum, _ = parse_enum(uchar, "UJoiningGroup")
    bptEnum, _ = parse_enum(uchar, "UBidiPairedBracketType")

    # property value aliases: short and long names per property
    pva = {}
    for line in ucd.lines("PropertyValueAliases.txt"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(";")]
        pva.setdefault(f[0], []).append(f[1:])

    def value_map(p, icuEnum, pairs):
        """UCD value name (short or long, any) -> ICU enum number"""
        m = {}
        for icuName, ucdShort in pairs:
            if icuName not in icuEnum:
                die(f"{icuName} not in ICU's headers")
            for row in pva[p]:
                if row[0] == ucdShort:
                    for n in row:
                        m[norm(n)] = icuEnum[icuName]
                    break
            else:
                die(f"{p}={ucdShort} not in PropertyValueAliases.txt")
        return m

    GC = [("U_UNASSIGNED", "Cn"), ("U_UPPERCASE_LETTER", "Lu"), ("U_LOWERCASE_LETTER", "Ll"),
          ("U_TITLECASE_LETTER", "Lt"), ("U_MODIFIER_LETTER", "Lm"), ("U_OTHER_LETTER", "Lo"),
          ("U_NON_SPACING_MARK", "Mn"), ("U_ENCLOSING_MARK", "Me"), ("U_COMBINING_SPACING_MARK", "Mc"),
          ("U_DECIMAL_DIGIT_NUMBER", "Nd"), ("U_LETTER_NUMBER", "Nl"), ("U_OTHER_NUMBER", "No"),
          ("U_SPACE_SEPARATOR", "Zs"), ("U_LINE_SEPARATOR", "Zl"), ("U_PARAGRAPH_SEPARATOR", "Zp"),
          ("U_CONTROL_CHAR", "Cc"), ("U_FORMAT_CHAR", "Cf"), ("U_PRIVATE_USE_CHAR", "Co"),
          ("U_SURROGATE", "Cs"), ("U_DASH_PUNCTUATION", "Pd"), ("U_START_PUNCTUATION", "Ps"),
          ("U_END_PUNCTUATION", "Pe"), ("U_CONNECTOR_PUNCTUATION", "Pc"), ("U_OTHER_PUNCTUATION", "Po"),
          ("U_MATH_SYMBOL", "Sm"), ("U_CURRENCY_SYMBOL", "Sc"), ("U_MODIFIER_SYMBOL", "Sk"),
          ("U_OTHER_SYMBOL", "So"), ("U_INITIAL_PUNCTUATION", "Pi"), ("U_FINAL_PUNCTUATION", "Pf")]
    BC = [("U_LEFT_TO_RIGHT", "L"), ("U_RIGHT_TO_LEFT", "R"), ("U_EUROPEAN_NUMBER", "EN"),
          ("U_EUROPEAN_NUMBER_SEPARATOR", "ES"), ("U_EUROPEAN_NUMBER_TERMINATOR", "ET"),
          ("U_ARABIC_NUMBER", "AN"), ("U_COMMON_NUMBER_SEPARATOR", "CS"), ("U_BLOCK_SEPARATOR", "B"),
          ("U_SEGMENT_SEPARATOR", "S"), ("U_WHITE_SPACE_NEUTRAL", "WS"), ("U_OTHER_NEUTRAL", "ON"),
          ("U_LEFT_TO_RIGHT_EMBEDDING", "LRE"), ("U_LEFT_TO_RIGHT_OVERRIDE", "LRO"),
          ("U_RIGHT_TO_LEFT_ARABIC", "AL"), ("U_RIGHT_TO_LEFT_EMBEDDING", "RLE"),
          ("U_RIGHT_TO_LEFT_OVERRIDE", "RLO"), ("U_POP_DIRECTIONAL_FORMAT", "PDF"),
          ("U_DIR_NON_SPACING_MARK", "NSM"), ("U_BOUNDARY_NEUTRAL", "BN"),
          ("U_FIRST_STRONG_ISOLATE", "FSI"), ("U_LEFT_TO_RIGHT_ISOLATE", "LRI"),
          ("U_RIGHT_TO_LEFT_ISOLATE", "RLI"), ("U_POP_DIRECTIONAL_ISOLATE", "PDI")]
    VO = [("U_VO_ROTATED", "R"), ("U_VO_TRANSFORMED_ROTATED", "Tr"),
          ("U_VO_TRANSFORMED_UPRIGHT", "Tu"), ("U_VO_UPRIGHT", "U")]
    gcMap = value_map("gc", gcEnum, GC)
    bcMap = value_map("bc", dirEnum, BC)
    voMap = value_map("vo", voEnum, VO)
    eaMap = value_map("ea", eaEnum, [(n, t) for n, t in eaTags.items() if not n.endswith("COUNT")])
    lbMap = value_map("lb", lbEnum, [(n, t) for n, t in lbTags.items()])
    ntMap = value_map("nt", ntEnum, [(n, t.capitalize() if t != "None" else t) for n, t in ntTags.items()])
    # scripts: by ISO 15924 code, the header's comment
    scByCode = {}
    for n, v in scEnum.items():
        if n in scTags and n not in ("USCRIPT_INVALID_CODE", "USCRIPT_CODE_LIMIT") and v >= 0:
            scByCode.setdefault(scTags[n], v)
    scMap = {}
    for code, v in scByCode.items():
        scMap[norm(code)] = v
    for row in pva["sc"]:
        code = row[0]
        if code not in scByCode:
            die(f"script {code} has no UScriptCode")
        for n in row:
            scMap[norm(n)] = scByCode[code]

    def via(m, what):
        def conv(v):
            k = norm(v)
            if k not in m:
                die(f"{what} value {v!r} has no ICU number")
            return m[k]
        return conv

    gc = ranges_to_array(ucd, "extracted/DerivedGeneralCategory.txt", 0, via(gcMap, "gc"))
    bc = ranges_to_array(ucd, "extracted/DerivedBidiClass.txt", 0, via(bcMap, "bc"))
    cccNames = {norm(n): int(row[0]) for row in pva["ccc"] for n in row}
    ccc = ranges_to_array(ucd, "extracted/DerivedCombiningClass.txt", 0, lambda v: int(v) if v.isdigit() else cccNames[norm(v)])
    ea = ranges_to_array(ucd, "EastAsianWidth.txt", 0, via(eaMap, "ea"))
    lb = ranges_to_array(ucd, "LineBreak.txt", 0, via(lbMap, "lb"))
    vo = ranges_to_array(ucd, "VerticalOrientation.txt", 0, via(voMap, "vo"))
    nt = ranges_to_array(ucd, "extracted/DerivedNumericType.txt", 0, via(ntMap, "nt"))
    sc = ranges_to_array(ucd, "Scripts.txt", 0, via(scMap, "sc"))
    for a, bits, what in ((ea, 3, "ea"), (vo, 2, "vo"), (nt, 2, "nt"), (lb, 6, "lb")):
        if max(a) >= 1 << bits:
            die(f"{what} needs more than {bits} bits")
    if max(sc) > 255:
        die("a script number above 255")

    # ---- binary properties
    binFiles = ["PropList.txt", "DerivedCoreProperties.txt", "emoji/emoji-data.txt",
                "DerivedNormalizationProps.txt", "extracted/DerivedBinaryProperties.txt"]
    sets = {}
    for name in binFiles:
        for first, last, f in ucd.fields(name):
            if len(f) != 1:
                continue        # not a binary property (InCB, NFC_QC, ...)
            s = sets.setdefault(norm(f[0]), set())
            s.update(range(first, last + 1))
    aliases = {}
    for line in ucd.lines("PropertyAliases.txt"):
        line = line.split("#", 1)[0].strip()
        if line:
            f = [norm(x) for x in line.split(";")]
            for x in f:
                aliases[x] = f[1]
    binLimit = prop["UCHAR_BINARY_LIMIT"]
    binProps = []          # (UProperty number, ICU name), in bit order
    binNames = {v: n for n, v in prop.items() if n.startswith("UCHAR_") and v < binLimit
                and n not in ("UCHAR_BINARY_START",)}
    for v in range(binLimit):
        n = binNames.get(v)
        if not n:
            continue
        key = aliases.get(norm(n[6:]), norm(n[6:]))
        if key in sets:
            binProps.append((v, n, sets[key]))
    for need in ("UCHAR_BIDI_MIRRORED", "UCHAR_BIDI_CONTROL", "UCHAR_JOIN_CONTROL"):
        if prop[need] not in [v for v, _, _ in binProps]:
            die(f"{need} must be ours: uo_icu.c answers ICU's ubidi_props with it")
    if len(binProps) > 96:
        die("more than 96 binary properties")
    binIndex, binSets = {}, []
    binv = [0] * MAXCP
    for c in range(MAXCP):
        bits = 0
        for i, (_, _, s) in enumerate(binProps):
            if c in s:
                bits |= 1 << i
        if bits not in binIndex:
            binIndex[bits] = len(binSets)
            binSets.append(bits)
        binv[c] = binIndex[bits]

    # ---- case
    upper, lower, title = list(range(MAXCP)), list(range(MAXCP)), list(range(MAXCP))
    for first, last, f in ucd.fields("UnicodeData.txt"):
        c = first
        if f[11]:
            upper[c] = int(f[11], 16)
        if f[12]:
            lower[c] = int(f[12], 16)
        title[c] = int(f[13], 16) if f[13] else upper[c]
    fold = list(range(MAXCP))
    fullFold, turkic = {}, {}
    for first, _, f in ucd.fields("CaseFolding.txt"):
        status, to = f[0], [int(x, 16) for x in f[1].split()]
        if status in ("C", "S"):
            fold[first] = to[0]
        if status == "F":
            fullFold[first] = to
        if status == "T":
            turkic[first] = to[0]
    if turkic != {0x49: 0x131, 0x130: 0x69}:
        die(f"CaseFolding's Turkic mappings changed: {turkic}")
    special = {}            # cp -> (lower, title, upper), unconditional
    conditional = []
    for first, _, f in ucd.fields("SpecialCasing.txt"):
        maps = [[int(x, 16) for x in f[i].split()] for i in range(3)]
        cond = f[3].strip() if len(f) > 4 and f[3].strip() else ""
        if cond:
            conditional.append((first, cond))
        else:
            special[first] = maps
    # the conditional mappings uo_case.c knows (SpecialCasing.txt, Unicode 17)
    known = {(0x3A3, "Final_Sigma"), (0x307, "lt After_Soft_Dotted"), (0x49, "lt More_Above"),
             (0x4A, "lt More_Above"), (0x12E, "lt More_Above"), (0xCC, "lt"), (0xCD, "lt"),
             (0x128, "lt"), (0x130, "tr"), (0x130, "az"), (0x307, "tr After_I"), (0x307, "az After_I"),
             (0x49, "tr Not_Before_Dot"), (0x49, "az Not_Before_Dot"), (0x69, "tr"), (0x69, "az")}
    if set(conditional) != known:
        die(f"SpecialCasing's conditional mappings changed: {sorted(set(conditional) ^ known)}")
    cased = sets[aliases["cased"]]
    ignorable = sets[aliases["caseignorable"]]
    softDotted = sets[aliases["softdotted"]]
    # flags: 1 cased, 2 case-ignorable, 4 soft-dotted, 8 ccc 230 (above),
    # 16 other ccc != 0, 32 has a full mapping (uo_special), 64 lowercase,
    # 128 uppercase or titlecase (ICU's case type for u_strToTitle later)
    lowerSet = sets[aliases["lowercase"]]
    upperSet = sets[aliases["uppercase"]]
    caseIndex, caseRecs = {(0, 0, 0, 0, 0): 0}, [(0, 0, 0, 0, 0)]
    casev = [0] * MAXCP
    fullList = sorted(set(special) | set(fullFold))
    for c in range(MAXCP):
        fl = 0
        if c in cased:
            fl |= 1
        if c in ignorable:
            fl |= 2
        if c in softDotted:
            fl |= 4
        if ccc[c] == 230:
            fl |= 8
        elif ccc[c] != 0:
            fl |= 16
        if c in special or c in fullFold:
            fl |= 32
        if c in lowerSet:
            fl |= 64
        if c in upperSet or gc[c] == gcEnum["U_TITLECASE_LETTER"]:
            fl |= 128
        r = (upper[c] - c, lower[c] - c, title[c] - c, fold[c] - c, fl)
        if r not in caseIndex:
            caseIndex[r] = len(caseRecs)
            caseRecs.append(r)
        casev[c] = caseIndex[r]
    # full mappings: per cp, lower/title/upper/fold strings (empty = the simple one)
    fullStr, fullTab = [], []

    def put(seq):
        if seq is None:
            return 0xFFFF
        pos = len(fullStr)
        for cp in seq:
            if cp >= 0x10000:
                cp -= 0x10000
                fullStr.extend((0xD800 | cp >> 10, 0xDC00 | cp & 0x3FF))
            else:
                fullStr.append(cp)
        return (pos, len(fullStr) - pos)
    for c in fullList:
        s = special.get(c)
        entry = [c]
        for i in range(3):
            if s:
                entry.append(put(s[i]))
            else:
                entry.append(None)
        entry.append(put(fullFold[c]) if c in fullFold else None)
        fullTab.append(entry)

    # ---- blocks, script extensions, mirroring
    blockByStart = {int(t, 16): v for n, v in blockEnum.items() for t in [blockTags.get(n, "")]
                    if re.fullmatch(r"[0-9A-F]{4,6}", t)}
    blocks = []
    for first, last, f in ucd.fields("Blocks.txt"):
        if first not in blockByStart:
            die(f"block {f[0]} at {first:04X} has no UBlockCode")
        blocks.append((first, last, blockByStart[first]))
    scx = []
    for first, last, f in ucd.fields("ScriptExtensions.txt"):
        codes = sorted(scByCode[x] for x in f[0].split())
        scx.append((first, last, codes))
    scx.sort()
    mirror = []
    for first, _, f in ucd.fields("BidiMirroring.txt"):
        mirror.append((first, int(f[0], 16)))
    mirror.sort()
    # joining (ubidi's): type and group per code point, as ranges
    jtMap = value_map("jt", jtEnum, [(n, t) for n, t in jtTags.items() if t.isalpha()])
    jgMap = {}
    for n, v in jgEnum.items():
        if n != "U_JG_COUNT":
            jgMap[norm(n[5:])] = v
    for row in pva["jg"]:
        for n in row:
            if norm(n) in jgMap:
                for m in row:
                    jgMap[norm(m)] = jgMap[norm(n)]
                break
        else:
            die(f"jg={row[0]} has no UJoiningGroup")
    jt = ranges_to_array(ucd, "extracted/DerivedJoiningType.txt", jtEnum["U_JT_NON_JOINING"], via(jtMap, "jt"))
    jg = ranges_to_array(ucd, "extracted/DerivedJoiningGroup.txt", jgEnum["U_JG_NO_JOINING_GROUP"], via(jgMap, "jg"))
    if max(jt) > 15 or max(jg) > 0xFFF:
        die("joining type or group out of range")
    joining = []
    for c in range(MAXCP):
        v = jt[c] | jg[c] << 4
        if joining and joining[-1][1] == c - 1 and joining[-1][2] == v:
            joining[-1][1] = c
        elif v:
            joining.append([c, c, v])
    # paired brackets: the pair and o/c
    bptOf = {"o": bptEnum["U_BPT_OPEN"], "c": bptEnum["U_BPT_CLOSE"]}
    brackets = []
    for first, _, f in ucd.fields("BidiBrackets.txt"):
        brackets.append((first, int(f[0], 16), bptOf[f[1].strip()]))
    brackets.sort()
    bidiMax = (max(bc), max(jg), max(jt), max(b[2] for b in brackets))
    # where any bidi property changes: ICU's ubidi_addPropertyStarts
    # (characterproperties.cpp builds its property sets on these)
    bidiBin = [sets.get(aliases.get(norm(n), norm(n)), set()) for n in ("Bidi_M", "Bidi_C", "Join_C")]
    bpt = [0] * MAXCP
    for c, _, t in brackets:
        bpt[c] = t
    bidiStarts, prevKey = [], None
    for c in range(MAXCP):
        key = (bc[c], jt[c], jg[c], bpt[c], c in bidiBin[0], c in bidiBin[1], c in bidiBin[2])
        if key != prevKey:
            bidiStarts.append(c)
            prevKey = key
    for c, _ in mirror:
        bidiStarts += [c, c + 1]
    bidiStarts = sorted(set(bidiStarts))
    # scripts written right to left: most of their letters are R or AL
    rtl = set()
    count = {}
    for c in range(MAXCP):
        if gc[c] in (gcEnum["U_OTHER_LETTER"], gcEnum["U_LOWERCASE_LETTER"], gcEnum["U_UPPERCASE_LETTER"],
                     gcEnum["U_MODIFIER_LETTER"], gcEnum["U_TITLECASE_LETTER"]):
            k = count.setdefault(sc[c], [0, 0])
            k[0] += 1
            if bc[c] in (dirEnum["U_RIGHT_TO_LEFT"], dirEnum["U_RIGHT_TO_LEFT_ARABIC"]):
                k[1] += 1
    for s, (n, r) in count.items():
        if r * 2 > n:
            rtl.add(s)
    scCount = scEnum["USCRIPT_CODE_LIMIT"]
    shortName = {}
    for row in pva["sc"]:
        shortName[scByCode[row[0]]] = row[0]
    scLong = {}
    for row in pva["sc"]:
        scLong[scByCode[row[0]]] = row

    scNames = {(norm(n), n, s) for s, row in scLong.items() for n in row}
    scNames |= {(norm(code), code, v) for code, v in scByCode.items() if v not in scLong}
    scNames = sorted(scNames)

    # error names
    errNames = {}
    for n, v in errEnum.items():
        # range markers are not names (ICU's u_errorName says BOGUS for them);
        # of two names for one code ICU gives the later (U_STRINGPREP_*),
        # but never the deprecated misspelling (U_MULTIPLE_DECIMAL_SEPERATORS)
        if re.search(r"(ERROR|WARNING)_(START|LIMIT)$", n) or n == "U_ERROR_LIMIT":
            continue
        if "SEPERATOR" in n and v in errNames:
            continue
        errNames[v] = n

    # ---- the rest of ICU's properties, and one record per code point for all
    unorm2 = open(os.path.join(inc, "unicode", "unorm2.h")).read()
    mp = more_props(ucd, pva, uchar, unorm2, gc, gcEnum, ccc, sets, aliases)
    qc = mp["qc"]
    idtList, idtIndex = [], {}
    moreIndex, moreRecs = {}, []
    recIndex, recs = {}, []
    props = [0] * MAXCP
    for c in range(MAXCP):
        t = mp["idTypes"][c]
        if t not in idtIndex:
            idtIndex[t] = len(idtList)
            idtList.append(t)
        m = (mp["dt"][c], mp["gcb"][c], mp["sb"][c], mp["wb"][c], mp["inpc"][c], mp["insc"][c],
             mp["incb"][c],
             qc["NFD_QC"][c] | qc["NFKD_QC"][c] << 1 | qc["NFC_QC"][c] << 2 | qc["NFKC_QC"][c] << 4,
             mp["flags"][c], idtIndex[t])
        if m not in moreIndex:
            moreIndex[m] = len(moreRecs)
            moreRecs.append(m)
        r = (gc[c], bc[c], ccc[c], sc[c], ea[c] | nt[c] << 3 | vo[c] << 5, lb[c], binv[c], moreIndex[m])
        if r not in recIndex:
            recIndex[r] = len(recs)
            recs.append(r)
        props[c] = recIndex[r]
    if len(binSets) > 256 or len(moreRecs) > 0xFFFF or max(max(m) for m in moreRecs) > 255:
        die("a property record field does not fit")
    # Hangul_Syllable_Type: ICU's rule from the grapheme cluster break, checked
    gcbEnum, _ = parse_enum(uchar, "UGraphemeClusterBreak")
    hstEnum, _ = parse_enum(uchar, "UHangulSyllableType")
    gcbToHst = {gcbEnum["U_GCB_L"]: hstEnum["U_HST_LEADING_JAMO"], gcbEnum["U_GCB_V"]: hstEnum["U_HST_VOWEL_JAMO"],
                gcbEnum["U_GCB_T"]: hstEnum["U_HST_TRAILING_JAMO"], gcbEnum["U_GCB_LV"]: hstEnum["U_HST_LV_SYLLABLE"],
                gcbEnum["U_GCB_LVT"]: hstEnum["U_HST_LVT_SYLLABLE"]}
    for c in range(MAXCP):
        h = gcbToHst.get(mp["gcb"][c], 0) if c <= 0xFFFF else 0
        if h != mp["hst"][c]:
            die(f"U+{c:04X}: Hangul_Syllable_Type is not ICU's rule from Grapheme_Cluster_Break")
    # age as runs: start << 8 | major << 4 | minor
    ageRuns = []
    for c in range(MAXCP):
        if c == 0 or mp["age"][c] != mp["age"][c - 1]:
            ageRuns.append(c << 8 | mp["age"][c])
    # numeric values: runs of one value, or of integers counting up by one
    numVals, numIdx = [], {}
    numRuns = []                     # [first, count, step, value index]
    vals = mp["numVals"]
    for c in range(MAXCP):
        v = mp["numv"][c]
        if not v:
            continue
        p, q = vals[v - 1]
        if numRuns:
            r = numRuns[-1]
            p0, q0 = numVals[r[3]]
            if r[0] + r[1] == c and r[1] < 255:
                if r[2] == 0 and (p, q) == (p0, q0):
                    r[1] += 1           # the same value
                    continue
                if q == q0 == 1 and p == p0 + r[1] and (r[2] == 1 or r[1] == 1):
                    r[1] += 1           # counting up: 0, 1, 2, ...
                    r[2] = 1
                    continue
        if (p, q) not in numIdx:
            numIdx[(p, q)] = len(numVals)
            numVals.append((p, q))
        numRuns.append([c, 1, 0, numIdx[(p, q)]])
    # lead and trail combining classes where they are not ccc
    fcd = [(c, mp["lccc"][c], mp["tccc"][c]) for c in range(MAXCP)
           if mp["lccc"][c] != ccc[c] or mp["tccc"][c] != ccc[c]]
    # the code points uchar.cpp tests by number (u_isblank, u_isspace, u_digit,
    # u_isIDIgnorable, ...): where ICU's uchar_addPropertyStarts adds a start
    # besides its tries' (uo_props.c adds the tries' own)
    hardStarts = [9, 10, 0xD + 1, 0x1C, 0x1F + 1, 0x85, 0x86, 0x7F, 0x200A, 0x200F + 1, 0x206A, 0x206F + 1,
                  0xFEFF, 0xFF00, 0xA0, 0xA1, 0x2007, 0x2008, 0x202F, 0x2030, 0x61, 0x7B, 0x41, 0x5B,
                  0xFF41, 0xFF5B, 0xFF21, 0xFF3B, 0x67, 0x47, 0xFF47, 0xFF27, 0x2060, 0xFFF0, 0xFFFB + 1,
                  0xE0000, 0xE0FFF + 1, 0x34F, 0x350, 0x2FFE, 0x3000]
    idTypesOut = idtList
    maxes = {"BLOCK": max(b[2] for b in blocks), "SC": scCount - 1, "EA": max(ea), "LB": max(lb), "VO": max(vo),
             "DT": max(m[0] for m in moreRecs), "GCB": max(m[1] for m in moreRecs), "SB": max(m[2] for m in moreRecs),
             "WB": max(m[3] for m in moreRecs), "INPC": max(m[4] for m in moreRecs),
             "INSC": max(m[5] for m in moreRecs), "INCB": max(m[6] for m in moreRecs)}

    # ---- write
    tries = {}
    with open(os.path.join(out, "uo_data.h"), "w") as f:
        f.write("/* made by gen_props.py from the UCD: unicode.library's own tables */\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f'#define UO_UNICODE_VERSION "{ucdver}"\n')
        f.write("enum { UO_UNICODE_MAJOR = %s, UO_UNICODE_MINOR = %s, UO_UNICODE_MICRO = %s };\n" % tuple(ucdver.split(".")))
        f.write("struct uo_prop { uint8_t gc, bc, ccc, sc, ea_nt_vo, lb, bin; uint16_t more; };\n")
        f.write("struct uo_more { uint8_t dt, gcb, sb, wb, inpc, insc, incb, qc, flags, idt; };\n")
        f.write("/* qc: NFD | NFKD << 1 | NFC << 2 | NFKC << 4; flags: */\n")
        f.write("enum { UO_NFD_INERT = 1, UO_NFKD_INERT = 2, UO_NFC_INERT = 4, UO_NFKC_INERT = 8,\n"
                "       UO_SEGMENT_STARTER = 16, UO_BASIC_EMOJI = 32, UO_CASE_SENSITIVE = 64 };\n")
        f.write("struct uo_num { int32_t first; uint8_t count, step; uint16_t value; };\n")
        f.write("struct uo_case { int32_t upper, lower, title, fold; uint8_t flags; };\n")
        f.write("struct uo_full { int32_t c; uint16_t s[4][2]; };   /* start, length; 0xFFFF: none */\n")
        f.write("struct uo_range { int32_t first, last; int32_t value; };\n")
        f.write("enum { UO_CASED = 1, UO_IGNORABLE = 2, UO_SOFT_DOTTED = 4, UO_ABOVE = 8, UO_OTHER_ACCENT = 16,\n"
                "       UO_FULL = 32, UO_LOWER = 64, UO_UPPER_OR_TITLE = 128 };\n")
        f.write("enum { UO_FULL_LOWER, UO_FULL_TITLE, UO_FULL_UPPER, UO_FULL_FOLD };\n")
        for name, vals in (("props", props), ("case", casev)):
            tries[name] = build_trie(vals, name)
            b1, b2, _, _, _, ltype, _ = tries[name]
            f.write(f"#define UO_{name.upper()}_B1 {b1}\n#define UO_{name.upper()}_B2 {b2}\n")
            f.write(f"extern const uint16_t uo_{name}_t1[], uo_{name}_t2[];\nextern const {ltype} uo_{name}_leaf[];\n")
            f.write(f"#define UO_{name.upper()}(c) uo_{name}_leaf[uo_{name}_t2[uo_{name}_t1[(c) >> {b1 + b2}] + (((c) >> {b2}) & {(1 << b1) - 1})] + ((c) & {(1 << b2) - 1})]\n")
        f.write("extern const struct uo_prop uo_props[];\nextern const uint32_t uo_bin_sets[][3];\n")
        f.write("extern const struct uo_more uo_more[];\nextern const uint16_t uo_idtypes[];   /* 1 << UIdentifierType */\n")
        f.write(f"#define UO_AGE_RUNS {len(ageRuns)}\nextern const uint32_t uo_age_runs[];   /* start << 8 | major << 4 | minor */\n")
        f.write(f"#define UO_NUM_RUNS {len(numRuns)}\nextern const struct uo_num uo_num_runs[];\nextern const double uo_num_values[];\n")
        f.write(f"#define UO_FCD_COUNT {len(fcd)}\n/* lead and trail ccc where they are not ccc: c << 8 | lead, and trail */\n"
                "extern const uint32_t uo_fcd_lead[];\nextern const uint8_t uo_fcd_trail[];\n")
        f.write(f"#define UO_HARD_STARTS {len(hardStarts)}\nextern const int32_t uo_hard_starts[];\n")
        f.write("enum { " + ", ".join(f"UO_MAX_{k} = {v}" for k, v in maxes.items()) + " };\n")
        f.write("extern const struct uo_case uo_cases[];\nextern const struct uo_full uo_full[];\n")
        f.write(f"#define UO_FULL_COUNT {len(fullTab)}\nextern const uint16_t uo_full_str[];\n")
        f.write(f"#define UO_BIN_COUNT {len(binProps)}\nextern const int16_t uo_bin_props[];   /* bit -> UProperty */\n")
        f.write(f"extern const int8_t uo_bin_bit[{binLimit}];   /* UProperty -> bit, -1: not ours */\n")
        f.write(f"#define UO_BLOCK_COUNT {len(blocks)}\nextern const struct uo_range uo_blocks[];\n")
        f.write(f"#define UO_SCX_COUNT {len(scx)}\nextern const struct uo_range uo_scx[];   /* value: index into uo_scx_sets */\n")
        f.write("extern const uint8_t uo_scx_sets[];   /* count, then the scripts */\n")
        f.write(f"#define UO_MIRROR_COUNT {len(mirror)}\nextern const int32_t uo_mirror[][2];\n")
        f.write(f"#define UO_JOINING_COUNT {len(joining)}\nextern const struct uo_range uo_joining[];   /* type | group << 4 */\n")
        f.write(f"#define UO_BRACKET_COUNT {len(brackets)}\nextern const int32_t uo_brackets[][3];   /* c, pair, type */\n")
        f.write("enum { UO_MAX_BC = %d, UO_MAX_JG = %d, UO_MAX_JT = %d, UO_MAX_BPT = %d };\n" % bidiMax)
        f.write(f"#define UO_BIDI_STARTS {len(bidiStarts)}\nextern const int32_t uo_bidi_starts[];\n")
        f.write(f"#define UO_SCRIPT_COUNT {scCount}\nextern const char *const uo_script_short[];\n")
        f.write("extern const uint8_t uo_script_rtl[];\n")
        f.write("struct uo_scname { const char *name; uint8_t sc; };   /* sorted by the loose name */\n")
        f.write(f"#define UO_SCNAME_COUNT {len(scNames)}\nextern const struct uo_scname uo_script_names[];\n")
        f.write("struct uo_err { int32_t code; const char *name; };\n")
        f.write(f"#define UO_ERR_COUNT {len(errNames)}\nextern const struct uo_err uo_errors[];\n")
    sizes = {}
    with open(os.path.join(out, "uo_data.c"), "w") as f:
        f.write("/* made by gen_props.py from the UCD " + ucdver + ": unicode.library's own tables.\n")
        f.write(" * " + ", ".join(ucd.used) + " */\n#include \"uo_data.h\"\n\n")
        for name in ("props", "case"):
            b1, b2, t1, t2, leaf, ltype, size = tries[name]
            c_array(f, "uint16_t", f"uo_{name}_t1", t1)
            c_array(f, "uint16_t", f"uo_{name}_t2", t2)
            c_array(f, ltype, f"uo_{name}_leaf", leaf)
            sizes[name] = size
        f.write(f"const struct uo_prop uo_props[{len(recs)}] = {{\n")
        for r in recs:
            f.write("    {%d, %d, %d, %d, %d, %d, %d, %d},\n" % r)
        f.write("};\n\n")
        sizes["props"] += 10 * len(recs)
        sizes["bin"] = 0
        f.write(f"const struct uo_more uo_more[{len(moreRecs)}] = {{\n")
        for r in moreRecs:
            f.write("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d},\n" % r)
        f.write("};\n\n")
        c_array(f, "uint16_t", "uo_idtypes", idTypesOut)
        c_array(f, "uint32_t", "uo_age_runs", ageRuns, per=8)
        f.write(f"const struct uo_num uo_num_runs[{len(numRuns)}] = {{\n")
        for r in numRuns:
            f.write("    {0x%X, %d, %d, %d},\n" % tuple(r))
        f.write("};\n\n")
        f.write(f"const double uo_num_values[{len(numVals)}] = {{\n")
        for p_, q_ in numVals:
            f.write(f"    {p_}.0 / {q_}.0,\n")
        f.write("};\n\n")
        c_array(f, "uint32_t", "uo_fcd_lead", [c << 8 | l for c, l, _ in fcd], per=8)
        c_array(f, "uint8_t", "uo_fcd_trail", [t for _, _, t in fcd], per=16)
        c_array(f, "int32_t", "uo_hard_starts", hardStarts, per=8)
        sizes["more"] = 10 * len(moreRecs) + 2 * len(idTypesOut) + 4 * len(ageRuns) + 8 * len(numRuns) \
            + 8 * len(numVals) + 5 * len(fcd) + 4 * len(hardStarts)
        f.write(f"const uint32_t uo_bin_sets[{len(binSets)}][3] = {{\n")
        for b in binSets:
            f.write("    {0x%x, 0x%x, 0x%x},\n" % (b & 0xFFFFFFFF, b >> 32 & 0xFFFFFFFF, b >> 64 & 0xFFFFFFFF))
        f.write("};\n\n")
        sizes["bin"] += 12 * len(binSets)
        f.write("const int16_t uo_bin_props[] = {" + ", ".join(str(v) for v, _, _ in binProps) + "};\n")
        bit = [-1] * binLimit
        for i, (v, _, _) in enumerate(binProps):
            bit[v] = i
        f.write(f"const int8_t uo_bin_bit[{binLimit}] = {{" + ", ".join(map(str, bit)) + "};\n")
        f.write("/* " + " ".join(n[6:] for _, n, _ in binProps) + " */\n\n")
        f.write(f"const struct uo_case uo_cases[{len(caseRecs)}] = {{\n")
        for r in caseRecs:
            f.write("    {%d, %d, %d, %d, %d},\n" % r)
        f.write("};\n\n")
        sizes["case"] += 20 * len(caseRecs)
        f.write(f"const struct uo_full uo_full[{len(fullTab)}] = {{\n")
        for e in fullTab:
            parts = ["{%d, %d}" % x if x else "{0xFFFF, 0}" for x in e[1:]]
            f.write("    {0x%X, {%s}},\n" % (e[0], ", ".join(parts)))
        f.write("};\n\n")
        c_array(f, "uint16_t", "uo_full_str", fullStr or [0])
        sizes["case"] += 12 * len(fullTab) + 2 * len(fullStr)
        f.write(f"const struct uo_range uo_blocks[{len(blocks)}] = {{\n")
        for r in blocks:
            f.write("    {0x%X, 0x%X, %d},\n" % r)
        f.write("};\n\n")
        sets_out, setIndex = [], {}
        f.write(f"const struct uo_range uo_scx[{len(scx)}] = {{\n")
        for first, last, codes in scx:
            k = tuple(codes)
            if k not in setIndex:
                setIndex[k] = len(sets_out)
                sets_out.append(len(codes))
                sets_out.extend(codes)
            f.write("    {0x%X, 0x%X, %d},\n" % (first, last, setIndex[k]))
        f.write("};\n\n")
        c_array(f, "uint8_t", "uo_scx_sets", sets_out)
        f.write(f"const int32_t uo_mirror[{len(mirror)}][2] = {{\n")
        for a, b in mirror:
            f.write("    {0x%X, 0x%X},\n" % (a, b))
        f.write("};\n\n")
        f.write(f"const struct uo_range uo_joining[{len(joining)}] = {{\n")
        for r in joining:
            f.write("    {0x%X, 0x%X, %d},\n" % tuple(r))
        f.write("};\n\n")
        f.write(f"const int32_t uo_brackets[{len(brackets)}][3] = {{\n")
        for r in brackets:
            f.write("    {0x%X, 0x%X, %d},\n" % r)
        f.write("};\n\n")
        sizes["props"] += 12 * (len(joining) + len(brackets))
        c_array(f, "int32_t", "uo_bidi_starts", bidiStarts, per=8)
        sizes["props"] += 4 * len(bidiStarts)
        f.write(f"const char *const uo_script_short[{scCount}] = {{\n")
        for s in range(scCount):
            f.write("    %s,\n" % (f'"{shortName[s]}"' if s in shortName else "0"))
        f.write("};\n\n")
        c_array(f, "uint8_t", "uo_script_rtl", [1 if s in rtl else 0 for s in range(scCount)], per=32)
        f.write(f"const struct uo_scname uo_script_names[{len(scNames)}] = {{\n")
        for _, n, s in scNames:
            f.write(f'    {{"{n}", {s}}},\n')
        f.write("};\n\n")
        f.write(f"const struct uo_err uo_errors[{len(errNames)}] = {{\n")
        for v in sorted(errNames):
            f.write(f'    {{{v}, "{errNames[v]}"}},\n')
        f.write("};\n")
    total = sum(sizes.values())
    print(f"gen_props.py: UCD {ucdver}: {len(recs)} property records, {len(binProps)} binary properties "
          f"({len(binSets)} sets), {len(caseRecs)} case records, {len(fullTab)} full mappings; "
          f"tables {sizes['props']} + {sizes['bin']} + {sizes['more']} + {sizes['case']} = {total} bytes")


if __name__ == "__main__":
    main()
