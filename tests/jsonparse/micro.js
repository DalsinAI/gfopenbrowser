// WebKit patch 0058 (JSON.parse through json.library): run with the JS command (javascript.library).
// Copyright (c) 2026 AmiCradle Team. MIT.
var reps = 20;
function time(label, s) {
    JSON.parse(s);
    var a = Date.now();
    for (var r = 0; r < reps; r++) JSON.parse(s);
    print(label + " chars=" + s.length + " per-parse=" + ((Date.now() - a) / reps).toFixed(1) + "ms");
}
var ints = [], strs = [], objs = [], dbls = [], keys = {};
for (var i = 0; i < 20000; i++) { ints.push(i); strs.push("s" + (i % 100)); dbls.push(i / 3); }
for (var i = 0; i < 4000; i++) objs.push({ id: i, name: "n", ok: true, v: null, t: "x" });
for (var i = 0; i < 4000; i++) keys["k" + i] = i;
time("ints", JSON.stringify(ints));
time("doubles", JSON.stringify(dbls));
time("strings", JSON.stringify(strs));
time("objects", JSON.stringify(objs));
time("manykeys", JSON.stringify(keys));
time("longstr", JSON.stringify(["y".repeat(200000)]));
print("PASS");
