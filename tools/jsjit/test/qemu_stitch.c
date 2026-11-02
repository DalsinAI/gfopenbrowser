/*
 * GFOpenBrowser (DalsinAI/gfopenbrowser)
 * Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
 *
 * Host test for the 68k template JIT's stitcher, run under qemu-m68k
 * (-cpu m68040) as an m68k Linux program. It stitches real op_mov templates
 * (generated from the 68k interpreter) for a short bytecode, runs them on a
 * fake call frame and checks the registers they moved, that copies fall
 * through into each other, and that a PC that does not match leaves
 * through jsc_m68k_tjit_redispatch. See run_qemu_test.sh.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../stitch.h"

uint32_t vrf[64] __asm__("_jsc_m68k_vrf");
extern const struct m68k_tpl tplMov __asm__("_jsc_m68k_tpl_op_mov");
uint32_t exitKind __asm__("tjit_exit_kind");
extern char runCode[] __asm__("tjit_run");
extern char exitStub[] __asm__("tjit_exit");

/* run(code, frame, pb): sets the interpreter's registers (cfr a5, PB a3,
 * PC a2 = 0) and jumps to code. Returns PC. */
__asm__(
    ".text\n"
    ".globl tjit_run\n"
    "tjit_run:\n"
    "    movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "    move.l 48(%sp),%a0\n"
    "    move.l 52(%sp),%a5\n"
    "    move.l 56(%sp),%a3\n"
    "    sub.l %a2,%a2\n"
    "    jmp (%a0)\n"
    ".globl _jsc_m68k_tjit_redispatch\n"
    "_jsc_m68k_tjit_redispatch:\n"
    "    move.l #1,tjit_exit_kind\n"
    "    bra.s 1f\n"
    ".globl tjit_exit\n"
    "tjit_exit:\n"
    "    move.l #2,tjit_exit_kind\n"
    "1:  move.l %a2,%d0\n"
    "    movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "    rts\n");

typedef uint32_t (*RunFn)(void* code, void* frame, const uint8_t* pb);

static uint32_t frame[2 * 16] __attribute__((aligned(8)));

static void setReg(int r, uint32_t hi, uint32_t lo) { frame[2 * r] = hi; frame[2 * r + 1] = lo; }

static int check(const char* what, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok ? 0 : 1;
}

int main(void)
{
    int failures = 0;
    uint8_t* code = mmap(0, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("op_mov template: %u bytes, %u holes\n", tplMov.length, tplMov.holes);
    for (uint32_t i = 0; i < tplMov.holes; i++)
        printf("  hole at +%u: bytecode +%u kind %u\n", tplMov.hole[i].offset, tplMov.hole[i].bytecode, tplMov.hole[i].kind);

    /* mov r5 <- r3; mov r6 <- r5; mov r7 <- r6 (narrow: opcode, dst, src). */
    const uint8_t bytecode[] = { 0, 5, 3, 0, 6, 5, 0, 7, 6 };
    setReg(3, 0xfffe0000, 1234);
    uint32_t n = 0, pc = 0;
    for (int i = 0; i < 3; i++, pc += 3)
        n += m68k_tpl_stitch(code + n, &tplMov, bytecode + pc, pc, 3);
    code[n++] = 0x4e; code[n++] = 0xf9; /* jmp (tjit_exit).l */
    uint32_t target = (uint32_t)(uintptr_t)exitStub;
    code[n++] = target >> 24; code[n++] = target >> 16; code[n++] = target >> 8; code[n++] = target;

    uint32_t endPc = ((RunFn)(void*)runCode)(code, frame, bytecode);
    failures += check("three movs fall through to the end", exitKind == 2 && endPc == 9);
    failures += check("r7 holds r3's value", frame[14] == 0xfffe0000 && frame[15] == 1234);
    failures += check("r5 and r6 hold it too", frame[11] == 1234 && frame[13] == 1234);

    /* A copy whose next PC does not match leaves through redispatch. */
    setReg(3, 0, 42);
    n = m68k_tpl_stitch(code, &tplMov, bytecode, 0, 4 /* wrong length */);
    endPc = ((RunFn)(void*)runCode)(code, frame, bytecode);
    failures += check("a PC mismatch leaves through redispatch", exitKind == 1 && endPc == 3);
    failures += check("the mov still happened", frame[11] == 42 && frame[10] == 0);

    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures != 0;
}
