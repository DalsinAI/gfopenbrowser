// Calls to several functions from one site, objects built by literals and
// constructors, prototype chains (patch 0035): every result must match node.
// The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
function a(x) { return x + 1; } function b(x) { return x * 2; } function c(x, y) { return "" + x + (y === undefined ? "u" : y); }
function mk(k) { return function (x) { return x + k; }; }
var fs = [a, b, c, mk(1), mk(2), mk(3), Math.abs, String, function () { return arguments.length; }, (x) => -x, a.bind(null, 5)];
var out = [];
for (var r = 0; r < 300; r++) { var f = fs[r % fs.length]; out.push(f(r - 150)); if (r % 7 === 0) f(); }
print(out.join(","));
// closure calls only (same executable)
var ks = []; for (var i = 0; i < 6; i++) ks.push(mk(i));
var s = 0; for (var i = 0; i < 2000; i++) s += ks[i % 6](i);
print("closures " + s);
// .call / .apply with several targets
function Em() { this.h = []; } Em.prototype.on = function (f) { this.h.push(f); return this; };
Em.prototype.emit = function (v) { var t = 0; for (var i = 0; i < this.h.length; i++) t += this.h[i].call(this, v); return t; };
var e = new Em(); e.on(function (v) { return v; }).on(function (v) { return v * 3; }).on(function (v) { return this.h.length + v; });
var t = 0; for (var i = 0; i < 1000; i++) t += e.emit(i);
print("emit " + t);
// constructors, literals, transitions with prototype chains and setters
class P { constructor(x) { this.x = x; this.y = x * 2; } sum() { return this.x + this.y; } }
class Q extends P { constructor(x) { super(x); this.z = 1; } sum() { return super.sum() + this.z; } }
var acc = 0; for (var i = 0; i < 1500; i++) { var p = i % 3 ? new P(i) : new Q(i); acc += p.sum(); var o = { a: i, b: "s" + i, c: p }; o.d = o.a + 1; acc += o.d; }
print("objs " + acc);
var proto = { set w(v) { this._w = v * 10; } }; var hits = 0;
for (var i = 0; i < 1000; i++) { var o = i < 500 ? {} : Object.create(proto); o.w = i; hits += o._w === undefined ? o.w : o._w; }
print("setter " + hits);
// a setter added to the prototype after the cache settled
function Thing() {} var th = [];
for (var i = 0; i < 800; i++) { if (i === 400) Object.defineProperty(Thing.prototype, "v", { set: function (x) { this.vv = x; } }); var o = new Thing(); o.v = i; th.push(o.v === undefined ? "s" + o.vv : o.v); }
print("late " + th.slice(395, 405).join(","));
// recursion through a polymorphic site
function fib(n) { return n < 2 ? n : fs2[n & 1](n - 1) + fs2[n & 1](n - 2); } var fs2 = [fib, function (n) { return fib(n); }];
print("fib " + fib(18));
