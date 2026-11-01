// Compares and tests on every kind of value (patch 0035): every result must match node.
// The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
var o1 = {}, o2 = {}, f1 = function () {}, sym = Symbol("s");
var big = 10n;
var s1 = "abc", s2 = "ab" + "c".repeat(1), s3 = "";
var vals = [0, 1, -1, 2147483647, -2147483648, 0.5, -0, NaN, Infinity, 1.0e10, true, false, null, undefined, "", "0", "abc", s2, s3, o1, o2, f1, sym, big, 10n, [], [1], 3];
function out(tag, xs) { print(tag + " " + xs.join("")); }
function strict(a, b) { return (a === b ? "1" : "0") + (a !== b ? "1" : "0"); }
function strictJ(a, b) { var r = ""; if (a === b) r += "e"; else r += "n"; if (a !== b) r += "N"; else r += "E"; return r; }
function strictK(a) { var r = ""; if (a === 3) r += "a"; if (a === "abc") r += "b"; if (a === null) r += "c"; if (a === undefined) r += "d"; if (a === true) r += "e"; if (a === o1) r += "f"; if (a === 0.5) r += "g"; if (3 === a) r += "h"; return r || "-"; }
function truth(a) { var r = ""; if (a) r += "T"; else r += "F"; if (!a) r += "f"; while (a) { r += "w"; break; } return r + (a ? "1" : "0"); }
function nul(a) { return "" + (a == null ? 1 : 0) + (a != null ? 1 : 0) + (a === null || a === undefined ? 1 : 0) + ((a == null) ? "y" : "n") + ((a != undefined) ? "Y" : "N"); }
function cmp(a, b) { return "" + (a < b ? 1 : 0) + (a <= b ? 1 : 0) + (a > b ? 1 : 0) + (a >= b ? 1 : 0) + (a == b ? 1 : 0) + (a != b ? 1 : 0); }
function cmpJ(a, b) { var r = ""; if (a == b) r += "q"; if (a != b) r += "Q"; var x = a < b; var y = a >= b; return r + x + y; }
function isUndef(a) { return typeof a === "undefined" ? "u" : "-"; }
for (var round = 0; round < 3; round++) {
    var r1 = [], r2 = [], r3 = [], r4 = [], r5 = [], r6 = [], r7 = [];
    for (var i = 0; i < vals.length; i++) {
        r3.push(truth(vals[i])); r4.push(nul(vals[i])); r6.push(strictK(vals[i])); r7.push(isUndef(vals[i]));
        for (var j = 0; j < vals.length; j++) {
            r1.push(strict(vals[i], vals[j])); r2.push(strictJ(vals[i], vals[j]));
            if (typeof vals[i] !== "symbol" && typeof vals[j] !== "symbol" && typeof vals[i] !== "bigint" && typeof vals[j] !== "bigint") r5.push(cmp(vals[i], vals[j]) + cmpJ(vals[i], vals[j]));
        }
    }
    for (var k = 0; k < 400; k++) { strict(k, k + 1); truth(k); nul(k); cmp(k, 5); cmpJ(k, 5); strictJ(o1, o2); strictK(k); }
    out("strict", r1); out("strictJ", r2); out("truth", r3); out("null", r4); out("cmp", r5); out("strictK", r6); out("undef", r7);
}
// int loops with these compares
var n = 0; for (var i = 0; i < 3000; i++) { if (i % 3 === 0) n++; if (i !== 7) n += 2; if (i > 100 === true) n++; var t = i & 1; if (t) n += 3; }
print("loop " + n);
