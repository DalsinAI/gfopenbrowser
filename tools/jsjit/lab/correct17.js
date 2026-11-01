// Array.prototype.push done in line (patch 0047): int32 and contiguous
// arrays, growing past their vector, other shapes, holes, frozen and
// sealed arrays, subclasses, non-arrays, a replaced push, the result,
// cells stored into old arrays across collections, and push with more or
// fewer arguments.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
function ints(n) { var a = []; for (var i = 0; i < n; i++) a.push(i); var t = 0; for (var j = 0; j < a.length; j++) t += a[j]; return t + ":" + a.length; }
print("ints " + ints(3000));
function objs(n) { var a = []; for (var i = 0; i < n; i++) a.push({ v: i }); var t = 0; for (var j = 0; j < a.length; j++) t += a[j].v; return t + ":" + a.length; }
print("objs " + objs(3000));
function mixed(n) { var a = [1, 2]; for (var i = 0; i < n; i++) a.push(i % 3 ? i : "s" + i); return a.length + ":" + a[5] + ":" + a[a.length - 1]; }
print("mixed " + mixed(2000));
function dbls(n) { var a = [0.5]; for (var i = 0; i < n; i++) a.push(i + 0.25); return a.length + ":" + a[100]; }
print("dbls " + dbls(2000));
function holes(n) { var a = [1, , 3]; for (var i = 0; i < n; i++) a.push(i); return a.length + ":" + (1 in a) + ":" + a[a.length - 1]; }
print("holes " + holes(500));
function frozen() { var a = Object.freeze([1, 2]), e = ""; for (var i = 0; i < 300; i++) { try { a.push(i); } catch (x) { e = x.constructor.name; } } return a.length + e; }
print("frozen " + frozen());
function sealed() { var a = Object.seal([1, 2]), e = ""; for (var i = 0; i < 300; i++) { try { a.push(i); } catch (x) { e = x.constructor.name; } } return a.length + e; }
print("sealed " + sealed());
function nonWritable() { var a = [1, 2], e = ""; Object.defineProperty(a, "length", { writable: false }); for (var i = 0; i < 300; i++) { try { a.push(i); } catch (x) { e = x.constructor.name; } } return a.length + e; }
print("nonWritable " + nonWritable());
class MyArray extends Array {}
function sub(n) { var a = new MyArray(); for (var i = 0; i < n; i++) a.push(i); return a.length + ":" + (a instanceof MyArray); }
print("sub " + sub(1000));
function generic(n) { var o = { length: 0, push: Array.prototype.push }; for (var i = 0; i < n; i++) o.push(i); return o.length + ":" + o[n - 1]; }
print("generic " + generic(1000));
function results(n) { var a = [], t = 0; for (var i = 0; i < n; i++) t += a.push(i); return t; }
print("results " + results(2000));
function replaced(n) { var a = [], b = [], t = 0; b.push = function (x) { t += x; return -1; }; for (var i = 0; i < n; i++) { (i % 2 ? a : b).push(i); } return a.length + ":" + b.length + ":" + t; }
print("replaced " + replaced(1000));
function protoPush(n) { var saved = Array.prototype.push, t = 0, a = []; for (var i = 0; i < n; i++) { if (i === n / 2) Array.prototype.push = function (x) { t += x; return 0; }; a.push(i); } Array.prototype.push = saved; return a.length + ":" + t; }
print("protoPush " + protoPush(1000));
function many(n) { var a = []; for (var i = 0; i < n; i++) { a.push(); a.push(i, i + 1); } return a.length + ":" + a[7]; }
print("many " + many(1000));
function old(n) { var keep = []; for (var r = 0; r < 20; r++) keep.push([]); var t = 0; for (var i = 0; i < n; i++) { keep[i % 20].push({ s: "x" + i }); var junk = []; for (var k = 0; k < 20; k++) junk.push([k, k + 1]); } for (var j = 0; j < 20; j++) for (var q = 0; q < keep[j].length; q++) t += keep[j][q].s.length; return t; }
print("old " + old(4000));
function cow(n) { var t = 0; for (var i = 0; i < n; i++) { var a = [1, 2, 3]; a.push(i); t += a[3] + a.length; } return t; }
print("cow " + cow(1000));
function onString(n) { var e = ""; for (var i = 0; i < n; i++) { try { Array.prototype.push.call("abc", 1); } catch (x) { e = x.constructor.name; } } return e; }
print("onString " + onString(200));
