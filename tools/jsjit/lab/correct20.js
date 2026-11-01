// arguments objects made in line (patch 0050): fewer, as many and more
// arguments than parameters; none; 33 and more (past the in-line limit);
// writes through arguments and through parameters (sloppy mapping);
// arguments.length and callee changed or deleted; arguments kept and read
// after collections; spread, apply and Array.from of arguments.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
function sum() { var t = 0; for (var i = 0; i < arguments.length; i++) t += arguments[i]; return t; }
function two(a, b) { return arguments.length * 1000 + (a | 0) + (b | 0) + (arguments[2] | 0) + (arguments[1] === undefined ? 7 : 0); }
function counts(n) { var t = 0; for (var i = 0; i < n; i++) t += sum() + sum(i) + sum(i, 1) + sum(i, 1, 2, 3, 4, 5, 6, 7, 8, 9) + two() + two(i) + two(i, 2) + two(i, 2, 3, 4); return t; }
print("counts " + counts(2000));
function big(n) { var a = []; for (var i = 0; i < 40; i++) a.push(i); var t = 0; for (var r = 0; r < n; r++) t += sum.apply(null, a.slice(0, 30 + (r % 10))); return t; }
print("big " + big(500));
function mapped(a, b) { arguments[0] = 10; b = 20; return a + ":" + arguments[1] + ":" + arguments.length; }
function maps(n) { var s = ""; for (var i = 0; i < n; i++) s = mapped(1) + "|" + mapped(1, 2) + "|" + mapped(1, 2, 3); return s; }
print("maps " + maps(1000));
function edit() { arguments.length = 1; var r = arguments.length + ":" + Array.prototype.slice.call(arguments).join(","); delete arguments[0]; return r + ":" + (0 in arguments) + ":" + (typeof arguments.callee); }
function edits(n) { var s; for (var i = 0; i < n; i++) s = edit(i, i + 1, i + 2); return s; }
print("edits " + edits(1000));
function keep() { return arguments; }
function kept(n) { var store = [], t = 0; for (var i = 0; i < n; i++) { var a = keep(i, { v: i }, "s" + i); if (i % 100 === 0) store.push(a); } for (var k = 0; k < store.length; k++) t += store[k][0] + store[k][1].v + store[k][2].length + store[k].length; return t + ":" + (store[3].callee === keep); }
print("kept " + kept(30000));
function spread() { return [...arguments].reverse().join("") + Array.from(arguments).length + Math.max.apply(null, arguments); }
function spreads(n) { var s; for (var i = 0; i < n; i++) s = spread(i, 3, 9, 1); return s; }
print("spreads " + spreads(1000));
function strict(a) { "use strict"; arguments[0] = 5; return a + arguments[0] + arguments.length; }
function stricts(n) { var t = 0; for (var i = 0; i < n; i++) t += strict(i) + strict(); return t; }
print("stricts " + (stricts(1000) | 0));
function many(a, b, c, d, e, f, g, h, i2, j, k, l) { return arguments.length + (l === undefined ? 1 : 0) + (arguments[11] === undefined ? 2 : 0) + a; }
function manys(n) { var t = 0; for (var i = 0; i < n; i++) t += many(i) + many(i, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12); return t; }
print("manys " + manys(2000));
