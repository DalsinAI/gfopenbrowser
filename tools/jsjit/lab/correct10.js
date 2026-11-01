// Strings in compiled code: s[i], .length of flat strings and ropes, and
// charCodeAt, including replaced and odd receivers (patch 0036): every
// result must match node. The AmiCradle Team, MIT.
if (typeof print === "undefined") var print = console.log;
var collect = typeof gc === "function" ? gc : function () {};
function chars(s) { var r = ""; for (var i = 0; i < s.length; i++) r += s[i]; return r; }
print("chars " + chars("hello, world") + " " + chars("été ÿ") + " " + chars("€中😀x").length);
function idx(s, n) { var r = []; for (var i = -2; i < n; i++) r.push(String(s[i])); return r.join(","); }
print("idx " + idx("abc", 5) + " " + idx("", 2));
function codes(s) { var t = 0; for (var i = 0; i < s.length; i++) t = (t * 31 + s.charCodeAt(i)) | 0; return t; }
print("codes " + codes("The quick brown fox") + " " + codes("Ā€￿") + " " + codes(""));
function oob(s) { var r = []; for (var i = -1; i <= s.length; i++) r.push(s.charCodeAt(i)); r.push(s.charCodeAt(1.5), s.charCodeAt("1"), s.charCodeAt(), s.charCodeAt(NaN)); return r.join(","); }
print("oob " + oob("xyz"));
// ropes: made by +, read before and after flattening
function ropes(n) { var s = ""; var t = 0; for (var i = 0; i < n; i++) { s += String.fromCharCode(97 + i % 26); t += s.length; if (i % 50 === 0) t += s.charCodeAt(i >> 1) + s[i >> 2].length; } return t + " " + s.length + " " + s[n - 1]; }
print("ropes " + ropes(600));
function ropeLen(a, b, n) { var t = 0; for (var i = 0; i < n; i++) { var r = a + b + i; t += r.length; } return t; }
print("ropelen " + ropeLen("left-", "right-", 400));
// substrings and 8/16-bit strings mixed at one site
function mixed(n) { var ss = ["plain", "中文字", "long string here".substring(3, 11), "x".repeat(40), "aāb"]; var t = 0; for (var i = 0; i < n; i++) { var s = ss[i % ss.length]; t += s.length + s.charCodeAt(i % s.length) + s[i % s.length].charCodeAt(0); } return t; }
print("mixed " + mixed(700));
// .length on things that are not strings at the same site
function lens(n) { var vs = ["abc", [1, 2], { length: 7 }, function (a, b, c) {}, "€€", new String("four")]; var t = 0; for (var i = 0; i < n; i++) t += vs[i % vs.length].length; return t; }
print("lens " + lens(600));
// charCodeAt replaced, or called on other receivers
function cc(s, n) { var t = 0; for (var i = 0; i < n; i++) t += s.charCodeAt(i % 3); return t; }
print("cc1 " + cc("abc", 300));
var saved = String.prototype.charCodeAt;
String.prototype.charCodeAt = function (i) { return 1000 + i; };
print("cc2 " + cc("abc", 300));
String.prototype.charCodeAt = saved;
print("cc3 " + cc("abc", 300));
var fake = { charCodeAt: function (i) { return -i; } };
function ccAny(n) { var rs = ["hi!", fake, new String("wow"), "āĂă"], t = 0; for (var i = 0; i < n; i++) t += rs[i & 3].charCodeAt(i % 3); return t; }
print("cc4 " + ccAny(800));
var cca = String.prototype.charCodeAt;
function ccCall(n) { var t = 0; for (var i = 0; i < n; i++) t += cca.call(i % 2 ? "zz" : 1234, i % 2); return t; }
print("cc5 " + ccCall(400));
try { cca.call(null, 0); } catch (e) { print("cc6 " + e.constructor.name); }
// string keys on objects: s[i] where s is not a string
function notStrings(n) { var vs = ["str", [9, 8, 7], { 0: "z", 1: "y", 2: "x" }, new Uint8Array([5, 6, 7])], t = ""; for (var i = 0; i < n; i++) t += vs[i & 3][i % 3]; return t.length + t.slice(0, 16); }
print("ns " + notStrings(400));
// a lexer-like loop with GC in the middle
function lex(src) { var toks = 0, ids = 0, nums = 0; for (var i = 0; i < src.length; i++) { var c = src.charCodeAt(i); if (c >= 48 && c <= 57) { nums++; while (i + 1 < src.length && src[i + 1] >= "0" && src[i + 1] <= "9") i++; } else if (c >= 97 && c <= 122) { ids++; while (i + 1 < src.length && src.charCodeAt(i + 1) >= 97) i++; } toks++; if (i % 500 === 0) collect(); } return toks + "/" + ids + "/" + nums; }
var src = ""; for (var k = 0; k < 300; k++) src += "let v" + k + " = " + (k * 7) + " + foo(bar, " + k + ");\n";
print("lex " + lex(src));
