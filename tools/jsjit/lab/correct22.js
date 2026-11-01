// Array writes one past the end (patch 0052), as put_by_val and as the
// array builtins' put_by_val_direct: int32, double, object and mixed
// arrays; appends that fill the vector and go past it; arrays made
// non-extensible, sealed or frozen, or given a read-only length, part way
// through; an indexed setter on Array.prototype; copy-on-write literals;
// plain objects with indexed properties; filter, map and friends.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
function append(n) { var a = [], b = [], c = [], d = [1.5]; for (var i = 0; i < n; i++) { a[a.length] = i; b[i] = { v: i }; c[c.length] = i % 3 ? i : "s" + i; d[d.length] = i + 0.5; } return a.length + ":" + b[n - 1].v + ":" + c.join("").length + ":" + d[n]; }
print("append " + append(3000));
function skip(n) { var a = []; for (var i = 0; i < n; i += 2) a[i] = i; return a.length + ":" + (1 in a) + ":" + a.filter(function () { return true; }).length; }
print("skip " + skip(1000));
function locked(n) { var out = []; var kinds = ["preventExtensions", "seal", "freeze", "length"]; for (var k = 0; k < kinds.length; k++) { var a = [1, 2]; a.push(3); for (var i = 0; i < n; i++) { if (i === n / 2) { if (kinds[k] === "length") Object.defineProperty(a, "length", { writable: false }); else Object[kinds[k]](a); } try { a[a.length] = i; } catch (e) { out.push("E"); break; } } out.push(kinds[k] + "=" + a.length); } return out.join(","); }
print("locked " + locked(200));
function lockedStrict(n) { "use strict"; var a = []; for (var i = 0; i < n; i++) { if (i === n / 2) Object.preventExtensions(a); try { a[a.length] = i; } catch (e) { return "threw at " + i + " length " + a.length; } } return "no throw"; }
print("lockedStrict " + lockedStrict(100));
function cow(n) { var t = 0; for (var i = 0; i < n; i++) { var a = [1, 2, 3]; a[3] = i; a[a.length] = 5; t += a.length + a[3]; } return t + ":" + [1, 2, 3].length; }
print("cow " + cow(1000));
function plain(n) { var o = {}; for (var i = 0; i < n; i++) o[i] = i * 2; var p = { 0: "a" }; for (var j = 1; j < 50; j++) p[j] = j; return Object.keys(o).length + ":" + o[n - 1] + ":" + Object.keys(p).length + ":" + o.length; }
print("plain " + plain(500));
function builtins(n) { var src = []; for (var i = 0; i < n; i++) src.push(i % 7 ? i : { i: i }); var t = 0; for (var r = 0; r < 20; r++) { var f = src.filter(function (x) { return typeof x === "number" && x % 2; }); var m = src.map(function (x) { return typeof x === "number" ? x * 2 : x.i; }); var s = src.slice(10, 50); var c = src.concat([1, 2]); var fl = [[1, [2]], [3]].flat(2); t += f.length + m[n - 1] + s.length + c.length + fl.length + Array.from({ length: 5 }, function (_, k) { return k; }).length + Array.of(1, 2, 3).length; } return t; }
print("builtins " + builtins(400));
function int32Shape(n) { var a = []; for (var i = 0; i < n; i++) a[a.length] = i; a[a.length] = 0.5; a[a.length] = "x"; a[a.length] = { y: 1 }; a[a.length] = undefined; return a.length + ":" + a.slice(-4).map(String).join("|"); }
print("int32Shape " + int32Shape(100));
function setter(n) { var log = 0, a = []; for (var i = 0; i < n; i++) { if (i === n / 2) Object.defineProperty(Array.prototype, String(n / 2), { set: function () { log++; }, configurable: true }); a[a.length] = i; } delete Array.prototype[n / 2]; return a.length + ":" + log + ":" + [1, 2].filter(function () { return true; }).length; }
print("setter " + setter(100));
function keepAcrossGC(n) { var keep = []; for (var i = 0; i < n; i++) { var a = []; a[a.length] = { i: i }; a[a.length] = [i]; if (i % 1000 === 0) keep.push(a); } var t = 0; for (var k = 0; k < keep.length; k++) t += keep[k][0].i + keep[k][1][0] + keep[k].length; return t; }
print("keepAcrossGC " + keepAcrossGC(20000));
