// WebKit patch 0058 (JSON.parse through json.library): run with the JS command (javascript.library).
// Copyright (c) 2026 AmiCradle Team. MIT.
// Focused JSON.parse checks for patch 0058: prints one line per check.
// Run on the old and the new engine and compare (only error messages may differ).
var n = 0, bad = 0;
function show(v) {
    if (typeof v === "string") {
        var s = "";
        for (var i = 0; i < v.length; i++) {
            var c = v.charCodeAt(i);
            s += c >= 32 && c < 127 ? v[i] : "\\u" + ("000" + c.toString(16)).slice(-4);
        }
        return '"' + s + '"';
    }
    if (typeof v === "number")
        return Object.is(v, -0) ? "-0" : String(v);
    if (v && typeof v === "object") {
        var parts = [];
        var keys = Reflect.ownKeys(v);
        for (var k of keys)
            parts.push(show(String(k)) + ":" + show(v[k]));
        return (Array.isArray(v) ? "A" : "O" + (Object.getPrototypeOf(v) === Object.prototype ? "" : "!proto")) + "{" + parts.join(",") + "}";
    }
    return String(v);
}
function check(name, text, reviver) {
    n++;
    var r;
    try {
        r = "ok " + show(reviver ? JSON.parse(text, reviver) : JSON.parse(text));
    } catch (e) {
        r = "throws " + e.name + (e.name === "SyntaxError" ? "" : ": " + e.message);
        if (e.name === "SyntaxError")
            print("  message " + name + ": " + e.message);
    }
    print(name + " " + r);
}
function expect(name, got, want) {
    n++;
    if (got !== want) {
        bad++;
        print("BAD " + name + ": got " + got + " want " + want);
    } else
        print(name + " ok");
}

// __proto__ is a plain own property
check("proto1", '{"__proto__": 1}');
check("proto2", '{"__proto__": {"x": 1}, "y": 2}');
var p = JSON.parse('{"__proto__": []}');
expect("proto3", Object.getPrototypeOf(p) === Object.prototype && Array.isArray(p.__proto__) && Object.keys(p).join() === "__proto__", true);
check("proto4", '{"__proto__": 1, "__proto__": 2}');
// duplicate keys: the last wins, in the first one's place
check("dup1", '{"a": 1, "b": 2, "a": 3}');
check("dup2", '{"a": {"x": 1}, "a": [1, 2]}');
check("dup3", '{"0": 1, "1": 2, "0": 3}');
// index keys and key order
check("idx1", '{"b": 1, "2": 2, "a": 3, "1": 4, "4294967294": 5, "4294967295": 6, "01": 7, "-1": 8}');
// numbers
check("num1", '[0, -0, 1, -1, 2147483647, -2147483648, 2147483648, -2147483649, 4294967296]');
check("num2", '[0.1, 0.2, 1e21, 1e-7, 1.5e300, 1e400, -1e400, 5e-324, 2e-324, 1e-400]');
check("num3", '[9007199254740993, 9007199254740995, 18446744073709551616, 123456789012345678901234567890]');
check("num4", '[1.7976931348623157e308, 1.7976931348623158e308, 1.7976931348623159e308, 2.2250738585072011e-308, 2.2250738585072012e-308]');
check("num5", '[0.30000000000000004, 3.0e0, 3E+0, 300e-2, -0.0, 0e5, 1.0]');
expect("num6", JSON.parse("0.1") === 0.1 && JSON.parse("1e23") === 1e23 && JSON.parse("8.98846567431158e307") === 8.98846567431158e307, true);
expect("num7", JSON.parse('"x"') === "x" && JSON.parse("123") === 123 && JSON.parse("-0.5") === -0.5, true);
check("num8", "[1e-1, 1E1, 12345678901234567890e-10, 0.000001, 0.0000001]");
// bad numbers
check("badnum1", "[01]");
check("badnum2", "[1.]");
check("badnum3", "[.5]");
check("badnum4", "[+1]");
check("badnum5", "[1e]");
check("badnum6", "[NaN]");
check("badnum7", "[Infinity]");
check("badnum8", "[-]");
// strings: escapes, surrogates, NUL, Latin-1, astral
check("str1", '["a\\"b", "\\\\", "\\/", "\\b\\f\\n\\r\\t", "\\u0041\\u00e9\\u20ac"]');
check("str2", '["\\ud83d\\ude00", "\\ud83d", "\\ude00", "x\\ud800y", "\\udc00\\ud800", "\\ud800\\ud800\\udc00"]');
check("str3", '["\\u0000", "a\\u0000b", {"\\u0000": 1}]');
check("str4", '["éÿ", "Ā", "€", "😀"]');
check("str5", '{"\\ud800": 1, "k\\udfff": 2}');
check("str6", '["\ud800", "a\udc00b", "😀\ud83d"]');    // raw lone surrogates in a 16-bit source
check("str7", '["�", "\\uFFFD", "\\uFFFD\\ud800"]');
check("str8", '[' + JSON.stringify("x".repeat(300)) + ',' + JSON.stringify("é".repeat(40)) + ']');
var latin = '{"café": "naïve", "ÿ": [" "]}';
check("latin1", latin);
check("latin2", '"\u0080\u009f"');
check("utf16keys", '{"€": 1, "Āā": "Ă"}');
// bad strings
check("badstr1", '["a\nb"]');
check("badstr2", '["\\x"]');
check("badstr3", '["\\u12"]');
check("badstr4", '["abc');
check("badstr5", "['a']");
check("badstr6", '["\t"]');
// whitespace, BOM, literals, structure
check("ws1", ' \t\n\r[ 1 ,\n 2 ] \r\n ');
check("bom1", '﻿[1]');
check("bom2", 'ï»¿[1]');
check("bom3", '["﻿"]');
check("ws2", ' [1]');
check("ws3", '[1] ');
check("lit1", "[true, false, null]");
check("lit2", "[tru]");
check("lit3", "[nul]");
check("lit4", "[True]");
check("lit5", "undefined");
check("struct1", "");
check("struct2", "   ");
check("struct3", "[1,]");
check("struct4", '{"a":1,}');
check("struct5", "[1 2]");
check("struct6", '{"a" 1}');
check("struct7", "{a:1}");
check("struct8", "[1]]");
check("struct9", "[1] x");
check("struct10", "{}{}");
check("struct11", '{"a":1');
check("struct12", "[");
check("struct13", "[[[]]]");
check("struct14", '{"a":{"b":{"c":[{}, [], {"d": []}]}}}');
check("struct15", "// x\n1");
check("struct16", "/* x */ 1");
check("prim1", '"top"');
check("prim2", "  42  ");
check("prim3", "null");
// positions: a 16-bit source and its line/column
check("pos1", '[1,\n 2,\n x]');
check("pos2", '["€€", @]');
check("pos3", '["😀", @]');
// depth
function nest(d) { return "[".repeat(d) + "]".repeat(d); }
var deep = nest(1000), v = JSON.parse(deep), d = 0;
while (Array.isArray(v) && v.length) { v = v[0]; d++; }
expect("depth1000b", d, 999);
check("depth1024", nest(1024));
check("depth1025", nest(1025));
check("depth5000", nest(5000));
check("depthobj", '{"a":'.repeat(1100) + "1" + "}".repeat(1100));
// arrays: shapes
check("arr1", "[1, 2, 3]");
check("arr2", "[1.5, 2, 3]");
check("arr3", '[1, "a", null, true, {}]');
check("arr4", "[[], [[]], [1, [2, [3]]]]");
expect("arr5", JSON.parse("[1,2,3]").push(4), 4);
var big = [];
for (var i = 0; i < 5000; i++) big.push(i % 7 ? i : { k: i, s: "v" + i });
expect("arr6", JSON.stringify(JSON.parse(JSON.stringify(big))) === JSON.stringify(big), true);
// objects stay ordinary and extensible
var o = JSON.parse('{"a": 1, "b": [1, 2]}');
o.c = 3;
expect("obj1", Object.keys(o).join() + Object.isExtensible(o) + Object.isFrozen(o.b), "a,b,ctruefalse");
expect("obj2", Object.getOwnPropertyDescriptor(o, "a").writable && Object.getOwnPropertyDescriptor(o, "a").enumerable && Object.getOwnPropertyDescriptor(o, "a").configurable, true);
// many keys, shared shapes
var many = {};
for (var i = 0; i < 300; i++) many["key" + i] = i;
expect("obj3", JSON.stringify(JSON.parse(JSON.stringify(many))) === JSON.stringify(many), true);
var rows = [];
for (var i = 0; i < 200; i++) rows.push({ id: i, name: "n" + i, tags: ["a", "b"], ok: i % 2 == 0 });
expect("obj4", JSON.stringify(JSON.parse(JSON.stringify(rows))) === JSON.stringify(rows), true);
// reviver: order, deletion, context.source
var seen = [];
JSON.parse('{"a": [1, {"b": 2}], "c": "x"}', function (k, v) { seen.push(k); return v; });
expect("rev1", seen.join("|"), "0|b|1|a|c|");
check("rev2", '{"a": 1, "b": 2}', function (k, v) { return k === "a" ? undefined : v; });
var sources = [];
JSON.parse(' {"a" : -1.50e1 , "b": [ true, "x\\u0041", null, 7 ], "c": {}} ', function (k, v, ctx) {
    sources.push(k + "=" + (ctx && "source" in ctx ? ctx.source : "-"));
    return v;
});
print("rev3 " + sources.join(" "));
sources = [];
JSON.parse('[1, [2, "\\ud800"], {"€": 3.0}]', function (k, v, ctx) {
    sources.push(k + "=" + (ctx && "source" in ctx ? show(ctx.source) : "-"));
    return v;
});
print("rev4 " + sources.join(" "));
sources = [];
JSON.parse('{"a": 1, "a": 2}', function (k, v, ctx) { sources.push(k + "=" + (ctx && "source" in ctx ? ctx.source : "-")); return v; });
print("rev5 " + sources.join(" "));
sources = [];
JSON.parse('[1, 2]', function (k, v, ctx) { if (k === "0") this[1] = 9; sources.push(k + "=" + (ctx && "source" in ctx ? ctx.source : "-")); return v; });
print("rev6 " + sources.join(" "));
check("rev7", "[1, 2", function (k, v) { return v; });
check("rev8", "nest", function (k, v) { return v; });
// JSON.rawJSON
if (typeof JSON.rawJSON === "function") {
    expect("raw1", JSON.stringify({ x: JSON.rawJSON("1e1000") }), '{"x":1e1000}');
    expect("raw2", JSON.stringify(JSON.rawJSON('"a"')), '"a"');
    try { JSON.rawJSON("{}"); print("raw3 no throw"); } catch (e) { print("raw3 throws " + e.name); print("  message raw3: " + e.message); }
    try { JSON.rawJSON("[1]"); print("raw4 no throw"); } catch (e) { print("raw4 throws " + e.name); }
    try { JSON.rawJSON("1 2"); print("raw5 no throw"); } catch (e) { print("raw5 throws " + e.name); }
    try { JSON.rawJSON(" 1"); print("raw6 no throw"); } catch (e) { print("raw6 throws " + e.name); }
    expect("raw7", JSON.isRawJSON(JSON.rawJSON("null")), true);
}
// eval and JSONP-shaped programs still work (their fast paths are gone on the 68k)
expect("eval1", JSON.stringify(eval('({"a": [1, 2], "b": "c"})')), '{"a":[1,2],"b":"c"}');
expect("eval2", JSON.stringify(eval("[1, 'two', {x: 3}]")), '[1,"two",{"x":3}]');
var jsonpResult;
function jsonpCallback(v) { jsonpResult = v; }
(0, eval)('jsonpCallback({"ok": true, "n": [1, 2]});');
expect("jsonp1", JSON.stringify(jsonpResult), '{"ok":true,"n":[1,2]}');
(0, eval)('var jsonpVar = {"z": 1};');
expect("jsonp2", jsonpVar.z, 1);
// non-string arguments
check("arg1", 123);
check("arg2", null);
check("arg3", true);
check("arg4", { toString: function () { return '{"t": 1}'; } });
check("arg5", undefined);
// a long 8-bit and 16-bit document both ways
var doc = { list: [] };
for (var i = 0; i < 2000; i++) doc.list.push({ i: i, f: i / 7, s: "item " + i, u: i % 50 ? "x" : "€" + i, b: i % 3 == 0, z: null });
var text = JSON.stringify(doc);
expect("long1", JSON.stringify(JSON.parse(text)) === text, true);
var text16 = JSON.stringify({ k: "€", doc: doc });
expect("long2", JSON.stringify(JSON.parse(text16)) === text16, true);
print("checks " + n + " bad " + bad);
print("PASS");
