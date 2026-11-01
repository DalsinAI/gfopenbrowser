// Regular expressions that skip start positions (patch 0051): a pattern
// whose one alternative starts with a character, a cased character or a
// character class, against 8-bit and 16-bit strings, global and sticky,
// with lastIndex, replace, split, matchAll and search; then patterns the
// skip must leave alone (alternatives, optional or empty-able starts,
// assertions, lookbehind, groups, back references, non-BMP characters).
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
var latin = "The quick <brown> fox & the \"lazy\" dog's café: naïve façade, Ünïcödé ÿ ß 123-456 KELVIN kµ xéy";
var wide = latin + " ſK İ 😀 Σσς end";
var patterns = [/[&<>"']/g, /[^a-z ]/g, /o/g, /O/gi, /é/g, /É/gi, /[é-ÿ]/g, /[à-ÿ]/gi, /\d+/g, /\w+/g, /\W/g, /\s\S/g, /[µÿ]/g, /k/gi, /s/giu, /K/giu,
    /ab|qu/g, /x?y/g, /a*b/g, /(?:fox|dog)/g, /^T/gm, /\bt/gi, /(?<=c)a/g, /(o)\1/g, /e\b/g, /.a/g, /[\s\S]e/g, /😀/gu, /[\u{1F600}]/gu, /σ/gi, /y$/g, /zzz/g, /a{0}b/g, /[]/g, /[^]/g, /\p{L}é/gu];
function all(re, s) { var out = []; re.lastIndex = 0; var m, n = 0; while ((m = re.exec(s)) && n++ < 200) { out.push(m.index + ":" + m[0]); if (!m[0].length) re.lastIndex++; } return out.join(","); }
for (var r = 0; r < 3; r++) {
    var res = [];
    for (var p of patterns) res.push(String(p) + " " + all(p, latin) + " | " + all(p, wide));
    if (r === 2) print(res.join("\n"));
}
function replaces(n) { var t = ""; for (var i = 0; i < n; i++) t = latin.replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; }).replace(/o/g, "0") + wide.replace(/[ß-ÿ]/g, "_"); return t; }
print("replaces " + replaces(100));
function sticky() { var re = /[a-z]/y, out = []; for (var i = 0; i < 12; i++) { re.lastIndex = i; out.push(re.test(latin) ? "1" : "0"); } return out.join(""); }
print("sticky " + sticky());
function lastIndex() { var re = /o/g, out = []; re.lastIndex = 13; out.push(re.exec(latin).index); re.lastIndex = latin.length; out.push(re.exec(latin)); re.lastIndex = 1000; out.push(re.exec(latin)); return out.join(","); }
print("lastIndex " + lastIndex());
print("split " + latin.split(/[ ,]/).length + " " + wide.split(/é/).join("|"));
print("matchAll " + [...wide.matchAll(/[à-ÿ]/g)].map(function (m) { return m.index; }).join(","));
print("search " + latin.search(/[<>]/) + " " + latin.search(/QQ/) + " " + "aaa".search(/b/) + " " + "".search(/[a]/) + " " + "".replace(/x/g, "y") + "|");
function bigInput() { var s = "x".repeat(5000) + "<" + "y".repeat(5000) + ">"; var c = 0; for (var i = 0; i < 20; i++) c += s.replace(/[<>]/g, "").length + (s.match(/[<>]/g) || []).length; return c; }
print("bigInput " + bigInput());
