// WebKit patch 0058 (JSON.parse through json.library): run with the JS command (javascript.library).
// Copyright (c) 2026 AmiCradle Team. MIT.
// JSON.parse speed: JS NOCACHE bench.js FILE [REPS] (FILE an absolute path, e.g. the 281 KB launcher/openapi.json from amicradle). Total time over REPS parses (the clock ticks coarsely).
var t = readFile(arguments[0]);
var reps = arguments[1] ? +arguments[1] : 40;
var t16 = t.replace('"title": "AmiCradle"', '"title": "AmiCradle €"');
function time(label, s, reviver) {
    var v = reviver ? JSON.parse(s, reviver) : JSON.parse(s);
    var a = Date.now();
    for (var r = 0; r < reps; r++)
        v = reviver ? JSON.parse(s, reviver) : JSON.parse(s);
    var d = Date.now() - a;
    print(label + " chars=" + s.length + " reps=" + reps + " total=" + d + "ms per-parse=" + (d / reps).toFixed(1) + "ms paths=" + Object.keys(v.paths || {}).length);
}
time("text8", t);
time("text16", t16);
time("reviver8", t, function (k, v) { return v; });
print("same " + (JSON.stringify(JSON.parse(t)) === JSON.stringify(JSON.parse(t16)).replace(" €", "")));
print("PASS");
