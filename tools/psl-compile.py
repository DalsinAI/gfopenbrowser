#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""The Public Suffix List as the engine reads it (WebKit patch 0070).

    tools/psl-compile.py public_suffix_list.dat data/public-suffixes

Reads Mozilla's list (https://github.com/publicsuffix/list) and writes one
rule a line, lower case, names in Punycode (the engine sees hosts as WTF's
URL parser gives them), sorted by byte so the engine can binary search the
file as it is. The first line is "OBPSL1 <rules>". Comments, blank lines and
the ICANN/private section marks are left out: the engine treats both
sections the same, as libpsl's PSL_TYPE_ANY did. Run it again to take a
newer list; the engine needs no rebuild."""
import sys


def to_ascii(label):
    if label in ("*",) or label.isascii():
        return label.lower()
    try:
        return label.encode("idna").decode("ascii")
    except UnicodeError:
        return "xn--" + label.encode("punycode").decode("ascii")


def rule_ascii(rule):
    mark = ""
    if rule.startswith("!"):
        mark, rule = "!", rule[1:]
    return mark + ".".join(to_ascii(l) for l in rule.split("."))


def main():
    src, out = sys.argv[1], sys.argv[2]
    rules = set()
    with open(src, encoding="utf-8") as f:
        for line in f:
            line = line.split()[0] if line.split() else ""
            if not line or line.startswith("//"):
                continue
            rules.add(rule_ascii(line))
    rules = sorted(rules, key=lambda r: r.encode("ascii"))
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("OBPSL1 %d\n" % len(rules))
        for r in rules:
            f.write(r + "\n")
    print("%s: %d rules" % (out, len(rules)))


if __name__ == "__main__":
    main()
