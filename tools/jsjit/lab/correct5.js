// Array access in the JIT (patch 0034): every result must match node.
// The AmiCradle Team, MIT.
var log = (typeof print === "function") ? print : console.log;
function out(n, v) { log(n + " " + v); }
function sum(a) { var s = 0; for (var i = 0; i < a.length; i++) s = (s + (a[i] | 0)) | 0; return s; }
function sumAny(a) { var s = ""; for (var i = 0; i < a.length; i++) s += a[i] + ","; return s; }
function fill(a, n, v) { for (var i = 0; i < n; i++) a[i] = v; return a; }
function bump(a) { for (var i = 0; i < a.length; i++) a[i] = (a[i] + 1) | 0; return a; }
function readAt(a, i) { return a[i]; }
function writeAt(a, i, v) { a[i] = v; return a.length; }
function len(x) { return x.length; }
for (var r = 0; r < 40; r++) {
    var ints = []; for (var i = 0; i < 50; i++) ints.push(i * 3);
    out("ints", sum(ints) + ":" + bump(ints)[49]);
    out("cow", sum([1, 2, 3, 4]) + ":" + bump([5, 6, 7]).join("-"));
    var lit = [9, 8, 7]; bump(lit); out("cowLit", lit.join("-") + ":" + [9, 8, 7].join("-"));
    var holes = [1, , 3]; out("holes", sumAny(holes) + ":" + readAt(holes, 1));
    Array.prototype[1] = "proto"; out("protoHole", readAt([0, , 2], 1)); delete Array.prototype[1];
    out("oob", readAt(ints, 50) + ":" + readAt(ints, -1) + ":" + readAt(ints, 1.5) + ":" + readAt(ints, "2"));
    var grow = [1, 2]; out("grow", writeAt(grow, 2, 3) + ":" + writeAt(grow, 10, 4) + ":" + sumAny(grow));
    var doubles = [0.5, 1.5, 2.5]; out("doubles", sumAny(doubles) + ":" + bump(doubles).join("-"));
    var objs = fill([], 5, null); for (var k = 0; k < 5; k++) objs[k] = { v: k }; out("objs", objs.map(function (o) { return o.v; }).join(""));
    var mixed = [1, "two", { three: 3 }, 4.5]; out("mixed", sumAny(mixed));
    var toInts = [1, 2, 3]; toInts[1] = "x"; out("shapeChange", sumAny(toInts));
    var sparse = []; sparse[100000] = 1; sparse[5] = 2; out("sparse", readAt(sparse, 5) + ":" + readAt(sparse, 100000) + ":" + len(sparse));
    var frozen = Object.freeze([1, 2, 3]); writeAt(frozen, 0, 9); out("frozen", frozen.join("-"));
    var ta = new Int32Array(4); fill(ta, 4, 7); out("typed", sum(ta) + ":" + len(ta));
    var o = { 0: "a", 1: "b", length: 2 }; out("arrayLike", sumAny(o) + ":" + len(o));
    out("lengths", len("hello") + ":" + len(function (a, b) {}) + ":" + len([]) + ":" + len(new Array(7)));
    (function () { out("arguments", sum(arguments) + ":" + len(arguments)); })(1, 2, 3);
    var big = []; big.length = 4294967295; out("bigLength", len(big));
    var s = 0, arr = fill([], 100, 2); for (var m = 0; m < 100; m++) { s = (s + arr[m] * m) | 0; arr[m] = s & 7; } out("loop", s + ":" + arr[99]);
}
