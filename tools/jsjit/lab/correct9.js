// get_by_id on objects of several shapes, getters (own and on the
// prototype), missing properties, prototypes that change and garbage
// collection of the shapes a site saw (patch 0036): every result must
// match node. The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
var collect = typeof gc === "function" ? gc : function () {};
// one site, up to six shapes
function shapes(n) {
    var objs = [{ x: 1 }, { a: 0, x: 2 }, { b: 0, c: 0, x: 3 }, { x: 4, y: 5 }, { d: 1, e: 2, f: 3, x: 6 }, { g: 1, x: 7 }];
    var t = 0;
    for (var i = 0; i < n; i++) t += objs[i % objs.length].x;
    return t;
}
print("shapes " + shapes(1200) + " " + shapes(7));
// own getters, several shapes
function Pt(x) { this._x = x; }
var g1 = { get v() { return 1; } }, g2 = { q: 0, get v() { return this.q + 2; } };
var g3 = Object.create(null); Object.defineProperty(g3, "v", { get: function () { return 3; } });
function getters(n) { var os = [g1, g2, g3, { v: 10 }], t = 0; for (var i = 0; i < n; i++) t += os[i & 3].v; return t; }
print("own " + getters(800));
// getters on the prototype, and the prototype changing under the site
var proto = { get len() { return this._x * 2; } };
Pt.prototype = proto;
function protoGet(objs, n) { var t = 0; for (var i = 0; i < n; i++) t += objs[i % objs.length].len; return t; }
var pts = [new Pt(1), new Pt(2), new Pt(3)];
var other = [{ len: 100 }, { z: 0, len: 200 }];
print("proto1 " + protoGet(pts.concat(other), 500));
Object.defineProperty(proto, "len", { get: function () { return -this._x; }, configurable: true });
print("proto2 " + protoGet(pts.concat(other), 500));
proto.extra = 1; // the prototype's structure changes again
print("proto3 " + protoGet(pts.concat(other), 500));
delete proto.len; // now missing: undefined from the prototype
print("proto4 " + String(protoGet(pts, 300)));
proto.len = 9; // a plain value on the prototype
print("proto5 " + protoGet(pts.concat(other), 300));
// missing properties: the prototype is the last on the chain
function missing(n) {
    var a = Object.create(null), b = Object.create(null); b.k = 1;
    var c = Object.create(Object.create(null)); var t = 0, u = 0;
    for (var i = 0; i < n; i++) { var o = [a, b, c, {}][i & 3]; if (o.k === undefined) u++; else t += o.k; if (i === n / 2) Object.getPrototypeOf(c).k = 5; }
    return t + "/" + u;
}
print("missing " + missing(800));
// a missing property that appears on Object.prototype
function missing2(n) { var os = [{ a: 1 }, { b: 1 }, { c: 1 }], u = 0, t = 0; for (var i = 0; i < n; i++) { var v = os[i % 3].zz; if (v === undefined) u++; else t += v; if (i === 300) Object.prototype.zz = 4; } delete Object.prototype.zz; return u + "/" + t; }
print("missing2 " + missing2(600));
// getters that throw, getters that call, getters reading arguments
var thrower = { get v() { throw new Error("boom" + this.n); }, n: 1 };
function throwy(n) { var os = [{ v: 1 }, { w: 0, v: 2 }, thrower], t = 0, e = 0; for (var i = 0; i < n; i++) { try { t += os[i % 3].v; } catch (x) { e++; } } return t + "/" + e; }
print("throw " + throwy(600));
function fib(k) { return k < 2 ? k : fib(k - 1) + fib(k - 2); }
var caller = { get v() { return fib(6); } };
function deep(n) { var os = [caller, { v: 1 }, { u: 0, v: 2 }], t = 0; for (var i = 0; i < n; i++) t += os[i % 3].v; return t; }
print("deep " + deep(300));
// a native getter, a getter that is not a function, a Proxy
var arr = [1, 2, 3], map = new Map([[1, 1]]);
function natives(n) { var t = 0; for (var i = 0; i < n; i++) { var o = [map, { size: 5 }, { q: 1, size: 6 }][i % 3]; t += o.size; } return t; }
print("native " + natives(600));
var prx = new Proxy({}, { get: function (t, k) { return k === "v" ? 42 : undefined; } });
function proxies(n) { var t = 0; for (var i = 0; i < n; i++) t += [prx, { v: 1 }, { a: 0, v: 2 }][i % 3].v; return t; }
print("proxy " + proxies(600));
// shapes dying: the site's table must forget them across a GC
function churn(n) { var t = 0; for (var r = 0; r < n; r++) { var o = {}; o["p" + (r % 40)] = r; o.x = r; t += o.x; if (r % 100 === 0) collect(); } return t; }
print("churn " + churn(2000));
function churnGet(n) { var t = 0; for (var r = 0; r < n; r++) { var o = Object.create({ get x() { return r; } }); o["q" + (r % 30)] = 1; t += o.x; if (r % 120 === 0) collect(); } return t; }
print("churnget " + churnGet(1500));
// primitives as the base: strings, numbers, booleans
String.prototype.twice = undefined;
Object.defineProperty(Number.prototype, "half", { get: function () { return this / 2; }, configurable: true });
function prims(n) { var t = 0; var bs = [3, "abcd", 8, { half: 1 }, true]; for (var i = 0; i < n; i++) { var v = bs[i % 5].half; t += v === undefined ? 100 : v; } return t; }
print("prims " + prims(500));
// classes and super getters
class A { constructor(k) { this.k = k; } get kk() { return this.k * 3; } }
class B extends A { get kk() { return super.kk + 1; } }
function cls(n) { var os = [new A(1), new B(2), new A(3), { kk: 1 }], t = 0; for (var i = 0; i < n; i++) t += os[i & 3].kk; return t; }
print("class " + cls(800));
// dictionary-mode objects
function dict(n) { var d = {}; for (var j = 0; j < 80; j++) d["k" + j] = j; delete d.k3; d.x = 1; var os = [d, { x: 2 }, { y: 0, x: 3 }], t = 0; for (var i = 0; i < n; i++) t += os[i % 3].x; return t; }
print("dict " + dict(600));
// a setter-only accessor (its getter is the null getter) read at a hot site
var setOnly = {}; Object.defineProperty(setOnly, "x", { set: function (v) {} }); setOnly.y = 1;
function readSetOnly(n) { var os = [setOnly, { x: 1 }, { z: 0, x: 2 }], u = 0, t = 0; for (var i = 0; i < n; i++) { var v = os[i % 3].x; if (v === undefined) u++; else t += v; } return u + "/" + t; }
print("setonly " + readSetOnly(900));
// a prototype getter redefined, then a property added to the prototype
// (the site's entry is refreshed, not stuck)
function Q(k) { this.k = k; }
Object.defineProperty(Q.prototype, "kk", { get: function () { return this.k + 1; }, configurable: true });
function readKK(objs, n) { var t = 0; for (var i = 0; i < n; i++) t += objs[i % objs.length].kk; return t; }
var qs = [new Q(1), new Q(2), { kk: 10 }];
var r1 = readKK(qs, 600);
Object.defineProperty(Q.prototype, "kk", { get: function () { return this.k * 10; }, configurable: true });
var r2 = readKK(qs, 600);
Q.prototype.other = 5;
print("refresh " + r1 + " " + r2 + " " + readKK(qs, 600));
