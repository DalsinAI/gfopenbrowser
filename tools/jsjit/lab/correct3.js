// Loop register copies (patch 0032): every result must match the interpreter.
// The AmiCradle Team, MIT.
function out(n, v) { print(n + " " + v); }
function f1() { var s = 0; for (var i = 0; i < 100000; i++) s = s + i * 30000; return s; }            // add overflows to double mid-loop
function f2() { var s = 1; for (var i = 0; i < 40; i++) s = s * 3; return s; }                        // mul overflow
function f3() { var i = 2147483600; var n = 0; while (i < 2147483700) { i++; n++; } return i + n; }  // inc overflow
function f4() { var s = 0; for (var i = 0; i < 1000; i++) { s = (s + i) | 0; if (i == 500) s = 0.5; } return s; } // leaves copy, re-enters
function f5() { var s = "a"; for (var i = 0; i < 10; i++) s = s + i; return s; }                       // string, preheader fails
function f6() { var t = 0; for (var i = 0; i < 300; i++) for (var j = 0; j < 300; j++) t = (t + (i ^ j)) | 0; return t; } // nested
function f7() { var s = 0, i = 0; for (;;) { s = (s + i) | 0; i++; if (i >= 777) break; } return s + ":" + i; }
function f8() { var a = 7, b = 0; for (var i = 0; i < 5000; i++) { b = (b - a) | 0; a = (a * 5) & 1023; b = b ^ a; } return a + ":" + b; }
function f9(n) { var s = 0; for (var i = n; i > 0; i--) s = (s + i) | 0; return s; }
function f10() { var x = -0, s = 0; for (var i = 0; i < 10; i++) { s = s - 0; x = s * -1; } return 1 / x; }  // -0 from mul
function f11() { var s = 0; for (var i = 0; i < 100; i++) s = s + 0.25; return s; }
function f12() { var s = 0; var o = { valueOf: function () { return 3; } }; for (var i = 0; i < 100; i++) s = (s + o) | 0; return s; }
function f13() { var s = 0; for (var i = 0; i < 70000; i++) s = s - i * 40000; return s; }
function f14() { var i = -2147483600, n = 0; while (i > -2147483700) { i--; n++; } return i + ":" + n; }
for (var r = 0; r < 3; r++) {
    out("f1", f1()); out("f2", f2()); out("f3", f3()); out("f4", f4()); out("f5", f5());
    out("f6", f6()); out("f7", f7()); out("f8", f8()); out("f9", f9(5000)); out("f9b", f9(70000));
    out("f10", f10()); out("f11", f11()); out("f12", f12()); out("f13", f13()); out("f14", f14());
}
function f15(n) { var x; var i = 0; while (i < n) { i = (i + 1) | 0; if (i > 3) break; x = (i * 2) | 0; } return x + ":" + i; }
function f16(n) { var x; for (var i = 0; i < n; i++) { if (i >= 0) break; x = i + 1; } return x; }
function f17() { var a = 0, b = 1, t; for (var i = 0; i < 40; i++) { t = (a + b) | 0; a = b; b = t; } return a + ":" + b + ":" + t; }
function f18() { var s = 0; for (var i = 0; i < 2000; i++) { var k = i & 7; s = (s + k * k) | 0; } return s + ":" + k; }
function f19() { var o = { a: 1, b: 2 }, s = 0; for (var i = 0; i < 3000; i++) { o.a = (o.b + i) | 0; s = (s + o.a) | 0; if (i == 1500) o.b = 0.5; } return s + ":" + o.a; }
function f20() { var o = { a: 1 }, p = { b: 9, a: 2 }, s = 0; for (var i = 0; i < 2000; i++) { var q = (i & 1) ? o : p; s = (s + q.a) | 0; q.a = (q.a + 1) | 0; } return s + ":" + o.a + ":" + p.a; }
function f21() { var o = { get a() { return 3; } }, s = 0; for (var i = 0; i < 1000; i++) s = (s + o.a) | 0; return s; }
function f22() { var o = { a: 0 }, s = 0; for (var i = 0; i < 1000; i++) { o.a = i; s = (s + o.a) | 0; if (i == 500) o.a = "x"; } return s + ":" + o.a; }
function f23() { var o = { a: 1 }, s = 0; for (var i = 0; i < 1000; i++) { s = (s + o.a) | 0; if (i == 600) delete o.a, o.a = 5; } return s; }
function f24() { var o = { n: 0 }, t = 0; for (var i = 0; i < 1000; i++) { o.n = (o.n + i) | 0; t = o.n; } var r = { x: o }; return t + ":" + r.x.n; }
for (var r = 0; r < 3; r++) { out("f19", f19()); out("f20", f20()); out("f21", f21()); out("f22", f22()); out("f23", f23()); out("f24", f24()); out("f15", f15(10)); out("f15b", f15(0)); out("f16", f16(5)); out("f17", f17()); out("f18", f18()); }
