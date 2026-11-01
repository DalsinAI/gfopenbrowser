// Math.min and Math.max done in line (patch 0048): int32 pairs, equal
// values, negatives and the int32 limits, then doubles, -0, NaN, strings,
// objects with valueOf (called once each, in order), undefined, more or
// fewer arguments, and a replaced Math.min.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
function ints(n) { var t = 0, u = 0; for (var i = 0; i < n; i++) { t += Math.min(i, n - i); u += Math.max(i - 50, 50 - i); } return t + ":" + u; }
print("ints " + ints(2000));
function limits() { var r = []; for (var i = 0; i < 200; i++) { r[0] = Math.min(2147483647, -2147483648); r[1] = Math.max(2147483647, -2147483648); r[2] = Math.min(-1, -1); r[3] = Math.max(0, 0); } return r.join(","); }
print("limits " + limits());
function mixedTypes(n) { var vals = [3, 2.5, -0, 0, NaN, "7", "x", null, undefined, true, { valueOf: function () { return 4; } }], out = []; for (var r = 0; r < n; r++) for (var i = 0; i < vals.length; i++) for (var j = 0; j < vals.length; j++) { var a = Math.min(vals[i], vals[j]), b = Math.max(vals[i], vals[j]); if (r === 0) out.push((Object.is(a, -0) ? "-0" : a) + "/" + (Object.is(b, -0) ? "-0" : b)); } return out.join(" "); }
print("mixed " + mixedTypes(30));
function order() { var log = ""; var a = { valueOf: function () { log += "a"; return 1; } }, b = { valueOf: function () { log += "b"; return 2; } }; for (var i = 0; i < 100; i++) Math.min(a, b); return log.length + log.slice(0, 6); }
print("order " + order());
function counts(n) { var t = 0; for (var i = 0; i < n; i++) t += Math.min(i, 5, 3) + Math.max(i) + (Math.min() === Infinity ? 1 : 0) + (Math.max() === -Infinity ? 1 : 0); return t; }
print("counts " + counts(500));
function replaced(n) { var saved = Math.min, t = 0; for (var i = 0; i < n; i++) { if (i === n / 2) Math.min = function (a, b) { return a + b; }; t += Math.min(i, 3); } Math.min = saved; return t; }
print("replaced " + replaced(1000));
function asValue(n) { var f = Math.max, t = 0; for (var i = 0; i < n; i++) t += f(i, 100); return t; }
print("asValue " + asValue(1000));
