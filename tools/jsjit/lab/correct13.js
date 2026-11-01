// Small functions inlined into their callers (patch 0038): every way out of
// the inlined code (types changing, getters, exceptions with stack traces,
// a callee replaced or garbage-collected, the stack running out) must give
// what node gives. The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
var collect = typeof gc === "function" ? gc : function () {};
function add(a, b) { return a + b; }
function addLoop(n) { var s = 0; for (var i = 0; i < n; i++) s = add(s, i) & 0xfffff; return s; }
print("add " + addLoop(3000));
// arguments turning into doubles, strings and objects at an inlined site
function mixed(n) { var r = []; for (var i = 0; i < n; i++) { var a = i < 500 ? i : i < 700 ? i + 0.5 : i < 800 ? "s" + i : { valueOf: function () { return 3; } }; r.push(add(a, 1)); } return r[10] + "," + r[600] + "," + r[750] + "," + r[850] + "," + r.length; }
print("mixed " + mixed(900));
// overflow inside the inlined body
function big(n) { var t = 0; for (var i = 0; i < n; i++) t = add(2147483000, i); return t; }
print("big " + big(1000));
// methods: to_this and get_by_id, objects of several shapes
function P(x) { this._x = x; }
P.prototype.getX = function () { return this._x; };
P.prototype.dbl = function () { return this._x * 2; };
function methods(n) { var ps = [new P(1), new P(2)], t = 0; for (var i = 0; i < n; i++) { var p = ps[i & 1]; t += p.getX() + p.dbl(); if (i === 600) ps[1] = { _x: 5, getX: P.prototype.getX, dbl: P.prototype.dbl }; if (i === 800) ps[0].extra = 1; } return t; }
print("methods " + methods(1000));
// a primitive this, sloppy and strict
Number.prototype.me = function () { return typeof this; };
String.prototype.len2 = function () { "use strict"; return this.length * 2; };
function prims(n) { var t = "", u = 0; for (var i = 0; i < n; i++) { if (i % 100 === 0) t += (5).me(); u += "abc".len2(); } return t + u; }
print("prims " + prims(500));
// conditionals and compares in the callee
function isDigit(c) { return c >= 48 && c <= 57; }
function clamp(x, lo, hi) { if (x < lo) return lo; if (x > hi) return hi; return x; }
function sel(a) { return a === null ? 0 : a === undefined ? 1 : a ? 2 : 3; }
function conds(n) { var d = 0, c = 0, s = 0; var vals = [null, undefined, 0, 1, "", "x", NaN, {}]; for (var i = 0; i < n; i++) { if (isDigit(40 + (i % 30))) d++; c += clamp(i % 50, 10, 40); s += sel(vals[i & 7]); } return d + " " + c + " " + s; }
print("conds " + conds(1200));
// an exception thrown inside the inlined callee: stack trace and catch
function getF(o) { return o.f; }
function trace(n) { var t = 0, msg = ""; for (var i = 0; i < n; i++) { try { t += getF(i === 777 ? null : { f: i }); } catch (e) { msg = e.constructor.name + ":" + (String(e.stack).indexOf("getF") >= 0); } } return t + " " + msg; }
print("trace " + trace(1000));
// a getter reached from an inlined callee (the callee leaves its inlined code)
var gobj = { get g() { return 7; } };
function readG(o) { return o.g; }
function getters(n) { var t = 0; for (var i = 0; i < n; i++) t += readG(i < 500 ? { g: 1 } : gobj); return t; }
print("getters " + getters(1000));
// the callee replaced at a call site, and the old one garbage-collected
var fn = function (x) { return x + 1; };
function callFn(n) { var t = 0; for (var i = 0; i < n; i++) { t += fn(i); if (i === 400) { fn = function (x) { return x * 2; }; collect(); } if (i === 700) { fn = function (x) { return x - 1; }; collect(); collect(); } } return t; }
print("replace " + callFn(1000));
// fewer and more arguments than parameters; unused results
function three(a, b, c) { return c === undefined ? a + b : a + b + c; }
function argc(n) { var t = 0; for (var i = 0; i < n; i++) { t += three(i, 1); t += three(i, 1, 2, 3); three(i, i); } return t; }
print("argc " + argc(600));
// globals read and written by the callee
var counter = 0;
function bump(k) { counter = counter + k; return counter; }
function globals(n) { for (var i = 0; i < n; i++) bump(i & 3); return counter; }
print("globals " + globals(800));
// arrays and properties written by the callee
function setAt(a, i, v) { a[i] = v; return a.length; }
function setP(o, v) { o.p = v; return o.p; }
function writes(n) { var a = [], o = {}, t = 0; for (var i = 0; i < n; i++) { t += setAt(a, i % 50, i); t += setP(o, i); if (i === 300) a.push("x"); } return t + " " + a.length + " " + a[3]; }
print("writes " + writes(600));
// deep recursion around an inlined call: the stack check
function leaf(x) { return x + 1; }
function rec(d) { return d === 0 ? 0 : leaf(rec(d - 1)); }
var depth = 0; try { depth = rec(3000); } catch (e) { depth = e.constructor.name; }
print("rec " + depth);
function forever(d) { return leaf(forever(d + 1)); }
try { forever(0); } catch (e) { print("overflow " + e.constructor.name); }
// a callee whose CodeBlock is compiled late and called through .call
function viaCall(n) { var t = 0; for (var i = 0; i < n; i++) t += add.call(null, i, 2); return t; }
print("call " + viaCall(500));
// inlined functions that call: nested results, a throw caught by the
// caller, the stack trace through the inlined frame, garbage collected
// during the inner call, and the caller's slots kept across it
function inner(x) { return x * 3; }
function middle(x) { return inner(x) + 1; }
function nested(n) { var t = 0, keep = { k: 7 }; for (var i = 0; i < n; i++) t += middle(i) + keep.k; return t; }
print("nested " + nested(700));
function maybeThrow(x) { if (x === 450) throw new Error("at " + x); return x; }
function thrower(x) { return maybeThrow(x) + 1; }
function catches(n) { var t = 0; for (var i = 0; i < n; i++) { try { t += thrower(i); } catch (e) { t += 1000; print("caught " + e.message); } } return t; }
print("catches " + catches(600));
function deepest() { return new Error("trace").stack.split("\n").length > 2; }
function viaInline() { return deepest(); }
function traces(n) { var ok = true; for (var i = 0; i < n; i++) ok = ok && viaInline(); return ok; }
print("trace " + traces(300));
function allocate(i) { return { i: i, s: "v" + i, a: [i, i + 1] }; }
function wrap(i) { return allocate(i).a[1]; }
function churn(n) { var t = 0, held = [1, 2, 3]; for (var i = 0; i < n; i++) { t += wrap(i); if (i % 100 === 0 && typeof gc === "function") gc(); } return t + held.length; }
print("churn " + churn(1500));
function ignore(i) { inner(i); return i; }
function ignored(n) { var t = 0; for (var i = 0; i < n; i++) t += ignore(i); return t; }
print("ignored " + ignored(500));
function recInline(d) { return d === 0 ? 0 : middle(recInline(d - 1)); }
print("recInline " + recInline(400));
