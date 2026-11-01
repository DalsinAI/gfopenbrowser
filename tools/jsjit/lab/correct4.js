// Calls into JavaScript from C++ (nested entries): every result must match.
// The AmiCradle Team, MIT.
var log = (typeof print === "function") ? print : console.log;
function out(n, v) { log(n + " " + v); }
var a = []; for (var i = 0; i < 300; i++) a.push((i * 7919) % 301);
out("sort", a.slice().sort(function (x, y) { return x - y; }).slice(0, 8).join(","));
out("sortnested", [3, 1, 2].sort(function (x, y) { return [y, x].sort(function (p, q) { return p - q; })[0] === x ? -1 : 1; }).join(","));
out("map", a.map(function (x, i) { return (x * i) | 0; }).reduce(function (s, x) { return (s + x) | 0; }, 0));
var acc = 0; a.forEach(function (x) { acc = (acc ^ (x << 3)) | 0; }); out("forEach", acc);
out("replace", "a1b22c333".replace(/\d+/g, function (m) { return "<" + m.length + ">"; }));
var o = {}; Object.defineProperty(o, "g", { get: function () { return [1, 2, 3].map(function (x) { return x * 2; }).join("-"); } });
out("getter", JSON.stringify(o.g));
out("reviver", JSON.stringify(JSON.parse('{"a":1,"b":[2,3]}', function (k, v) { return typeof v === "number" ? v * 10 : v; })));
var vo = { valueOf: function () { return [5, 4].sort(function (x, y) { return x - y; })[0]; } };
out("valueOf", (vo + 1) + ":" + Math.max(vo, 2));
try { [1, 2, 3].forEach(function (x) { if (x == 2) throw new Error("boom" + x); }); } catch (e) { out("throw", e.message); }
function deep(n) { return n == 0 ? 0 : [n].map(function (x) { return deep(x - 1) + 1; })[0]; }
out("deep", deep(200));
var s = 0; for (var r = 0; r < 50; r++) s = (s + [r, r + 1].reduce(function (p, q) { var t = 0; for (var k = 0; k < 20; k++) t = (t + k * q) | 0; return p + t; }, 0)) | 0;
out("loopinside", s);
out("from", Array.from({ length: 5 }, function (_, i) { return i * i; }).join(","));
out("tostr", String({ toString: function () { return ["x", "y"].map(function (c) { return c + c; }).join(""); } }));
