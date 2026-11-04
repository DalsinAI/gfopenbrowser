// Bit operations (patch 0101): &, |, ^, ~, <<, >> and >>> on int32s and on
// doubles (sums that overflowed, uint32s from >>> 0, fractions, negative
// and huge values, NaN, infinities), with constant and variable counts,
// counts of 32 and more, in loops (the register copy) and out of them,
// and the hashes and checksums pages run: FNV-1a, MurmurHash3, CRC-32,
// xorshift, a djb2 string hash.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
var values = [0, -0, 1, -1, 5, 31, 32, 33, 63, 64, -33, 2147483647, -2147483648, 2147483648, -2147483649, 4294967295, 4294967296, 4294967297,
    1.5, -1.5, 2.9, -2.9, 0.49999999999999994, 2147483647.9, 2147483648.5, 3000000000.7, 4294967295.99, -2147483648.5, -3e9, 1e10, -1e10, 1e300,
    NaN, Infinity, -Infinity, 5e-324, 65536.25, 123456789, -987654321];
function all(f) { var out = []; for (var i = 0; i < values.length; i++) for (var j = 0; j < values.length; j++) out.push(f(values[i], values[j])); return out; }
function sum(a) { var t = 0; for (var i = 0; i < a.length; i++) t = (t * 31 + a[i]) % 1000000007; return t; }
function and(a, b) { return a & b; }
function or(a, b) { return a | b; }
function xor(a, b) { return a ^ b; }
function shl(a, b) { return a << b; }
function sar(a, b) { return a >> b; }
function shr(a, b) { return a >>> b; }
function not(a) { return ~a; }
function u(a) { return a >>> 0; }
var fs = [and, or, xor, shl, sar, shr];
for (var r = 0; r < 3; r++) {
    for (var k = 0; k < fs.length; k++) print(fs[k].name + " " + sum(all(fs[k])));
    var t = []; for (var i = 0; i < values.length; i++) t.push(not(values[i]), u(values[i]), values[i] | 0, values[i] >> 0, values[i] << 0, values[i] & -1, values[i] ^ 0);
    print("unary " + sum(t) + " " + t.slice(0, 12).join(","));
}
function constants(x) { return [x << 1, x << 8, x << 9, x << 31, x << 32, x << 33, x >> 1, x >> 8, x >> 31, x >> 32, x >>> 1, x >>> 9, x >>> 31, x >>> 32, x >>> 0, x & 0xff, x & 0xffffffff, x | 0x80000000, x ^ 0xffffffff, x & 1.9, x | 4294967296.5, ~x].join(","); }
for (var i = 0; i < values.length; i++) print("c " + constants(values[i]));
function fnv(s) { var h = 0x811c9dc5; for (var i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = (h * 16777619) >>> 0; } return h; }
function fnvImul(s) { var h = 0x811c9dc5 | 0; for (var i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24); } return h >>> 0; }
function murmur(key, seed) { var h = seed >>> 0, k; for (var i = 0; i + 4 <= key.length; i += 4) { k = (key.charCodeAt(i) & 0xff) | ((key.charCodeAt(i + 1) & 0xff) << 8) | ((key.charCodeAt(i + 2) & 0xff) << 16) | ((key.charCodeAt(i + 3) & 0xff) << 24); k = Math.imul(k, 0xcc9e2d51); k = (k << 15) | (k >>> 17); k = Math.imul(k, 0x1b873593); h ^= k; h = (h << 13) | (h >>> 19); h = (Math.imul(h, 5) + 0xe6546b64) | 0; } h ^= key.length; h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); h ^= h >>> 16; return h >>> 0; }
var table = []; for (var n = 0; n < 256; n++) { var c = n; for (var b = 0; b < 8; b++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; table[n] = c >>> 0; }
function crc32(s) { var crc = 0xffffffff; for (var i = 0; i < s.length; i++) crc = table[(crc ^ s.charCodeAt(i)) & 0xff] ^ (crc >>> 8); return (crc ^ 0xffffffff) >>> 0; }
function djb2(s) { var h = 5381; for (var i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0; return h; }
function slowHash(s) { var h = 0; for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return h; }
function xorshift(n) { var x = 2463534242, t = 0; for (var i = 0; i < n; i++) { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; t = (t + (x >>> 0)) % 1000003; } return t + ":" + (x >>> 0); }
var text = ""; for (var i = 0; i < 2000; i++) text += String.fromCharCode(32 + (i * 7919) % 90);
print("fnv " + fnv(text) + " " + fnvImul(text));
print("murmur " + murmur(text, 0) + " " + murmur(text, 0x9747b28c));
print("crc32 " + crc32(text) + " " + crc32("The quick brown fox jumps over the lazy dog"));
print("djb2 " + djb2(text) + " slow " + slowHash(text));
print("xorshift " + xorshift(20000));
function loopShifts(n) { var a = 0, b = 1, c = -7, s = 0; for (var i = 0; i < n; i++) { a = (a << 3) ^ i; b = (b >> 1) | (i << 20); c = (c >>> (i & 7)) ^ ~i; s = (s + (a & 0xffff) + (b >>> 16) + (c & 255)) | 0; } return s + ":" + a + ":" + b + ":" + c; }
print("loopShifts " + loopShifts(30000));
function loopCounts(n) { var x = 0x12345678, s = 0; for (var i = 0; i < n; i++) { var k = i % 70 - 3; s = (s ^ (x << k) ^ (x >> k) ^ (x >>> k)) | 0; } return s; }
print("loopCounts " + loopCounts(5000));
function loopDoubles(n) { var h = 0, t = 0; for (var i = 0; i < n; i++) { h = h * 31 + i; t ^= h; h = h | 0; t = t >>> 0; } return h + ":" + t; }
print("loopDoubles " + loopDoubles(20000));
function objects(n) { var o = { valueOf: function () { return 6; } }, t = 0; for (var i = 0; i < n; i++) t += (o & 3) + (o << 1) + ("12" >> 1) + (true | 0) + (null ^ 5) + (undefined >>> 0) + ~"7"; return t; }
print("objects " + objects(1000));
try { print(1n & 1); } catch (e) { print("bigint " + (e instanceof TypeError)); }
print("bigints " + ((5n & 3n) + (5n << 2n)) + " " + (~5n));
