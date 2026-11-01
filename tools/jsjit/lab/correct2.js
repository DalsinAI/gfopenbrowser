// GFOpenBrowser JIT lab: doubles, NaN, -0, property caches and calls; must print the
// same with JSC_useM68KTemplateJIT=0 and =1. MIT, Copyright (c) 2026 AmigaChrome Team.
var out = [];
function log(x) { out.push(String(x)); }
// doubles, NaN, -0, overflow into doubles
function d1() { var s = 0.5, r = []; for (var i = 0; i < 50; i++) { s = s * 1.5 - i + 0.25; r.push(s); } return r.join(","); }
function d2() { var r = []; var v = [NaN, -0, 0, 1.5, -2, Infinity, -Infinity, 3, 2147483647, -2147483648];
  for (var i = 0; i < v.length; i++) for (var j = 0; j < v.length; j++) { var a = v[i], b = v[j];
    r.push(a + b, a - b, a * b, a < b, a <= b, a > b, a >= b, !(a < b), 1 / (a * b)); if (a < b) r.push("L"); if (!(a >= b)) r.push("NG"); }
  return r.join(","); }
function d3() { var r = 0; for (var i = 0; i < 100; i++) { var x = i * 46341; r += x + x; } return r; }
function d4() { var r = []; for (var i = -3; i < 4; i++) { r.push(1 / (i * 0), 1 / (0 * -i), 0 * 1.5 * i); } return r.join(","); }
function d5() { var a = 0.1, b = 0.2, r = []; for (var i = 0; i < 30; i++) { r.push(a + b); a = a * 3 + 1e-17; b = b / 3 + 1e300 * 1e-300; } return r.join(","); }
function d6() { var x = 1e308, r = []; for (var i = 0; i < 5; i++) { r.push(x * 10, x - x * 2, (x * 10) - (x * 10)); x = x * 2; } return r.join(","); }
function d7() { var r = 0; for (var x = 0.5; x < 20.25; x += 0.75) r += x; for (var y = 10; y >= -3.5; y -= 0.5) r -= y; return r; }
function d8() { var s = "a", r = []; for (var i = 0; i < 10; i++) { r.push(s + i, i + s, s - i, (i < "5")); } return r.join(","); }
// properties: inline, out-of-line, polymorphic, prototype, getters, setters, objects stored
function p1() { var o = {a: 1, b: 2}; var r = 0; for (var i = 0; i < 200; i++) { o.a = o.b + i; r += o.a; } return r; }
function p2() { var o = {}; for (var k = 0; k < 80; k++) o["k" + k] = k; var r = 0; for (var i = 0; i < 200; i++) { o.k70 = o.k75 + i; o.k3 = o.k70 - 1; r += o.k3 + o.k79; } return r; }
function p3() { var os = [{x: 1}, {y: 1, x: 2}, {z: 1, y: 2, x: 3}, {x: "s"}]; var r = ""; for (var i = 0; i < 100; i++) { var o = os[i % 4]; o.x = o.x + 1; r += o.x; } return r.length + r.slice(-30); }
function P() { this.q = 1; } P.prototype.w = 5;
function p4() { var o = new P(), r = 0; for (var i = 0; i < 100; i++) { r += o.w + o.q; if (i == 60) o.w = 7; } return r; }
function p5() { var n = 0, o = { get g() { return n++; }, set s(v) { n += v; } }; for (var i = 0; i < 100; i++) { o.s = i; n += o.g; } return n; }
function p6() { var keep = []; var o = {v: null}; for (var i = 0; i < 3000; i++) { o.v = {i: i, arr: [i, i + 1]}; if (i % 100 == 0) keep.push(o.v); } var r = 0; for (var j = 0; j < keep.length; j++) r += keep[j].arr[1]; return r + o.v.i; }
function p7() { var o = {a: 1}; var r = 0; for (var i = 0; i < 100; i++) { if (i == 40) delete o.a; if (i == 70) o.a = 9; r += (o.a === undefined ? 100 : o.a); o.b = i; } return r + "," + o.b; }
function p8() { var arr = [], r = 0; for (var i = 0; i < 50; i++) arr.push({v: i, w: i * 2}); for (var k = 0; k < 10; k++) for (var j = 0; j < arr.length; j++) { arr[j].v = arr[j].w + k; r += arr[j].v; } return r; }
function p9() { var o = {a: 1}; Object.freeze(o); var r = 0; for (var i = 0; i < 50; i++) { try { o.a = i; } catch (e) {} r += o.a; } return r; }
function p10() { var r = 0; var x = {a: 5}; for (var i = 0; i < 100; i++) { var t = i % 3 ? x : 7; r += (t.a === undefined) ? 1 : t.a; } return r; }
// calls (entry compile)
function add(a, b) { return a + b; }
function c1() { var s = 0; for (var i = 0; i < 2000; i++) s = add(s, i * 0.5); return s; }
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function c2() { return fib(18); }
function thrower(x) { if (x % 7 == 3) throw new Error("e" + x); return x * 2; }
function c3() { var r = 0; for (var i = 0; i < 300; i++) { try { r += thrower(i); } catch (e) { r += e.message.length; } } return r; }
function deep(n) { return deep(n + 1) + 1; }
function c4() { var r = ""; for (var i = 0; i < 3; i++) { try { deep(0); } catch (e) { r += e.constructor.name; } } return r; }
function c5() { var r = 0; for (var i = 0; i < 300; i++) r += Math.max(i, 150) + parseInt("1" + (i % 10)); return r; }
function sq(x) { return x * x; } function cu(x) { return x * x * x; }
function c6() { var fs = [sq, cu, sq, function (x) { return -x; }]; var r = 0; for (var i = 0; i < 400; i++) r += fs[i % 4](i % 13); return r; }
function Pt(x, y) { this.x = x; this.y = y; }
function c7() { var r = 0; for (var i = 0; i < 300; i++) { var p = new Pt(i, i + 1); r += p.x * p.y; } return r; }
function argsf(a, b, c) { return arguments.length + (c === undefined ? 100 : c) + (b | 0); }
function c8() { var r = 0; for (var i = 0; i < 300; i++) r += argsf(i) + argsf(i, 2) + argsf(i, 2, 3, 4); return r; }
function c9() { var o = { get v() { return add(this.k, 1); }, k: 5 }; var r = 0; for (var i = 0; i < 300; i++) { o.k = i; r += o.v; } return r; }
function rec(n, acc) { if (n == 0) return acc; return rec(n - 1, acc + n * 0.5); }
function c10() { var r = 0; for (var i = 0; i < 50; i++) r += rec(40, i); return r; }
var tests = [c3,c4,c5,c6,c7,c8,c9,c10,d1,d2,d3,d4,d5,d6,d7,d8,p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,c1,c2];
for (var rep = 0; rep < 12; rep++) for (var k = 0; k < tests.length; k++) log(rep + " " + tests[k].name + "=" + tests[k]());
print(out.join("\n"));
