# WebKit for GFOpenBrowser

WebKit is not copied into this repository. Use the pinned upstream revision
in `UPSTREAM` and apply the patches in `patches/` in order.

```sh
git clone --filter=blob:none --no-checkout https://github.com/WebKit/WebKit.git
cd WebKit
git sparse-checkout set --cone CMakeLists.txt Source/cmake Source/WTF Source/bmalloc Source/JavaScriptCore Source/WebCore Source/ThirdParty Tools
git checkout 54fe1539718eef2f24d813019949ca8cd4b9e89e
git apply /path/to/gfopenbrowser/webkit/patches/0001-amiga-m68k-jsconly.patch
git apply /path/to/gfopenbrowser/webkit/patches/0002-amiga-webcore.patch
git apply /path/to/gfopenbrowser/webkit/patches/0003-jsc-llint-prototype-cache-big-endian.patch
git apply /path/to/gfopenbrowser/webkit/patches/0004-amiga-webcore-network.patch
git apply /path/to/gfopenbrowser/webkit/patches/0005-utf16-big-endian.patch
git apply /path/to/gfopenbrowser/webkit/patches/0006-amiga-disk-webp-quiet.patch
git apply /path/to/gfopenbrowser/webkit/patches/0007-amiga-host-connections-llint-o2.patch
git apply /path/to/gfopenbrowser/webkit/patches/0008-jsc-for-in-big-endian.patch
git apply /path/to/gfopenbrowser/webkit/patches/0009-amiga-stop-run-loops-at-exit.patch
git apply /path/to/gfopenbrowser/webkit/patches/0010-amiga-fetch-proxy-experiment.patch
# then the rest the same way, in number order (0030 last)
```

The patches, in order:

- `0002-amiga-webcore.patch`: WebCore's Amiga port (`PORT=Amiga`): build
  options, the platform files (screen, keyboard, pasteboard, MIME types,
  user agent, fonts), pictures through the system's datatypes, cairo
  drawing without accelerated compositing, and the network layer over
  WebCore's curl backend. Notes are in `../docs/WEBCORE_PORT.md`.
- `0003-jsc-llint-prototype-cache-big-endian.patch`: a JavaScriptCore fix
  for big-endian 32-bit CPUs; without it, methods found on a prototype
  (`a.push`, `s.concat`) read back as `undefined` after their first cached
  call.
- `0004-amiga-webcore-network.patch`: the network layer running on AmigaOS:
  the curl delegate's type, no large-file requirement (libnix's `off_t` is
  32-bit), stopping curl's thread before the program exits, and settings
  for slow TLS handshakes on a 68k (X25519 and P-256 key exchange, one
  connection per host and four in all, a 120 s connect timeout).
- `0005-utf16-big-endian.patch`: fixes for big-endian CPUs, where 16-bit
  strings were converted to UTF-8 as if little-endian (garbling any text
  beyond Latin-1) and the HTML fast-path parser could not find tags in
  16-bit text.
- `0006-amiga-disk-webp-quiet.patch`: the cookie database keeps its journal
  in memory instead of on the disk, WebP sizes come from the file's header
  so a picture is decoded only when drawn, and a missing picture no longer
  writes a warning to stderr.
- `0007-amiga-host-connections-llint-o2.patch`: two connections per host
  instead of one (one left pictures queued long enough to time out), and
  the C-loop interpreter, which runs every line of JavaScript, built with
  `-O2`.
- `0008-jsc-for-in-big-endian.patch`: `for (k in o)` loops end. The
  interpreter's pointer comparisons (`op_jeq_ptr`, `op_jneq_ptr`) read
  half of an 8-byte value on big-endian CPUs with 32-bit pointers, so a
  for-in loop never saw the value that ends it: it ran forever, and reading
  `o[k]` inside it crashed the program.
- `0009-amiga-stop-run-loops-at-exit.patch`: libpthread waits for every
  thread when a program exits, and a work queue's thread waits for work for
  ever, so a program that had made one never exited. The generic run loop
  keeps a list on AmigaOS, and `amigaStopAllRunLoops()` stops them all.
- `0010-amiga-fetch-proxy-experiment.patch`: the host-fetch experiment.
  With the Shell variable `OB_FETCH_PROXY` set (`scripts/ob-fetch-proxy.py`,
  removed with 0075), requests go to a proxy on another PC, which makes the
  TLS connections. Off unless the variable is set. It is in `CurlContext.cpp`,
  which 0075 no longer builds; openweb.fetch's card does the same job.
- `0019-jsc-generators-in-python.patch`: JavaScriptCore builds without
  Ruby. offlineasm (front end and the M68K back end), the bytecode generator
  and GeneratePreferences run as Python 3 ports (standard library only), in
  `Source/JavaScriptCore/offlineasm-py`, `Source/JavaScriptCore/generator-py`
  and `Source/WTF/Scripts/GeneratePreferences.py`. Their output matches the
  Ruby scripts' byte for byte. The 68k build always uses the M68K back end
  (the `JSC_M68K_LLINT` option is gone). A step that still asks for Ruby
  runs `no-ruby.py`, which names the script and fails. (0018 is the visit
  cache, still a draft.)

- `0020-jsc-m68k-interpreter-data-registers.patch`: J1. The 68k
  interpreter keeps t0-t3's low words in d4-d7 instead of its register file
  in memory (58% fewer register-file accesses). jsbench on the AC090:
  int-loop and properties 1.4 times faster, array sort 1.1 times.

- `0021-webcore-generators-in-python.patch`: WebCore builds without Ruby.
  `generate-abstract-heap.rb`, `GenerateSettings.rb` and
  `GenerateQuirkBehaviors.rb` run as Python ports beside them, their nine
  templates as Python forms (`.pyerb`); the outputs match the Ruby scripts'
  byte for byte. No build step of the Amiga port runs Ruby now.

- `0022-amiga-webcore-curl-opentls.patch`: the engine without OpenSSL. curl
  does TLS through GFOpenTLS (gfopencurl's main branch), so `USE(OPENSSL)`
  is off and WebCore links no AmiSSL: curl's OpenSSL-only files are left
  out, and no cipher list, curve list or CA file is set (GFOpenTLS picks its
  own suites and reads `S:GFOpenTLS/ca-bundle.pem`). Responses carry no
  certificate details, and WebCrypto (`crypto/amiga/CryptoAmiga.cpp`)
  offers the SHA-1 and SHA-2 digests only.

- `0023-amiga-webcore-http2.patch`: HTTP/2. curl (gfopencurl, built with
  nghttp2) is asked for `CURL_HTTP_VERSION_2TLS` on https://, HTTP/1.1
  when the server does not offer h2, and the multi handle multiplexes
  (`CURLPIPE_MULTIPLEX`): requests to one server are streams on one
  connection, one TLS handshake per server. The programs link
  `libnghttp2.a` after curl.

- `0024-jsc-m68k-template-jit.patch`: J2's first stage, the 68k template
  JIT. A hot loop's CodeBlock is compiled by copying, for each bytecode
  instruction, the 68k interpreter's handler for it as a template
  (`offlineasm-py/m68k_templates.py`) with the instruction's operands written
  in (`llint/M68KTemplateJIT.cpp`). The copies run on the interpreter's own
  frames and registers and fall through into each other; op_loop_hint enters
  the code and `jsc_m68k_tjit_redispatch` leaves it. On by default since
  0029; `JSC_useM68KTemplateJIT=0` turns it off. Tools and the qemu test are in
  `../tools/jsjit/`.

- `0025-amiga-eclock-monotonic-time.patch`: `MonotonicTime::now()` on
  AmigaOS 3 reads timer.device's E-clock (`ReadEClock`) instead of libnix's
  `clock_gettime()`, which is `DateStamp()` (the wall clock in 50ths of a
  second) followed by a floating-point division and a comparison on every
  call. `amigaMonotonicMicroseconds()` gives the same clock in whole
  microseconds, and `RunLoop::cycle()` keeps the next timer on it as a
  plain integer (`amigaRunLoopNextFireMicroseconds()`), so the programs
  work out their waits without floating point or libatomic.

- `0026-amiga-webcore-http-disk-cache.patch`: a hook for the browser's HTTP
  disk cache (`setAmigaHttpCache()` in `AmigaNetwork.h`). Before a request
  goes out the browser may answer it from a fresh stored copy, or add
  `If-None-Match`/`If-Modified-Since`; a 304 is then answered with the
  stored copy, and other responses are handed to the cache as they arrive.
  The cache itself is `src/webcore/ob_httpcache.c` (see
  `../docs/WEBCORE_PORT.md`).

- `0027-jsc-m68k-jit-int32-fast-paths.patch`: J3, 32-bit integer fast
  paths in the template JIT. For mov, add, sub, mul, inc, dec, the bit ops,
  jmp, loop_hint and the compare-and-jump ops the JIT writes its own 68k
  code: it checks the operands are int32s, works in 32-bit registers and
  stores the result; anything else runs the instruction's template, kept
  out of line. Branches between instructions are direct. JSValues stay
  64-bit in memory. `JSC_useM68KFastPaths=0` turns it off for comparison.

- `0028-amiga-webcore-worker-indexeddb-identifier.patch`: a worker's global
  scope builds an IndexedDB connection proxy even though this single-process
  port has no IndexedDB server, and `IDBConnectionToServer::identifier()`
  dereferenced the empty identifier of `EmptyDatabaseProvider`'s delegate,
  which aborted the engine (reddit's reCAPTCHA worker). On AmigaOS the
  delegate now answers with `Process::identifier()`; requests still go
  nowhere.

- `0029-jsc-m68k-template-jit-on-by-default.patch`: `useM68KTemplateJIT`
  defaults to true, so engine builds run the template JIT and its fast
  paths everywhere. `JSC_useM68KTemplateJIT=0` runs the interpreter alone.

- `0030-amiga-xml-parser-oxp.patch`: XML (SVG, XHTML, `DOMParser`,
  `responseXML`) through OXP, the Team's own XML parser
  (DalsinAI/gfopenxml, `oxp/`), in place of libxml2 (`USE(OXP)`).
  `XMLDocumentParserOXP.cpp` is WebKit's libxml2 backend moved onto OXP.
  `XMLDocumentParserOXPBridge.h` gives `XMLDocumentParser` OXP's events in
  libxml2's shapes, and is tested on the host against libxml2
  (`../tests/xml-oxp/`). No external entity or DTD is loaded; XSLT and EME
  stay off.

- `0031-jsc-m68k-jit-functions-properties-doubles.patch`: J4. Whole
  functions: the function prologues enter a function's code once it has
  been called often enough, not only its loops. Calls: op_enter, op_ret,
  op_call and op_construct get their own code (a call to the callee its
  call link holds goes straight in and returns into compiled code). The 68k interpreter stops filling value, arithmetic
  and argument profiles, which only the optimizing JITs read. Property
  access: get_by_id and put_by_id get their own code for sites whose
  interpreter cache holds an own property (`JSC_useM68KPropertyFastPaths=0`
  turns it off). Doubles: add, sub, mul and the compare-and-jumps run on the
  FPU when an operand is a double or an int32 result overflows. Double
  arithmetic, in the JIT and the interpreter, uses the 68040's
  fdadd/fdsub/fdmul/fddiv/fdsqrt, which round once to double as
  JavaScript asks.

- `0032-jsc-m68k-jit-loop-registers.patch`: J6. A loop whose body runs on
  int32s alone (moves, inc/dec, add/sub/mul/and/or/xor, the
  compare-and-jumps, get_by_id and put_by_id) gets a second copy that keeps its busiest variables in
  d2-d7. A preheader checks and loads them; every write also goes to the
  frame, so the copy can leave for the plain code at any point and comes
  back at the next back edge. `JSC_useM68KRegisterCache=0` turns it off.
  Calls into JavaScript from C++ (sort comparators, map, forEach, getters)
  cost less: the 68k entry keeps only the register slots a C call must
  give back, copied with move.l instead of memcpy. Calls from compiled
  code to a compiled function skip the interpreter's prologue: each
  function's code starts with its own.

- `0033-jsc-m68k-thread-registers.patch`: each thread that runs
  JavaScript keeps its own interpreter registers. The 68k interpreter's
  register file is one global, and AmigaOS switches threads preemptively,
  so a worker and the main thread overwrote each other's sp, lr and t
  registers (reddit's reCAPTCHA worker died in the stack check, and the
  page waited for it for ever). exec's tc_Switch and tc_Launch hooks save
  and restore the register file per thread.

- `0034-jsc-m68k-jit-arrays-calls-in-loops.patch`: get_by_val, put_by_val
  and get_length get their own code for int32 and contiguous arrays
  (in-bounds, no holes; anything else is the template). Loop register
  copies also cover array access and calls (registers reloaded after the
  call). A nested entry from C++ saves only sp and lr.

- `0035-jsc-m68k-jit-compares-objects-scopes.patch`: fixes the sp left at
  the callee's frame after a JIT call (since 0031), which let a garbage
  collection clear live slots. Gives the JIT its own code for ===, !==,
  == null, jtrue/jfalse and int32 compares as values, put_by_id
  transitions (new properties, with the prototype chain check),
  polymorphic and unused-result calls, and resolve_scope, get_from_scope
  and put_to_scope for globals, global let/const and closure variables.
  `StructureChain.h` gains `offsetOfVector()` for the chain check.

- `0036-jsc-m68k-jit-shapes-strings-doubles.patch`: get_by_id sites keep a
  four-entry structure table (own values, getters on the object or an
  unchanged prototype, missing properties), cleared of dead structures at
  the end of a garbage collection (`CodeBlock.cpp` calls
  `M68KTemplateJIT::reconcileCaches`). `s[i]`, string `.length` and
  `charCodeAt` get their own code (`VM.cpp` keeps charCodeAt's intrinsic
  on the 68k C loop), and loop register copies keep doubles in fp2-fp6.

- `0037-webcore-webgpu-only-when-enabled.patch`: WebGPU's bindings,
  module and inspector paths build only with `ENABLE(WEBGPU)`, which the
  Amiga port leaves off (its IDLs had no Conditional, so about 1.5 MB of
  unused code was in the engine). `getContext("webgpu")` returns null and
  `navigator.gpu` is absent, as before at run time.

- `0038-jsc-m68k-jit-inline-small-functions.patch`: call sites whose
  call link holds a small JavaScript function (64 bytecode bytes or less,
  `m68kTemplateJITMaximumInlineSize`) get its code in line, behind a guard
  on the CodeBlock; the inlined function may call, and anything its fast
  code does not take finishes the frame and goes on in its own code.
  op_to_this gets fast code. `useM68KInlining` turns it off.
- `0039-jsc-m68k-jit-page-hot-spots.patch`: fast code for `index in array`,
  `arguments.length` and `arguments[i]`, for-in's next name, and `===` on
  atom strings; `CodeBlock::bytecodeOffset` takes the CodeBlock's raw
  instructions pointer on 68k, which makes every slow path's
  setCurrentVPC cheaper.
- `0040-webcore-unused-web-apis-only-when-enabled.patch`: WebTransport,
  the Cookie Store API, Web Locks, the Screen Wake Lock API, the Shape
  Detection API and speech recognition get build flags
  (`ENABLE_WEB_TRANSPORT`, `ENABLE_COOKIE_STORE_API`, `ENABLE_WEB_LOCKS`,
  `ENABLE_SCREEN_WAKE_LOCK`, `ENABLE_SHAPE_DETECTION`,
  `ENABLE_SPEECH_RECOGNITION`, on by default), and the Amiga port turns
  them off. Their settings were already off, so pages see no change.
- `0041-webcore-web-sql-and-text-extraction-only-when-enabled.patch`: Web
  SQL (`ENABLE_WEB_DATABASE`) and the page text extraction behind element
  targeting (`ENABLE_TEXT_EXTRACTION`) get build flags, on by default; the
  Amiga port turns them off. `openDatabase` was already off by setting.
- `0042-jsc-m68k-typed-array-sort-shared.patch`: on 68k, TypedArray
  `sort` and `toSorted` share one out-of-line sort per array type (they
  were each inlining it, in different files): 467 KB less code.
- `0043-jsc-m68k-jit-virtual-calls.patch`: a fix for 0035's polymorphic
  calls. A virtual call link (a site with more callees than a polymorphic
  stub holds, or a construct site whose callees have no call CodeBlock)
  was taken for a polymorphic stub and crashed; it now takes the template.
- `0044-jsc-m68k-jit-object-allocation.patch`: `new_object` (`{}` and
  object literals) and `create_this` (`new F()` on a JS function) make the
  object in line from the allocator's free list, as the baseline JIT does,
  instead of calling into C++.
- `0045-jsc-m68k-conservative-scan-halfwords.patch`: the garbage
  collector's conservative scan of the machine stack reads a candidate
  pointer at every halfword on the 68k, where pointers are only 2-byte
  aligned. It used to read only every 4 bytes and could miss a live
  object that C++ code still held, which then got freed (crashes and
  wrong values under GC pressure, with and without the JIT).
- `0046-jsc-m68k-scalar-string-scans.patch`: WTF's SIMD::find and
  reverseFind take their scalar loop on the 68k, which has no vector unit
  (the compiler emulated each 16-byte vector a byte at a time). JSON.parse,
  the JavaScript lexer, the URL parser and String find gain; page-json runs
  26% fewer instructions.
- `0047-jsc-m68k-jit-array-push.patch`: `array.push(value)` is done in
  line in the template JIT for Int32 and Contiguous arrays with room in
  their vector, instead of a call into C++.
- `0048-jsc-m68k-jit-math-min-max.patch`: `Math.min(a, b)` and
  `Math.max(a, b)` of two int32s are done in line in the template JIT.
- `0049-jsc-m68k-jit-new-array.patch`: `[]` and `[a, b, ...]` (up to
  eight elements) are made in line in the template JIT, butterfly and
  cell from the free lists, instead of a call into C++.
- `0050-jsc-m68k-jit-direct-arguments.patch`: `arguments` objects
  (DirectArguments) are made in line in the template JIT, with up to 32
  slots, instead of a call into C++.
- `0051-jsc-regexp-interpreter-first-character.patch`: the regular
  expression interpreter (the 68k has no regex JIT) skips start
  positions whose character the pattern cannot start with, in 8-bit
  strings, instead of trying the whole pattern at each one. Changes
  YarrInterpreter.h, so the files that include RegExpInlines.h rebuild.
- `0052-jsc-m68k-jit-array-append.patch`: the template JIT's `a[i] = v`
  also stores one past the end of an array with room, growing its length
  (as the interpreter does), and the array builtins' put_by_val_direct
  (the results of filter, map and friends) takes the same fast path.
- `0056-amiga-plain-inline.patch`: `ALWAYS_INLINE` is a plain inline on
  the Amiga port, so GCC inlines only where it pays at -Os, except in
  JavaScriptCore, WTF and WebCore's style, layout and DOM code (css/,
  dom/, layout/, rendering/, style/, marked `WTF_AMIGA_FORCE_INLINE` by
  WebCore's `PlatformAmiga.cmake`), where speed is spent. The stripped
  engine is 1.16 MB smaller; the DOM bench runs as fast.
- `0057-amiga-no-inspector.patch`: no Web Inspector. The port has no
  inspector frontend, local or remote, but the inspector was compiled and
  linked all the same (2.8 MB). WebCore's `inspector/` folder and its
  bindings are left out of the build (`WebCore_UNIFIED_SOURCE_EXCLUDES`),
  and of JavaScriptCore's only what the console uses is built (console
  messages, their arguments and call stacks). `InspectorInstrumentation`
  returns at once, pages, frames and workers have no inspector
  controllers, canvas calls are never traced, and the heap has no
  injected-script subspaces. Console messages still reach the
  ChromeClient. The stripped engine is 3.26 MB smaller.
- `0058-amiga-json-parse-json-library.patch`: `JSON.parse` (with or
  without a reviver), `JSON.rawJSON` and JavaScriptCore's `JSONParse` parse
  through json.library (`src/jsonlib`) on the 68k, the browser's and
  javascript.library's engine alike, and LiteralParser and the eval and
  JSONP fast paths built on it are left out: about 1.2 MB less engine.
  `LIBS:json.library` (version 1) must be installed; without it
  `JSON.parse` throws an Error that says so. Also checks the stack in the
  fast `JSON.stringify` on the 68k (no guard page below the stack).
- `0059-amiga-cold-code-for-size.patch`: code pages seldom run is built
  for size. JavaScriptCore's Intl, Temporal and typed array prototype
  functions, C API, debugger, profilers, tools, the inspector's console
  part, the builtins' executables and the bytecode dumper get -Os and a
  plain inline (`WTF_AMIGA_PLAIN_INLINE`, set per source by
  JavaScriptCore's `PlatformAmiga.cmake`) while the rest of
  JavaScriptCore keeps -O2; WebCore's `rendering/svg/`,
  `rendering/mathml/` and `css/typedom/` lose the forced inlining 0056
  keeps for style, layout and DOM code. The cold objects go last in the
  JavaScriptCore library, so an inline function both kinds of code use out
  of line keeps its -O2 copy. On main 71d58e6 the stripped engine is
  1,858,488 bytes smaller and the JavaScriptEngine image 1,843,036 bytes;
  page times are unchanged.
- `0060-amiga-unused-features-not-built.patch`: Web platform features
  the browser switches off at run time are not built: accessibility,
  IndexedDB, service workers (with the Push and Cache APIs), shared
  workers, the File System and Entries APIs, the Async Clipboard API and
  credential management. Each has a feature flag (`ENABLE_ACCESSIBILITY`,
  `ENABLE_INDEXED_DATABASE`, `ENABLE_SERVICE_WORKER`, `ENABLE_PUSH_API`,
  `ENABLE_CACHE_API`, `ENABLE_SHARED_WORKER`, `ENABLE_FILE_SYSTEM_API`,
  `ENABLE_ASYNC_CLIPBOARD`, `ENABLE_CREDENTIAL_MANAGEMENT`; on by default)
  that the Amiga port turns off; `PlatformAmiga.cmake` leaves their IDL
  files, generated bindings and sources out, and the code that called them
  compiles them out (a document has no accessibility object cache, no
  IndexedDB or service worker connection). Also drops the last pieces of
  Geolocation and WebTransport, which were off but partly built. Headers
  that are still compiled keep the dropped interfaces' type casts and
  interface types under the flags, and two small Amiga files
  (`AccessibilityDisabledAmiga.cpp`, `IndexedDBDisabledAmiga.cpp`) answer
  the few functions code outside the dropped folders names but never
  reaches. `DataTransferItem.webkitGetAsEntry()` goes; nothing else pages
  could reach changes. The stripped engine is 4,309,716 bytes smaller.
- `0061-amiga-file-urls-amigados-paths.patch`: the port's file loader
  (`ResourceHandle::amigaLoadFile`, 0002) reads a file: URL from its
  AmigaDOS path: `file:///Volume:dir/file` and `file:///Volume/dir/file`
  are `Volume:dir/file` (PROGDIR: and assigns as well), where it used to
  keep the URL's leading "/", which AmigaDOS reads as the parent
  directory. One name stays relative to the current directory, and
  `dir/file` that exists relative to it is read there first.
- `0062-amiga-media-error-without-video.patch`: `MediaError` is built
  and exposed on `window` without `ENABLE_VIDEO` (it was
  `Conditional=VIDEO`), so pages that test `instanceof MediaError` or
  read its constants run instead of stopping with a ReferenceError.
  VIDEO stays off; nothing makes a MediaError.
- `0063-amiga-datatype-blank-pictures.patch`: two kinds of picture the
  datatypes drew blank. An APNG with acTL straight after IHDR (claimed by
  the animation descriptor) goes to the datatype without its animation
  chunks, as the PNG of its default image. A GIF whose first frame does not
  cover the screen goes as that frame alone and is put back in a
  screen-sized picture, transparent around it.
- `0070-amiga-public-suffixes-without-libpsl.patch`: public suffixes
  (cookies, sites, `document.domain`) from our own reader of Mozilla's
  Public Suffix List (`PublicSuffixAmiga.c`) in place of libpsl, which is
  no longer linked. The list is `PROGDIR:Network/public-suffixes`, made by
  `tools/psl-compile.py` (`data/public-suffixes`, in the package); a newer
  list needs no new engine. Host test: `tests/test_public_suffixes.c`,
  the list's own checks, in `tests/run-host-tests.sh`.
- `0071-amiga-no-curl-websocket-stream.patch`: curl's WebSocket stream
  (`CurlStream`, `CurlStreamScheduler`) is not built. It serves WebKit's
  network process, which the port does not have; pages get no WebSocket
  channel today (`EmptySocketProvider`), so nothing changes.
- `0072-amiga-network-types-without-curl.patch`: `ResourceError` gets an
  Amiga domain (`AmigaNetworkErrorDomain`, `ResourceError::AmigaCode`):
  openweb.fetch's errors 1 to 9 and the loader's own, with curl's codes
  mapped onto them; `isCertificationVerificationError` reads it.
  `ResourceResponse` is built from a plain response head
  (`AmigaResponseHead`) and `AuthenticationChallenge` from a
  `ResourceResponse`. curl still does every load, as before.
- `0073-amiga-openweb-fetch-backend.patch`: loads through openweb.fetch
  beside curl (`platform/network/amiga/`): `WebFetchAmiga.cpp` runs them on
  one network thread with `wf_async_start` and `wf_wait`, through a plain C
  glue (`OpenWebFetchAmiga.c`); `FormData` bodies go through `wf_send`.
  `ResourceHandleAmiga.cpp` keeps redirects, cookies and the HTTP cache for
  both. The Shell variable `OB_NET` picks it (`SetEnv OB_NET openweb`);
  curl is the default (until 0075, which makes it the only backend). Basic and Digest credentials go in the URL; proxies
  are the transport's. Bodies are decoded only when the transport decodes
  (the card); the native transport sends `Accept-Encoding: identity` until
  the datatypes stream decodes. Needs gfopenservice with the
  asynchronous calls (protocol 2); without them the engine has curl only.
- `0074-amiga-png-out-through-datatype.patch`: canvas `toDataURL` and
  `toBlob` write PNG through a PNG datatype's write (`DTM_WRITE` with
  `DTWM_RAW`, by `SaveDTObjectA()`, gfopenpng.datatype first, then
  png.datatype), not cairo's libpng writer; the browser writes no PNG itself
  and WebCore no longer finds or links libpng. Without a PNG datatype that
  writes, the picture is not encoded. `obcore-dump` writes its page PNG the
  same way.
- `0075-amiga-no-curl.patch`: curl out. openweb.fetch does every load
  (`OB_NET` is gone): `ResourceHandleAmiga.cpp` loses the curl delegate and
  keeps redirects, cookies, the HTTP cache, `file:` and synchronous loads.
  `platform/Curl.cmake` is not included; `PlatformAmiga.cmake` lists the
  curl folder's files that stay (the network types, `CookieJarDB`,
  `CookieUtil`, the empty DNS queue), whose curl parts are left out on
  `PLATFORM(AMIGA)`; `USE(CURL)` stays on to pick those types. `CurlContext`
  (with 0010), `CurlRequest`, `CurlRequestScheduler`, `CurlFormDataStream`,
  `CurlMultipartHandle`, `CurlProxySettings`, `CurlSSLHandle` and
  `CurlSSLHandleAmiga.cpp` are not built (19 files, 4,057 lines), and curl
  and nghttp2 are no longer found or linked. `multipart/x-mixed-replace`
  arrives as one body. Every load asks for `WF_DECODE`, so pages come
  compressed as with curl: the card's host decodes, and the native
  transport decodes through stream.datatype, gzip.datatype and
  brotli.datatype (needs gfopenservice #15). `OpenWebFetchAmiga.c` needs
  gfopenservice's headers; there is no build without them.
- `0076-amiga-graphicscontext-opengpu.patch`: the window is painted through
  `GraphicsContextOpenGPU` (`platform/graphics/amiga`), which
  `AmigaWebView::paint()` uses unless the Shell variable `OB_PAINT=cairo`
  is set or gfopengpu.library is missing or has no `COMPOSITE_PD`. Solid
  rectangles, `clearRect`, rectangle clips on whole pixels and pictures go
  into one GFOpenGPU batch per paint (`FILL`, `COMPOSITE_PD`, and a
  `PATH_FILL` with the picture as a pattern when it is scaled or turned);
  every other call goes to a `GraphicsContextCairo` on the same pixels and
  state, after the batch is drawn, and is counted by kind. The stream is
  written by `OpenGPUPaintAmiga.c`, plain C tested on the host against
  gfopenrtg's CPU core (`tests/test_opengpu_paint.c`);
  `OpenGPULibraryAmiga.c` opens the library at run time through
  gfopenrtg's inline calls, so nothing more is linked. Both C files
  need gfopenrtg's headers.
- `0077-amiga-opengpu-paths.patch`: `GraphicsContextOpenGPU` draws
  `fillPath`, `strokePath`, `strokeRect`, `fillRoundedRect`,
  `fillRectWithRoundedHole` and gradient fills itself, through
  `PATH_FILL` and `PATH_STROKE` (stream v1.6), and rectangles of one
  colour off whole pixels or turned as anti-aliased paths. Paths are read
  from WebCore's `Path` elements; strokes take cairo's width, caps, joins,
  miter limit and dashes from its context; linear and radial gradients
  (pad, repeat, reflect) where every stop has one alpha. By source-over,
  source-atop, destination-over, destination-out, xor and plus-lighter.
  Clips to paths, patterns, conic gradients, stops of different alphas,
  copy and clear on paths and paths without anti-aliasing stay cairo's.
  Scratch grows to 16 KB; a path larger goes to cairo. Tested on the host
  (`tests/test_opengpu_paint.c`; `tools/patch-files.py` applies 0077 to
  0076's files).
- `0078-amiga-opengpu-clips.patch`: clips to paths (`clipPath`,
  `clipOut` of a path or a rectangle, rounded rectangle clips) and
  rectangle clips off whole pixels or turned are A8 masks drawn by
  `PATH_FILL` with `IN` (clip-outs `OUT_REVERSE`), kept until the level's
  restore, and everything drawn while they are the clip goes through them
  (`COMPOSITE_PD`'s mask, `PATH_MASK`). Shapes with anti-aliasing off use
  stream v1.8's `OGPU_PATH_ALIASED` (gfopengpu.library 0.23 and later).
  Copy and clear on paths and through masks as cairo bounds them: the
  destination times 1 - the coverage, then the paint times the coverage
  added. Tested on the host against cairo 1.18 drawing the same calls
  (`tests/test_opengpu_clip.c`) and with 0076's and 0077's checks.
- `0090-amiga-text-openfont.patch`: text on gfopenfont.library 1.3
  (`USE_OPENFONT`) in place of FreeType, fontconfig and HarfBuzz, which are
  no longer found or linked. Families are matched with GFOpenFont's
  `OFA_Exact`, with the aliases the old `fonts.conf` gave kept in
  `FontCacheOpenFont.cpp`; glyphs are drawn through cairo user fonts that
  fill GFOpenFont's outlines (cairo built with `CAIRO_FONTS=user`, no
  FreeType backend); metrics follow FreeType's rules from the raw tables;
  complex runs are itemised by script with ICU and shaped with
  `OF_ShapeUTF16`. Emoji in fonts that keep them as PNG images (CBDT,
  sbix) are drawn through cairo's colour glyph callback from
  gfopenfont.library 1.4's `OF_GlyphColorBitmap`, which decodes them through
  the PNG datatype, so neither FreeType nor libpng is linked.
  `LIBS:gfopenfont.library` 1.3 must be installed; 1.4 for emoji.
- `0091-amiga-openfont-shape-lock.patch`: the complex text path holds
  `OpenFontFace::lock()` only while it shapes, not while it asks cairo for
  glyph extents. cairo takes its scaled font's lock and then renderGlyph
  takes this one, so holding both the other way round could deadlock two
  tasks drawing the same font.
- `0092-amiga-openfont-library-from-drawer.patch`: without
  `LIBS:gfopenfont.library`, the engine also opens the package's copy from
  `PROGDIR:/`, the drawer's `Libs/` above the engine's `Libs/WebBrowser/`.
  Before this it found no library there, so no face, and stopped at
  `lastResortFallbackFont`'s assert on most pages.
- `0093-webcore-openfont-program-fonts.patch`: 0092 was not enough. ramlib
  resolves `OpenLibrary("PROGDIR:...")` in its own process, where the
  engine's `PROGDIR:` means nothing, so the package's copy never opened. The
  library is now opened by its full name, from `PROGDIR:/`, `PROGDIR:Libs/`
  or `PROGDIR:Libs/WebBrowser/Libs/`. It is opened by its name alone only
  when it is in memory or in `LIBS:`: a ramlib search that finds nothing
  took 65 ms of every cold start. The package's fonts are looked for in
  `PROGDIR:Fonts` and `PROGDIR:Libs/WebBrowser/Fonts`, for a program with
  the engine in it that runs from the browser's drawer (obcore-view).
  Adding them is tried again on the next lookup when it was first asked
  from a plain task or the library refused. What isn't found is printed
  on stderr.
- `0098-amiga-request-blocked-hook.patch`: `setAmigaRequestBlockedHook()`
  (`AmigaNetwork.h`) lets the browser say whether a request goes out at
  all. `ResourceHandle::amigaStartRequest()` asks it for every http request
  and redirect, with the page's site; a blocked one fails as cancelled from
  the run loop. GFOpenBrowser answers from Tata's ad and tracker rules
  (`src/webcore/ob_tata.c`).
- `0104-jsc-m68k-jit-sooner.patch`: functions and loops go to the 68k
  template JIT sooner: `thresholdForJITAfterWarmUp` is 100 on the 68k
  (500 elsewhere), so a function is compiled on about its seventh call and a
  loop at its 100th iteration. Code compiled while some of its sites'
  inline caches were still empty notes those cache words; once one has
  filled (checked at the end of each collection) the code is compiled once
  more, after `m68kTemplateJITRecompileThreshold` (100) more counts in the
  interpreter. The replaced code is kept until its CodeBlock dies.
  `JSC_useM68KTemplateJITRecompile=0` turns the second compile off, and
  `JSC_reportM68KTemplateJITStatistics=1` prints at exit what was compiled
  and how often the interpreter's property caches missed.

`0001-amiga-m68k-jsconly.patch` is one combined patch. It holds:

- the m68k CPU and AROS platform layer from the AmiCradle AROS m68k port;
- the AmigaOS 3.x platform layer (`OS(AMIGAOS3)`, and `OS(AMIGA)` for code
  shared with AROS);
- the big-endian and m68k-alignment fixes (see `../docs/PORT_NOTES.md`).

Each patched file keeps its WebKit licence header (LGPL-2 or BSD).
