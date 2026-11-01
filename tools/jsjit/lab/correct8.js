// Global, lexical and closure variables read and written from hot code, with
// eval injection, TDZ, read-only globals and garbage collection (patch 0035):
// every result must match node. The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
var collect = typeof gc === "function" ? gc : function () {};
var gv = 0, gs = "", gobj = null;
let gl = 1; const gc1 = 7;
globalThis.gp = 3;
function readGlobals(n) { var t = 0; for (var i = 0; i < n; i++) t += gv + gl + gc1 + gp; return t; }
function writeGlobals(n) { for (var i = 0; i < n; i++) { gv = i; gl = i * 2; gp = i + 1; gobj = { i: i, s: "o" + i }; if (i % 97 === 0) collect(); } return gv + gl + gp + gobj.s; }
print("g " + readGlobals(500) + " " + writeGlobals(800) + " " + readGlobals(10));
function makeCounter() { var c = 0, objs = []; return { inc: function () { c++; return c; }, add: function (o) { objs.push(o); return objs.length; }, get: function () { return c + objs.length; }, deep: function () { return function () { return function () { c += 2; return c; }; }; } }; }
var ctr = makeCounter(), d = ctr.deep()();
for (var i = 0; i < 1000; i++) { ctr.inc(); if (i % 10 === 0) ctr.add({ n: i }); d(); if (i % 250 === 0) collect(); }
print("closure " + ctr.get() + " " + d());
var cells = [];
function holder() { var held = null; return function (o) { var old = held; held = o; return old; }; }
var h = holder(); for (var i = 0; i < 600; i++) { var o = { k: i, arr: [i, i + 1] }; var prev = h(o); if (prev) cells.push(prev.arr[1]); if (i % 150 === 0) collect(); }
print("barrier " + cells.length + " " + cells[cells.length - 1]);
// TDZ: reading and writing a let before its initialisation throws
function tdz() { var r = []; for (var i = 0; i < 300; i++) { try { r.push(late); } catch (e) { r.push(e.constructor.name); } try { late2 = i; } catch (e) { r.push("w" + e.constructor.name); } } return r.slice(0, 4).join(",") + r.length; }
print("tdz " + tdz());
let late = 5; let late2 = 0;
print("tdz2 " + tdz() + " " + late2);
// const assignment
function cst() { var n = 0; for (var i = 0; i < 300; i++) { try { gc1 = i; } catch (e) { n++; } } return n; }
print("const " + cst() + " " + gc1);
// read-only globals in sloppy code
function ro() { for (var i = 0; i < 300; i++) { undefined = i; NaN = i; Infinity = 1; } return "" + undefined + NaN + Infinity; }
print("ro " + ro());
// eval injecting a var into a function scope
function inj(flag) { var x = 1; var t = 0; for (var i = 0; i < 400; i++) { t += x + gv; if (flag && i === 200) eval("var gv = 1000;"); } return t; }
print("inj " + inj(false) + " " + inj(true) + " " + gv);
// a new global let shadowing a global property later on
function shadowRead() { var t = 0; for (var i = 0; i < 300; i++) t += typeof shadow === "undefined" ? 0 : shadow; return t; }
globalThis.shadow = 2; print("shadow1 " + shadowRead());
(0, eval)("let shadow = 5;"); print("shadow2 " + shadowRead());
// with: dynamic scope
function withScope() { var o = { a: 1 }, t = 0, a = 10; for (var i = 0; i < 300; i++) { with (o) { t += a; } if (i === 150) delete o.a; } return t; }
print("with " + withScope());
// global var deleted / redefined as accessor
var acc = 0; Object.defineProperty(globalThis, "dyn", { get: function () { return ++acc; }, configurable: true });
function readDyn() { var t = 0; for (var i = 0; i < 300; i++) t += dyn; return t; }
print("dyn " + readDyn());
Object.defineProperty(globalThis, "dyn", { value: 4, writable: true, configurable: true }); print("dyn2 " + readDyn());
