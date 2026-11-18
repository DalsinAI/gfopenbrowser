#!/bin/sh
# WebKit patch 0122: the task hooks patch 0033 puts on each task that runs
# JavaScript come off again at exit. Left on, exec went on calling them in
# freed memory on the Shell's process, and the next program loaded there
# hung the machine (17 November 2026).
#
# This takes jsc_m68k_detach_threads() out of the patch as it is and runs
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
awk '/^\+extern "C" void jsc_m68k_detach_threads\(\)/ { p = 1 }
     p { print substr($0, 2) }
     p && /^\+\}/ { exit }' "$P" > "$OUT/detach.inc"
ok '[ -s "$OUT/detach.inc" ]' "the patch has jsc_m68k_detach_threads()"

# A destructor calls it when the program ends.
body=$(awk '/^\+__attribute__\(\(destructor\)\) static void detachM68KThreadsAtExit\(\)/ { p = 1; next }
            p && /^\+\}/ { exit } p' "$P" | tr -d ' \n')
ok '[ "$body" = "+{+jsc_m68k_detach_threads();" ]' "a destructor calls it (body: $body)"

# What the task had before goes into the slot as the hooks go on.
ok 'grep -q "^+    thread->switchBefore = task->tc_Switch;" "$P"' "the switch hook from before is kept"
ok 'grep -q "^+    thread->launchBefore = task->tc_Launch;" "$P"' "the launch hook from before is kept"
ok 'grep -q "^+    thread->flagsBefore = task->tc_Flags & (TF_SWITCH | TF_LAUNCH);" "$P"' "the two flags from before are kept"

# Each of the patch's hunks counts its lines.
bad=$(awk '/^@@ / { if (c && k != n) print "x"; split($3, a, ","); n = a[2]; k = 0; c = 1; next }
           c && /^-- $/ { if (k != n) print "x"; c = 0 }
           c && /^[+ ]/ { k++ }' "$P")
ok '[ -z "$bad" ]' "each hunk counts its lines"

cat > "$OUT/t.cpp" <<'EOF'
#include <cstdio>
#define TF_SWITCH (1 << 6)
#define TF_LAUNCH (1 << 7)
struct Task {
    unsigned char tc_Flags;
    void (*tc_Switch)();
    void (*tc_Launch)();
    bool alive;
};
static int depth, writesOutside;
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
static bool m68kTaskExists(struct Task* task)
{
    if (!depth)
        writesOutside++;        /* the task list is only read in Disable() */
    return task->alive;
}
#include "detach.inc"

static int failed;
#define CHECK(c, what) do { if (!(c)) { failed++; std::printf("FAIL: %s\n", what); } } while (0)

int main()
{
    /* ours: the Shell's process, which lives on; one other flag set */
    Task shell = { TF_SWITCH | TF_LAUNCH | 1, jsc_m68k_task_switch, jsc_m68k_task_launch, true };
    /* a task that has since been given someone else's hooks */
    Task other = { TF_SWITCH | TF_LAUNCH, othersHook, othersHook, true };
    /* a task that ended: its memory is not ours to write */
    Task gone = { TF_SWITCH | TF_LAUNCH, jsc_m68k_task_switch, jsc_m68k_task_launch, false };
    /* a task that had a launch hook of its own before ours went on */
    Task hooked = { TF_SWITCH | TF_LAUNCH | 2, jsc_m68k_task_switch, jsc_m68k_task_launch, true };
    m68kThreads[0].task = &shell;
    m68kThreads[7].task = &hooked;
    m68kThreads[7].launchBefore = othersHook;
    m68kThreads[7].flagsBefore = TF_LAUNCH;
    m68kThreads[3].task = &other;
    m68kThreads[31].task = &gone;

    jsc_m68k_detach_threads();

    CHECK(!shell.tc_Switch && !shell.tc_Launch, "our hooks come off the task that lives on");
    CHECK(shell.tc_Flags == 1, "TF_SWITCH and TF_LAUNCH are cleared, other flags kept");
    CHECK(!hooked.tc_Switch && hooked.tc_Launch == othersHook && hooked.tc_Flags == (TF_LAUNCH | 2),
        "what a task had before our hooks is put back");
    CHECK(other.tc_Switch == othersHook && other.tc_Launch == othersHook
        && other.tc_Flags == (TF_SWITCH | TF_LAUNCH), "another's hooks are left alone");
    CHECK(gone.tc_Switch == jsc_m68k_task_switch && gone.tc_Launch == jsc_m68k_task_launch
        && gone.tc_Flags == (TF_SWITCH | TF_LAUNCH), "a task that ended is not written to");
    for (auto& thread : m68kThreads)
        CHECK(!thread.task, "every task is forgotten");
    CHECK(!depth, "Disable() and Enable() pair up");
    CHECK(!writesOutside, "tasks are looked at inside Disable() only");

    /* a second call (the program's own, then the destructor's) does nothing */
    shell.tc_Launch = jsc_m68k_task_launch;
    jsc_m68k_detach_threads();
    CHECK(shell.tc_Launch == jsc_m68k_task_launch, "a second call touches nothing");
    return failed ? 1 : 0;
}
EOF
ok '"$CXX" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -I"$OUT" "$OUT/t.cpp" -o "$OUT/t"' "the function builds on this machine"
ok '"$OUT/t"' "the function's cases"

echo "task hooks off: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
