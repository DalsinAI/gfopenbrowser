// The AmiCradle Team. JIT correctness check: output must match with JSC_useM68KTemplateJIT=1 and without.
var out = [];
function log(x) { out.push(String(x)); }
function t1() { var s = 0; for (var i = -50; i < 50; i++) { s += i * i - (i >> 1) + (i % 7) - (i / 3); } return s; }
function t2() { var s = 0; for (var i = 0; i < 300; i++) { s = (s + 0x7fffffff) | 0; s ^= i << 3; } return s; }
function t3() { var a = 2147483640, r = []; for (var i = 0; i < 20; i++) { a = a + 1; r.push(a); } return r.join(","); }
function t4() { var o = {x: 1}, r = 0; for (var i = 0; i < 100; i++) { if (i == 50) o = {y: 2, x: 5}; r += o.x; } return r; }
function t5() { var r = 0; for (var i = 0; i < 100; i++) { try { if (i % 10 == 0) throw i; r += 1; } catch (e) { r += e * 2; } } return r; }
function t6() { var s = ""; for (var i = 0; i < 60; i++) s += (i % 3 ? "a" : 1.5) + i; return s.length + s.slice(0, 20); }
function t7() { var r = 0, g = { get v() { return 3; } }; for (var i = 0; i < 100; i++) r += g.v + (i & 1 ? 1 : -1); return r; }
function t8() { var f = function (a, b) { return a === undefined ? -1 : a * b; }; var r = 0; for (var i = 0; i < 100; i++) r += f(i, 2) + f(); return r; }
function t9() { var a = [5, 3, 9, 1]; var r = 0; for (var i = 0; i < 100; i++) { for (var j = 0; j < a.length; j++) r += a[j] * j; a[i % 4] = i; } return r; }
function t10() { var r = 0; for (var i = 0; i < 100; i++) { var x = i < 50 ? i : i + 0.5; r += x > 70.2 ? Math.floor(x) : -x; } return r; }
function t11() { var r = []; for (var i = 0; i < 40; i++) { switch (i % 5) { case 0: r.push("z"); break; case 3: r.push(i); break; default: r.push(typeof i); } } return r.join(""); }
function t12() { var n = 0; for (var k in {a: 1, b: 2, c: 3}) for (var i = 0; i < 30; i++) n += k.charCodeAt(0) + i; return n; }
function t13() { var r = 0; for (var i = 0; i < 200; i++) { var c = (function (z) { return function () { return z + i; }; })(i); r += c(); } return r; }
function t14() { var r = 0; var i = 0; do { r += i-- ; i += 2; } while (i < 100); return r; }
function t15() { var x = -0, r = []; for (var i = -3; i < 3; i++) { r.push(1 / (i * 0), -i, ~i, !i, i >>> 1); } return r.join(","); }
var tests = [t1,t2,t3,t4,t5,t6,t7,t8,t9,t10,t11,t12,t13,t14,t15];
for (var k = 0; k < tests.length; k++) { for (var rep = 0; rep < 3; rep++) log(tests[k].name + "=" + tests[k]()); }
print(out.join("\n"));
