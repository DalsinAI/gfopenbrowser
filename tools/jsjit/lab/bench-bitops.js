// Bit operation microbenchmark for patch 0101: each case is timed and
// printed in seconds; run it with JSC_useM68KBitFastPaths set to 1 and to 0
// (Set, not SetEnv, on the Amiga). The checksums must match between runs.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
// Each case repeats SCALE times (50; ?scale=N on a page, or scale=N as an argument).
var SCALE = 50;
if (typeof location !== "undefined" && /[?&]scale=(\d+)/.test(location.search)) SCALE = +RegExp.$1;
if (typeof arguments !== "undefined" && arguments.length && /^scale=(\d+)$/.test(arguments[0])) SCALE = +RegExp.$1;
if (typeof process !== "undefined" && process.argv && /^scale=(\d+)$/.test(process.argv[2] || "")) SCALE = +RegExp.$1;
var now = typeof preciseTime === "function" ? preciseTime : function () { return Date.now() / 1000; };
var text = ""; for (var i = 0; i < 4000; i++) text += String.fromCharCode(32 + (i * 7919) % 90);
var table = []; for (var n = 0; n < 256; n++) { var c = n; for (var b = 0; b < 8; b++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; table[n] = c >>> 0; }
function int32Ops(n) { var a = 1, s = 0; for (var i = 0; i < n; i++) { a = (a << 3) ^ (a >> 5) ^ i; s = (s + (a & 0xffff)) | 0; } return s; }
function shifts(n) { var x = -0x12345678, s = 0; for (var i = 0; i < n; i++) { var k = i & 31; s = (s + ((x << k) ^ (x >> k) ^ (x >>> k))) | 0; } return s; }
function doubleToInt(n) { var h = 0, t = 0; for (var i = 0; i < n; i++) { h = h * 31 + i; t ^= h; h = h | 0; } return t; }
function uint32(n) { var x = 2463534242, t = 0; for (var i = 0; i < n; i++) { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; t = (t + (x >>> 0)) % 1000003; } return t; }
function crc32(s, r) { var v = 0; for (var k = 0; k < r; k++) { var crc = 0xffffffff; for (var i = 0; i < s.length; i++) crc = table[(crc ^ s.charCodeAt(i)) & 0xff] ^ (crc >>> 8); v = (crc ^ 0xffffffff) >>> 0; } return v; }
function fnv(s, r) { var v = 0; for (var k = 0; k < r; k++) { var h = 0x811c9dc5; for (var i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = (h * 16777619) >>> 0; } v = h; } return v; }
function notAndOr(n) { var s = 0; for (var i = 0; i < n; i++) s = (s + (~i & 0x55) + (i | 0x100)) | 0; return s; }
var cases = [["int32 & ^ << >>", function () { return int32Ops(200000); }], ["variable shifts", function () { return shifts(200000); }],
    ["double | 0 and ^", function () { return doubleToInt(100000); }], ["xorshift >>> 0", function () { return uint32(100000); }],
    ["crc32", function () { return crc32(text, 10); }], ["fnv-1a", function () { return fnv(text, 10); }], ["~ & |", function () { return notAndOr(200000); }]];
var total = 0;
for (var c = 0; c < cases.length; c++) {
    cases[c][1]();
    var t0 = now(), v; for (var r = 0; r < SCALE; r++) v = cases[c][1](); var t = now() - t0; total += t;
    print(cases[c][0] + ": " + t.toFixed(2) + " s, checksum " + v);
}
print("total: " + total.toFixed(2) + " s (scale " + SCALE + ")");
