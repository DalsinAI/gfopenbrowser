// Arrays made in line (patch 0049): empty arrays that stay empty, take
// int32s, doubles, objects or holes; literals with elements of every kind;
// enough of them for collections to run; arrays kept alive across those;
// a site whose arrays change shape; then a bad time (an indexed setter on
// Array.prototype) part way through.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
function empties(n) { var keep = [], t = 0; for (var i = 0; i < n; i++) { var a = []; if (i % 3 === 1) a.push(i); if (i % 3 === 2) a.push({ v: i }); t += a.length; if (i % 97 === 0) keep.push(a); } return t + ":" + keep.length + ":" + keep[5].length + ":" + (keep[7][0] && keep[7][0].v); }
print("empties " + empties(5000));
function ints(n) { var t = 0; for (var i = 0; i < n; i++) { var a = []; a.push(i, i + 1); a[3] = 7; t += a[0] + a[1] + a.length + (2 in a ? 100 : 0); } return t; }
print("ints " + ints(3000));
function doubles(n) { var t = 0; for (var i = 0; i < n; i++) { var a = []; a.push(i + 0.5); a.push(-0); t += a[0] + (Object.is(a[1], -0) ? 1 : 0); } return t; }
print("doubles " + doubles(3000));
function literals(n) { var out = "", keep = []; for (var i = 0; i < n; i++) { var o = { i: i }, s = "s" + i; var a = [i, s, o, null, undefined, i + 0.25, true]; var b = [o, o]; var c = [s]; if (i % 500 === 0) { out += JSON.stringify(a) + "|" + b.length + c[0] + ";"; keep.push(a, b, c); } } var t = 0; for (var k = 0; k < keep.length; k++) t += keep[k].length; return out + t + ":" + keep[3][2].i; }
print("literals " + literals(4000));
function eight(n) { var t = 0; for (var i = 0; i < n; i++) { var a = [i, i, i, i, i, i, i, i], b = [i, i, i, i, i, i, i, i, i]; a.push(1); t += a.length + b.length + a[7] - b[8]; } return t; }
print("eight " + eight(2000));
function holes(n) { var t = ""; for (var i = 0; i < n; i++) { var a = [i, i]; a.length = 5; if (i === n - 1) t = a.length + ":" + (3 in a) + ":" + a.indexOf(undefined) + ":" + a.join("-"); } return t; }
print("holes " + holes(1000));
function identity(n) { var prev = null, same = 0; for (var i = 0; i < n; i++) { var a = []; if (a === prev) same++; prev = a; } return same + ":" + Array.isArray(prev) + ":" + Object.getPrototypeOf(prev === null ? [] : prev).constructor.name; }
print("identity " + identity(2000));
function shapes(n) { var out = []; for (var i = 0; i < n; i++) { var a = []; if (i < n / 2) a.push(i); else a.push("x" + i); if (i % 400 === 0) out.push(a[0]); } return out.join(","); }
print("shapes " + shapes(3000));
function badTime(n) { var log = 0, t = 0; for (var i = 0; i < n; i++) { if (i === n / 2) Object.defineProperty(Array.prototype, 5, { set: function (v) { log++; }, get: function () { return 42; }, configurable: true }); var a = [i, i]; a[5] = 1; var b = []; t += a.length + a[5] + b[5] === undefined ? 0 : 1; } delete Array.prototype[5]; return t + ":" + log; }
print("badTime " + badTime(2000));
function gc(n) { var keep = []; for (var i = 0; i < n; i++) { var a = [i, [i], { x: [i, i] }]; if (i % 1000 === 0) keep.push(a); } var t = 0; for (var k = 0; k < keep.length; k++) t += keep[k][0] + keep[k][1][0] + keep[k][2].x[1]; return t; }
print("gc " + gc(30000));
