# GFOpenJavaScript integration contract for GFOpenBrowser

**Status:** transition in progress, 19 November 2026. **Standalone RC1 not yet qualified.**

## Ownership

- [DalsinAI/gfopenjavascript](https://github.com/DalsinAI/gfopenjavascript) is the **independent home for releases**, APIs, user-facing binaries, qualification and future canonical engine source.
- GFOpenBrowser retains the pinned JavaScriptCore/WebKit build, WebCore browser integration and the existing `src/jslib` sources **until a tested source migration is complete**.
- GFOpenSDK owns the published `javascript.library` public headers, SFD, FD, and versioned API contract.
- Existing source: GFOpenBrowser `c731d89f6f018ffb2ea4091cd00595b5fbba9e9a`. SDK: `9433cc8d2b1b363b095f0af14e3c114f2aebb22a`.
- No duplicate, independently diverging JavaScript interpreters or untracked source copies.

## Build relationship

| Build/consumer | Required behavior |
| --- | --- |
| `scripts/build-javascript-library.sh` | Retained as compatibility entry point; builds the library and CLI from the current in-tree source until standalone build scripts replace it |
| `scripts/build-webcore.sh build-javascript` | Builds the current JavaScriptEngine against GFOpenBrowser's pinned JSC; future external build must use the **same pinned JSC and ABI** |
| GFOpenJavaScript RC package | Must contain **both** the loader and its matching `JavaScriptEngine`; browser must consume the qualified pair as a unit |
| GFOpenSDK | Source of public v2 SDK declarations; `JS_ClassPrototype` appended at -210, all prior vectors unchanged |
| Existing browser / applications | Keep using original v1 vectors without recompilation; new functionality must check version/callback support |

## Consumer integration gate

1. Build matching `javascript.library` and `JavaScriptEngine` from pinned source/toolchain; record both SHA-256 hashes and source commits.
2. Run ABI-vector checker; ensure v1 layout is identical and the new `JS_ClassPrototype` LVO is -210.
3. Native AmigaOS 3.2.3/AC090 68040: qualified v2 loader plus v2 engine, `JS_ClassPrototype` non-NULL, class/derived prototype identity, `new` and `instanceof`, correct root ownership and finalizers.
4. Verify v1 callers, the standalone `JS` shell, WebCore scripts, Oolite `Vector3D`, error recovery, and bytecode caching without cross-version corruption.
5. Update browser packaging and startup diagnostics so an unsupported/missing engine produces a useful error, not a silent crash or unsupported API call.

**Deployment warning:** a v2 `javascript.library` with a v1 `JavaScriptEngine` may still handle legacy calls. Do not call `JS_ClassPrototype` without verifying support in both sides. The bridge returns NULL for unsupported callbacks; clients must check this outcome.

**Release policy:** no `2.0.0-rc1` tag or published prerelease until the matching engine, native guest and consumer regressions pass.

## Migration sequencing

- [x] Establish GFOpenJavaScript repository and cross-reference in GFOpenBrowser.
- [x] Merge v2 API and GFOpenSDK contract.
- [ ] Extract buildable canonical engine/library source and reproducible build machinery into GFOpenJavaScript, preserving provenance.
- [ ] Update browser's build scripts to consume **pinned standalone source/version**, with a documented compatibility fallback until external builds qualify.
- [ ] Complete native qualification; publish prerelease from GFOpenJavaScript.
- [ ] Retire duplicate engine sources from browser only after all consumers migrate and CI runs against the independent repository.

This is a build and release handoff, not a claim that the source extraction is already complete.
