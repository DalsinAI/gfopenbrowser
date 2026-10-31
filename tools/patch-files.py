#!/usr/bin/env python3
# Copyright (c) 2026 AmigaChrome Team. MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Writes the files a WebKit patch adds whole into a folder, for the host
tests of code that lives only in a patch (such as 0070's public suffix
lookup), or applies a later patch's changes to files already written there
(such as 0077's to 0076's OpenGPU writer).

    tools/patch-files.py PATCH OUT_DIR NAME...

NAME is a file's name (not its path): a new file is written, a changed one
must be in OUT_DIR already and is changed there."""
import os
import re
import sys


def apply_hunks(path, hunks):
    with open(path) as f:
        text = f.readlines()
    shift = 0
    for start, body in hunks:
        old = [l[1:] for l in body if l[:1] in (" ", "-")]
        new = [l[1:] for l in body if l[:1] in (" ", "+")]
        at = start - 1 + shift if start else 0
        if text[at:at + len(old)] != old:
            sys.exit("%s: a hunk at line %d does not apply" % (path, start))
        text[at:at + len(old)] = new
        shift += len(new) - len(old)
    with open(path, "w") as f:
        f.write("".join(text))


def main():
    patch, out, names = sys.argv[1], sys.argv[2], set(sys.argv[3:])
    os.makedirs(out, exist_ok=True)
    found, name, lines, new, hunks, left = set(), None, [], False, [], [0, 0]

    def flush():
        if not name:
            return
        if new:
            with open(os.path.join(out, name), "w") as f:
                f.write("".join(lines))
        elif hunks:
            apply_hunks(os.path.join(out, name), hunks)
        found.add(name)

    for line in open(patch):
        if line.startswith("diff --git "):
            flush()
            name, lines, new, hunks = None, [], False, []
        elif line.startswith("new file mode"):
            new = True
        elif (m := re.match(r"\+\+\+ b/(.*)", line)):
            base = os.path.basename(m.group(1).strip())
            name = base if base in names else None
        elif name and new and line.startswith("+"):
            lines.append(line[1:])
        elif name and not new and (m := re.match(r"@@ -(\d+)(?:,(\d+))? \+\d+(?:,(\d+))? @@", line)):
            hunks.append((int(m.group(1)), []))
            left = [int(m.group(2) or 1), int(m.group(3) or 1)]
        elif name and not new and hunks and (left[0] or left[1]) and line[:1] in (" ", "-", "+"):
            hunks[-1][1].append(line)
            left[0] -= line[:1] != "+"
            left[1] -= line[:1] != "-"
    flush()
    missing = names - found
    if missing:
        sys.exit("%s adds or changes no %s" % (patch, ", ".join(sorted(missing))))


if __name__ == "__main__":
    main()
