/*
 * GFOpenBrowser (DalsinAI/openamigabrowser)
 * Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
 *
 * The 68k template JIT's stitcher core (J2, stage T2 prototype). Plain C so
 * the host test (test/qemu_stitch.c) and JavaScriptCore share it.
 *
 * A template (tools/jsjit/m68k_templates.py) is one bytecode handler's code
 * plus a table of holes. Stitching one bytecode instruction copies its
 * template to the end of the code buffer and fills each hole's 32-bit
 * immediate: an operand read from the instruction's bytes, or the PC of the
 * instruction after it. Copies placed one after another fall through into
 * each other while PC moves on by one instruction.
 */
#ifndef JSC_M68K_STITCH_H
#define JSC_M68K_STITCH_H

#include <stdint.h>
#include <string.h>

enum {
    M68K_TPL_HOLE_U8 = 1,
    M68K_TPL_HOLE_S8 = 2,
    M68K_TPL_HOLE_U16 = 3,
    M68K_TPL_HOLE_S16 = 4,
    M68K_TPL_HOLE_32 = 5,
    M68K_TPL_HOLE_NEXT_PC = 6,
    /* The high word (0 or -1) of a signed operand of the given size. */
    M68K_TPL_HOLE_HI_S8 = 7,
    M68K_TPL_HOLE_HI_S16 = 8,
    M68K_TPL_HOLE_HI_32 = 9,
};

/* Laid out as m68k_templates.py writes it (.long, .long, .long, then
 * .word triples), big-endian like everything on the 68k. */
struct m68k_tpl_hole {
    uint16_t offset;       /* of the immediate, from the template's start */
    uint16_t bytecode;     /* operand's byte offset in the instruction */
    uint16_t kind;
};

struct m68k_tpl {
    const uint8_t* start;
    uint32_t length;
    uint32_t holes;
    struct m68k_tpl_hole hole[];
};

static inline uint32_t m68k_tpl_operand(const uint8_t* instruction, const struct m68k_tpl_hole* h)
{
    const uint8_t* p = instruction + h->bytecode;
    switch (h->kind) {
    case M68K_TPL_HOLE_U8: return p[0];
    case M68K_TPL_HOLE_S8: return (uint32_t)(int32_t)(int8_t)p[0];
    case M68K_TPL_HOLE_U16: return (uint32_t)((p[0] << 8) | p[1]);
    case M68K_TPL_HOLE_S16: return (uint32_t)(int32_t)(int16_t)((p[0] << 8) | p[1]);
    case M68K_TPL_HOLE_32: return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    case M68K_TPL_HOLE_HI_S8:
    case M68K_TPL_HOLE_HI_S16:
    case M68K_TPL_HOLE_HI_32: return (p[0] & 0x80) ? 0xffffffffu : 0;
    }
    return 0;
}

/* Copies tpl for the instruction at bytecode offset pc (length bytes long,
 * its bytes at instruction) to out and fills the holes. Returns the bytes
 * written. The caller flushes the caches over the whole buffer once
 * (CacheClearE on AmigaOS) before running it. */
static inline uint32_t m68k_tpl_stitch(uint8_t* out, const struct m68k_tpl* tpl,
    const uint8_t* instruction, uint32_t pc, uint32_t length)
{
    memcpy(out, tpl->start, tpl->length);
    for (uint32_t i = 0; i < tpl->holes; i++) {
        const struct m68k_tpl_hole* h = &tpl->hole[i];
        uint32_t value = h->kind == M68K_TPL_HOLE_NEXT_PC ? pc + length : m68k_tpl_operand(instruction, h);
        uint8_t* imm = out + h->offset;
        imm[0] = (uint8_t)(value >> 24);
        imm[1] = (uint8_t)(value >> 16);
        imm[2] = (uint8_t)(value >> 8);
        imm[3] = (uint8_t)value;
    }
    return tpl->length;
}

#endif
