// Doubles in loop register copies: values kept in FPU registers, written
// back to the frame, mixed with int32 work, NaN, -0, Infinity, and loops left
// by exceptions, calls and closures (patch 0036): every result must match
// node. The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
var collect = typeof gc === "function" ? gc : function () {};
function sum(n) { var s = 0.5; for (var i = 0; i < n; i++) s = s + 0.25; return s; }
print("sum " + sum(1000) + " " + sum(0));
function poly(n) { var x = 1.5, y = -2.25, z = 0.1; for (var i = 0; i < n; i++) { x = x * 1.0001 + y; y = y - x * 0.5; z = z + x * y; } return [x, y, z].join(","); }
print("poly " + poly(300));
function negz(n) { var z = -0.0, w = 0.0; for (var i = 0; i < n; i++) { z = z * 1.0; w = w - 0.0; } return [1 / z, 1 / w, Object.is(z, -0)].join(","); }
print("negz " + negz(50));
function nans(n) { var a = 0.0, b = 1.5; for (var i = 0; i < n; i++) { a = a + b; if (i === 20) b = NaN; if (i === 40) b = 1.5; } return [a, a !== a].join(","); }
print("nan " + nans(60));
function inf(n) { var a = 1.5; for (var i = 0; i < n; i++) a = a * 1e10; return [a, -a, a - a].join(","); }
print("inf " + inf(40));
// a double that int-only instructions also use
function mixedUse(n) { var d = 0.5, t = 0; for (var i = 0; i < n; i++) { d = d + 1.0; t = t + (d | 0); t ^= i; } return t + " " + d; }
print("mixed " + mixedUse(500));
// a double turning into an int and back, and into other types
function change(n) { var d = 2.5, r = []; for (var i = 0; i < n; i++) { d = d * 2.0; if (i === 3) d = 7; if (i === 5) d = "s"; if (i === 7) d = 1.25; if (i % 2) r.push(typeof d === "number" ? d : d.length); } return r.join(","); }
print("change " + change(12));
// values read outside the loop by closures and arguments while it runs
function seen(n) { var d = 0.75; var peek = function () { return d; }; var t = 0; for (var i = 0; i < n; i++) { d = d + 0.5; if (i % 10 === 0) t += peek(); } return t + " " + d; }
print("seen " + seen(200));
function args(n) { var d = 1.5; for (var i = 0; i < n; i++) { d = d * 1.5; if (i === 5) return arguments.length + " " + d; } return d; }
print("args " + args(10, 2));
// exceptions and calls in the middle of a double loop
function thrower(i) { if (i === 77) throw new Error("x" + i); return 0.5; }
function exc(n) { var d = 0.0; try { for (var i = 0; i < n; i++) d = d + thrower(i); } catch (e) { return e.message + " " + d; } return d; }
print("exc " + exc(200));
function calls(n) { var d = 0.0; for (var i = 0; i < n; i++) { d = d + Math.sqrt(i + 0.5); if (i % 50 === 0) collect(); } return d.toFixed(6); }
print("calls " + calls(400));
// double compares as loop conditions
function cmp(n) { var d = 0.0, c = 0; while (d < n) { d = d + 0.3; if (d >= 5.0 && d <= 10.0) c++; } return c + " " + d.toFixed(3); }
print("cmp " + cmp(20));
// nested loops, many doubles (more than the FPU registers)
function many(n) { var a = 0.1, b = 0.2, c = 0.3, d = 0.4, e = 0.5, f = 0.6, g = 0.7; for (var i = 0; i < n; i++) { for (var j = 0; j < 4; j++) { a = a + b; b = b + c; c = c + d; d = d + e; e = e + f; f = f + g; g = g * 0.5; } } return [a, b, c, d, e, f, g].map(function (v) { return v.toPrecision(10); }).join(","); }
print("many " + many(100));
// doubles from constants and from typed arrays
function typed(n) { var f = new Float64Array(16), s = 0.5; for (var i = 0; i < n; i++) { s = s + 0.125; f[i & 15] = s; } var t = 0.0; for (var k = 0; k < 16; k++) t = t + f[k]; return t; }
print("typed " + typed(300));
// int32 overflow into doubles inside the copy
function ovf(n) { var x = 2147483000, d = 0.5; for (var i = 0; i < n; i++) { x = x + 100; d = d + 0.5; } return x + " " + d; }
print("ovf " + ovf(20));
// a slot that holds doubles given constants that are not numbers
function nonNumbers(n) { var d = 0.5, s = 0.0, kinds = ""; for (var i = 0; i < n; i++) { d = d * 1.5; s = s + 0.25; if (i === 100) d = "x"; if (i === 101) { kinds += typeof d; d = 2.5; } if (i === 150) d = null; if (i === 151) { kinds += typeof d; d = 0.25; } if (i === 160) d = undefined; if (i === 161) { kinds += typeof d; d = 1.0; } if (i === 170) d = true; if (i === 171) { kinds += typeof d; d = 0.5; } } return kinds + " " + d + " " + s; }
print("nonnum " + nonNumbers(200));
