// GFOpenBrowser "real page" JavaScript benchmark. Each part mimics work that
// web pages and their frameworks do, without needing a DOM.
// MIT, Copyright (c) 2026 The AmiCradle Team.
if (typeof print === "undefined") var print = console.log;
function run(name, f) {
    var t = Date.now(), r = f();
    print("JSBENCH page-" + name + " " + (Date.now() - t) + " (" + r + ")");
}
