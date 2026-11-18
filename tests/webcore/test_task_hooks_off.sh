#!/bin/sh
# WebKit patch 0122: the task hooks patch 0033 puts on each task that runs
# JavaScript come off the thread that ends the program. Left on, exec went
# on calling them in freed memory on the Shell's process, and the next
# program loaded there hung the machine (17 November 2026).
#
# This takes jsc_m68k_detach_thread() out of the patch as it is and runs
# it on this machine against stand-ins for exec's tasks.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P="$ROOT/webkit/patches/0122-jsc-m68k-thread-hooks-off-at-exit.patch"
OUT=${OUT:-${TMPDIR:-/tmp}/ob-task-hooks-off.$$}
CXX=${CXX:-c++}
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

# The function, from its first line to its closing brace.
awk '/^\+extern "C" void jsc_m68k_detach_thread\(\)/ { p = 1 }
     p { print substr($0, 2) }
     p && /^\+\}/ { exit }' "$P" > "$OUT/detach.inc"
ok '[ -s "$OUT/detach.inc" ]' "the patch has jsc_m68k_detach_thread()"

# A destructor calls it, after every other destructor (priority 101).
body=$(awk '/^\+__attribute__\(\(destructor\(101\)\)\) static void detachM68KThreadAtExit\(\)/ { p = 1; next }
            p && /^\+\}/ { exit } p' "$P" | tr -d ' \n')
ok '[ "$body" = "+{+jsc_m68k_detach_thread();" ]' "the last destructor calls it (body: $body)"

# What the task had before goes into the slot as the hooks go on.
ok 'grep -q "^+    thread->switchBefore = task->tc_Switch;" "$P"' "the switch hook from before is kept"
ok 'grep -q "^+    thread->launchBefore = task->tc_Launch;" "$P"' "the launch hook from before is kept"
ok 'grep -q "^+    thread->flagsBefore = task->tc_Flags & (TF_SWITCH | TF_LAUNCH);" "$P"' "the two flags from before are kept"

# Each of the patch's hunks counts its lines.
bad=$(awk '/^@@ / { if (c && k != n) print "x"; split($3, a, ","); n = a[2]; k = 0; c = 1; next }
           c && /^-- $/ { if (k != n) print "x"; c = 0 }
           c && /^[+ ]/ { k++ }' "$P")
ok '[ -z "$bad" ]' "each hunk counts its lines"

# abort() in the test viewer and in the engine ends with _exit(), which runs
# no destructors: each takes the hooks off itself first.
for f in src/webcore/obcore_view.c src/webbrowser/wb_engine.c; do
    last=$(awk '/^void abort\(void\)/ { p = 1 } p && /jsc_m68k_detach_thread\(\);/ { d = NR } p && /_exit\(20\);/ { print (d == NR - 1) ? "ok" : "no"; exit }' "$ROOT/$f")
    ok '[ "$last" = ok ]' "$f: abort() takes the hooks off just before _exit()"
done

cat > "$OUT/t.cpp" <<'EOF'
#include <cstdio>
#define TF_SWITCH (1 << 6)
#define TF_LAUNCH (1 << 7)
struct Task {
    unsigned char tc_Flags;
    void (*tc_Switch)();
    void (*tc_Launch)();
};
static struct { struct Task* ThisTask; } execBase, *SysBase = &execBase;
static int depth, outside;
static void Disable() { depth++; }
static void Enable() { depth--; }
extern "C" void jsc_m68k_task_switch() { }
extern "C" void jsc_m68k_task_launch() { }
static void othersHook() { }
struct M68KThreadRegisters {
    struct Task* task;
    unsigned long d[2];
    void (*switchBefore)();
    void (*launchBefore)();
    unsigned char flagsBefore;
};
static M68KThreadRegisters m68kThreads[32];
static M68KThreadRegisters* m68kThreadFor(struct Task* task)
{
    if (!depth)
        outside++;              /* the slots are only looked at in Disable() */
    for (auto& thread : m68kThreads) {
        if (thread.task == task)
            return &thread;
    }
    return nullptr;
}
#include "detach.inc"

static int failed;
#define CHECK(c, what) do { if (!(c)) { failed++; std::printf("FAIL: %s\n", what); } } while (0)
#define OURS(t) ((t).tc_Switch == jsc_m68k_task_switch && (t).tc_Launch == jsc_m68k_task_launch \
    && ((t).tc_Flags & (TF_SWITCH | TF_LAUNCH)) == (TF_SWITCH | TF_LAUNCH))

int main()
{
    /* the Shell's process, which lives on; one other flag set */
    Task shell = { TF_SWITCH | TF_LAUNCH | 1, jsc_m68k_task_switch, jsc_m68k_task_launch };
    /* one of the program's other threads, perhaps still in the interpreter */
    Task worker = { TF_SWITCH | TF_LAUNCH, jsc_m68k_task_switch, jsc_m68k_task_launch };
    m68kThreads[0].task = &shell;
    m68kThreads[3].task = &worker;

    SysBase->ThisTask = &shell;
    jsc_m68k_detach_thread();
    CHECK(!shell.tc_Switch && !shell.tc_Launch, "our hooks come off the thread that ends the program");
    CHECK(shell.tc_Flags == 1, "TF_SWITCH and TF_LAUNCH are cleared, other flags kept");
    CHECK(!m68kThreads[0].task, "its slot is free again");
    CHECK(OURS(worker) && m68kThreads[3].task == &worker, "another thread keeps its hooks and its slot");
    CHECK(!depth && !outside, "all of it inside one Disable() and Enable()");

    /* a second call, and a call on a thread that never ran scripts, do nothing */
    shell.tc_Launch = othersHook;
    jsc_m68k_detach_thread();
    CHECK(shell.tc_Launch == othersHook, "a thread without a slot is left alone");

    /* a task that had a launch hook of its own before ours went on gets it back */
    Task hooked = { TF_SWITCH | TF_LAUNCH | 2, jsc_m68k_task_switch, jsc_m68k_task_launch };
    m68kThreads[7].task = &hooked;
    m68kThreads[7].launchBefore = othersHook;
    m68kThreads[7].flagsBefore = TF_LAUNCH;
    SysBase->ThisTask = &hooked;
    jsc_m68k_detach_thread();
    CHECK(!hooked.tc_Switch && hooked.tc_Launch == othersHook && hooked.tc_Flags == (TF_LAUNCH | 2),
        "what a task had before our hooks is put back");

    /* hooks another program put on since are not ours to take off */
    Task other = { TF_SWITCH | TF_LAUNCH, othersHook, othersHook };
    m68kThreads[9].task = &other;
    SysBase->ThisTask = &other;
    jsc_m68k_detach_thread();
    CHECK(other.tc_Switch == othersHook && other.tc_Launch == othersHook
        && other.tc_Flags == (TF_SWITCH | TF_LAUNCH), "another's hooks are left alone");
    CHECK(!m68kThreads[9].task, "and its slot is free again");
    return failed ? 1 : 0;
}
EOF
ok '"$CXX" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -I"$OUT" "$OUT/t.cpp" -o "$OUT/t"' "the function builds on this machine"
ok '"$OUT/t"' "the function's cases"

echo "task hooks off: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
