# Reddit exit hang: reCAPTCHA worker's Structure chain loops (31 Oct 2026, Team 3)

## What happens
On Instance-32 copies (t3-deploy-i32-20261031, full18-t3), about 1 reddit.com load in 3 or 4 leaves obcore-view (or the engine) unable to exit.
1. reCAPTCHA's Web Worker (`https://www.google.com/recaptcha/enterprise/webworker.js` -> `recaptcha__en.js`, 859,260 B) crashes in JavaScriptCore:
   - The runtime log says `Task guard: task "WebCore: Worker" ... wrote at bbadbeef` (WTFCrash).
   - OpenKick's trap code (tc_TrapCode 00f806e4) then parks the task at f8070e: `jsr Wait(0)`, `bra *`.
2. libpthread's exit (`__pthread_Exit_Func -> pthread_join`) waits for that thread for ever. libpthread 0007 (openamigagcc) stops the exit hang; the crash itself is still there.

## Evidence (kept-alive lab f3a-i32-20261031, main package v59c)
- Worker stack, symbolized with the jsimage client's link1.map and JavaScriptEngine's map (image base 4412bde8):
  `WorkerDedicatedRunLoop::run -> MessagePort::dispatchEvent -> JSEventListener -> microtask checkpoint -> runInternalMicrotask -> LLInt CLoop -> llint_slow_path_put_by_id -> JSObject::putDirectInternal -> Structure::addNewPropertyTransition -> takePropertyTableOrCloneIfPinned -> materializePropertyTable -> findStructuresAndMapForMaterialization -> Vector<Structure*,8>::expandCapacity -> WTF::fastMalloc -> CRASH`.
- The fastMalloc argument on the stack was 0x0c5ae790 = 207,284,112 B.
- The Vector in materializePropertyTable's frame (at 4d87c9b4) is {buffer 0x50810518, capacity 0x041e4d30, size 0x020f2698 = 34,547,352}. Every entry read is 0x50483420.
- Structure 0x50483420's m_previousOrRareData (at +0x36, 2-byte aligned as m68k gcc lays it out) holds 0x50483420, its own address. previousID() returns `this`, so findStructuresAndMapForMaterialization appends the same Structure until malloc fails.
- Free fast RAM drops from about 440 MB to about 186 MB during the runaway. Every hung run in 6 labs has the bbadbeef line (main build and fix builds alike).

## Reading
A transition Structure whose previous is itself can only appear if the previous Structure's cell was swept while still in use, and the new Structure was then allocated in that same cell. So the worker's VM loses a live Structure: a GC liveness bug, not a cycle JSC makes on purpose.

Why only i32: i32 has 512 MB of AC090 fast RAM ("standard" mode); i14 has 1,904 MB ("2gb"). The worker heap on i32 collects far more often, so a collection more often lands in the window where the previous Structure is held only where the GC cannot see it.

## Candidate causes, and how to test each
Instrumented JSC: `RELEASE_ASSERT(previous != this)` in the Structure transition constructor (Structure(VM&, Structure* previous, ...)), plus a GC log per cycle: start and end, marked counts, the threads scanned with their stack ranges, and the register buffer captured. Build it in a copy of the build folder (JavaScriptCore + JavaScriptEngine image + jsimage clients), then run reddit on fresh i32 copies until it trips.

1. **Conservative scan of the worker thread.**
   - Is the 0045 halfword stack scan used for every thread (MachineThreads), or only the main one?
   - Do the registers captured for the current thread (libnix setjmp's jmp_buf on m68k) include every callee-saved register gcc may keep a Structure* in (d2-d7, a2-a6)?
   - Test: force a word- and halfword-aligned scan of the whole stack; capture registers with a direct `movem.l d0-d7/a0-a6` into the scanned buffer. If the assert stops tripping, this is the cause.
2. **visitChildren and m_previousOrRareData at +0x36.**
   - Structure::visitChildren marks m_previousOrRareData through WriteBarrier.
   - Check that the field's 2-byte offset reaches the visitor correctly, and that StructureRareData's own previousID is visited too.
   - Test: log in visitChildren when the previous isn't marked after the visit, or assert after marking that previousID() isMarked.
3. **A missing write barrier on the m68k CLoop/JIT paths** (patches 0020-0058: put_by_id, structure transition fast paths).
   - Test: run with the m68k JIT off (`JSC_useJIT=false` or a build without our JIT patches), or force the barrier in the transition paths (`vm.writeBarrier` after setting the new structure). If it stops with the JIT off, bisect the m68k JIT patches.

Note: JSC options from ENV: (JSC_slowPathAllocsBetweenGCs) seemed not to take effect in obcore-view (DOM timings unchanged). Check Options::initialize's non-Cocoa getenv path on the Amiga first, if you want to force GC pressure that way.

## Separate issues seen on the way (not fixed)
- **WTF::sleep never returned.** ParkingLot -> ThreadCondition::timedWait -> libpthread pthread_cond_timedwait: a negative interval (an absolute time already past) waited for ever. Fixed in openamigagcc libpthread 0007.
- **obcore-view's main stack peaks at 1,973,924 of 2,097,152 B (94%) on reddit** (runtime log, "Stack:" line). It runs on oam_run_with_stack(2 MB) in src/webcore/obcore_view.c; the engine probably comes close in the same way.
- **The runtime's "Task guard" logs a write to empty space, and the write is ignored** (no bus error): `*(long*)0xBBADBEEF = 0` alone does not crash on AC090 / i32. WTFCrash's trap after it is what stops the thread.

Labs, dumps and tools: ~/WorkingSpace/fix3-20261031/
- results/f3a-*, f3fixb-*, f3hangb/c-*: summaries, stacks json, jsym output
- hangdump.py, hangrep.py, jsym.py, peek.py

## Update, 31 Oct 23:15 (instrumented builds)
- **SELFPREV confirmed.** A build with a check in `Structure::Structure(VM&, StructureVariant, Structure* previous)` (`previous == this`: one line to `DH1:OBGC.log`, then RELEASE_ASSERT) fired on reddit:
  - gc8c run 1: `structure 0x50031440 vm 0x4d295d58`;
  - gc9 run 2: `structure 0x4db73420 vm 0x48e6c090`.
  The transition Structure gets its own cell as "previous". So the previous Structure was swept while the code making the transition still held it: a liveness loss, as read above.
- **Main-thread scan range is right.** Thread 0x40d0aa20 scanned `0x40940464-0x40940fd4` in a run whose main task stack was `4073fbe4-4093fbe4`: the origin is tc_SPUpper. The main thread just sits about 3 KB deep in its run loop. libnix's setjmp saves d2-d7/a2-a7 (`movem.l d2-d7/a2-sp`), so all callee-saved integer registers are captured.
- **Template JIT off: unproven.** `useM68KTemplateJIT=false` was added to the `setOptions` string in src/webcore/ob_webview.cpp (experiment branch exp/gc-nojit, not pushed). SELFPREV still fired, but dom's JS time didn't change (1.50-1.56 s), so the option may not have taken. Next step: print `Options::useM68KTemplateJIT()` after `JSC::initialize`, and check `setOptions`' return value.
- **JSC options from the environment don't reach obcore-view.** `SetEnv`/`Set JSC_useM68KTemplateJIT false` and `JSC_slowPathAllocsBetweenGCs` change nothing. Set options in code instead.
- **Logging from the worker stalls the page.** The first instrumented build (dataLog of every GC from the worker and main threads) stalled reddit. Keep logging to the rare event, or use a memory ring dumped at the event.
- **One instrumented hang had the main task in Wait(0) too** (gc8c). Its SP was near the worker's, and the worker's SP sat inside the main's stack range. Worth a look: whether a RELEASE_ASSERT inside StackSwap'd code (oam_run_with_stack) parks differently.

Builds (copies, on dev):
- ~/WorkingSpace/fix3-20261031/v59l: main c72b167 + SELFPREV check, linked with libpthread 0007.
- f9files/: the instrumented Structure.cpp.
- exp1.bundle: the JIT-off experiment.

## Update, 1 Nov 03:35 (time-boxed narrowing, all with the m68k template JIT off via OBJSC unless noted)
Each row is reddit runs on i32 with the SELFPREV assert.

| change | runs with SELFPREV |
|---|---|
| template JIT off (useM68KTemplateJIT=false, confirmed in the log) | 6/6, then 9/10 |
| + useGenerationalGC=false | 4/6 |
| + Heap::gatherStackRoots also scans jsc_m68k_vrf | 9/10 |
| + CLoop stack pointer check at every GC | never behind (fewer than 20 GCs per worker run) |
| + verifyGC=true verboseVerifyGC=true (log to file) | 3/3; verifier silent |
| + single-slot transitions marked strongly in every collection (Structure::visitChildren) | 6/6 |
| + sweepSynchronously=true | 4/4 |
| + scribbleFreeCells=true | 0/4, all returned (painting about 2x slower) |

Reading:
- Not the m68k template JIT, not write barriers (it happens with every collection full), not the single-slot transition weak link, not incremental-sweep timing.
- Marking agrees with the verifier, so the cell is not wrongly unmarked given the roots.
- Only scribbleFreeCells hides it. That fits a use-after-free where an old Structure still referenced somewhere is freed, then reused in place. Scribbling breaks the stale Structure's fields, so a lookup no longer matches it.
- The self-looped cell sits at fixed offsets in its 16 KB block: 0x34c0, 0x5440, 0x3420, 0x1440.

Next steps:
- Watch that cell index: log allocation and sweep of the Structure-space cell at offset 0x34c0, with GC version, plus who still points at the old Structure. Candidates: TransitionMap (WeakGCMap) entries, StructureRareData caches, inline caches in the LLInt metadata (put_by_id caches oldStructureID/newStructureID).
- The LLInt put_by_id metadata cache is a strong candidate. On 32-bit, StructureIDs are raw pointers, and the m68k interpreter's put_by_id fast path (0015 and later) may take a cached new structure without the GC knowing about it, if the metadata cache isn't visited or reconciled. Upstream clears dead StructureIDs in metadata at GC end (CodeBlock reconcile/finalize with isMarked). Check the m68k LLInt put_by_id transition fast path and CodeBlock::finalizeLLIntInlineCaches for 32-bit.

Test aid: OBJSC / OBJSCLOG (draft PR #124) sets JSC options; Shell JSC_ variables never reached JSC.
