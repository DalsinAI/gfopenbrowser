/*
 * GFOpenBrowser: atomic operations with the 68040's CAS and CAS2 instead of
 * libatomic's.
 *
 * GCC for m68k-amigaos turns every C11/C++ atomic (std::atomic: reference
 * counts, locks, JavaScriptCore's and WTF's flags) into a call to libatomic,
 * and the stove's libatomic makes each one atomic with exec's Forbid() and
 * Permit(): two library calls and two ROM calls an operation, 3% of a bbc
 * load's time and 5% of reddit's (js-profile, 3 Nov 2026). These are the
 * same functions, linked into each program and the JavaScriptEngine image so
 * they are used instead: CAS.B, CAS.W and CAS.L for 1, 2 and 4 bytes, CAS2.L
 * for 8 (two longs, which the 68040 locks together; the 68060 would trap
 * it, and this build is for the 68040).
 *
 * The Amiga's chipset does not take CAS's read-modify-write cycle in Chip
 * RAM, so an address below the end of Chip RAM (OB_ATOMIC_CHIP_END: 8 MB, the
 * most an Amiga or AmiCradle board has) takes the old way, under Forbid().
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdint.h>

/* GCC declares these names with its builtins' types (a weak flag the
 * library call doesn't pass, void pointers): the library's are the ones
 * defined here, which is what GCC calls. */
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

#ifndef OB_ATOMIC_TEST
#include <proto/exec.h>
#define LOCK() Forbid()
#define UNLOCK() Permit()
#endif

#ifndef OB_ATOMIC_CHIP_END
#define OB_ATOMIC_CHIP_END 0x00800000UL
#endif

#define IN_CHIP(p) ((uintptr_t)(p) < OB_ATOMIC_CHIP_END)

/* CAS: if *p holds *expected, desired goes there; else *expected gets *p */
#define DEFINE_CAS(N, T, SFX)                                                                                  \
    static inline int cas##N(volatile T *p, T *expected, T desired)                                           \
    {                                                                                                         \
        T old = *expected;                                                                                    \
        if (IN_CHIP(p)) {                                                                                     \
            int ok;                                                                                           \
            LOCK();                                                                                           \
            old = *p;                                                                                         \
            ok = old == *expected;                                                                            \
            if (ok)                                                                                           \
                *p = desired;                                                                                 \
            UNLOCK();                                                                                         \
            *expected = old;                                                                                  \
            return ok;                                                                                        \
        }                                                                                                     \
        __asm__ volatile("cas." SFX " %0,%2,%1" : "+d"(old), "+m"(*p) : "d"(desired) : "cc", "memory");       \
        if (old == *expected)                                                                                 \
            return 1;                                                                                         \
        *expected = old;                                                                                      \
        return 0;                                                                                             \
    }

DEFINE_CAS(1, uint8_t, "b")
DEFINE_CAS(2, uint16_t, "w")
DEFINE_CAS(4, uint32_t, "l")

static inline int cas8(volatile uint64_t *p, uint64_t *expected, uint64_t desired)
{
    volatile uint32_t *hi = (volatile uint32_t *)p, *lo = hi + 1;
    uint32_t eh = (uint32_t)(*expected >> 32), el = (uint32_t)*expected;
    uint32_t dh = (uint32_t)(desired >> 32), dl = (uint32_t)desired;
    uint64_t old;
    if (IN_CHIP(p)) {
        int ok;
        LOCK();
        old = *p;
        ok = old == *expected;
        if (ok)
            *p = desired;
        UNLOCK();
        *expected = old;
        return ok;
    }
    __asm__ volatile("cas2.l %0:%1,%2:%3,(%4):(%5)" : "+d"(eh), "+d"(el) : "d"(dh), "d"(dl), "a"(hi), "a"(lo) : "cc", "memory");
    old = ((uint64_t)eh << 32) | el;
    if (old == *expected)
        return 1;
    *expected = old;
    return 0;
}

/* a 64-bit read in one piece: a CAS2 that writes what it found back */
static inline uint64_t load8(volatile uint64_t *p)
{
    uint64_t v = 0;
    cas8(p, &v, 0);   /* *p is 0: 0 written, the same; else v gets *p */
    return v;
}

/* the fetch-and-op and op-and-fetch functions, as CAS loops */
#define DEFINE_OPS(N, T)                                                                                      \
    int __atomic_compare_exchange_##N(volatile T *p, T *expected, T desired, int s, int f)                   \
    {                                                                                                         \
        (void)s;                                                                                              \
        (void)f;                                                                                              \
        return cas##N(p, expected, desired);                                                                  \
    }                                                                                                         \
    T __atomic_exchange_##N(volatile T *p, T v, int m)                                                        \
    {                                                                                                         \
        T old = N == 8 ? (T)load8((volatile uint64_t *)p) : *p;                                               \
        (void)m;                                                                                              \
        while (!cas##N(p, &old, v)) {                                                                         \
        }                                                                                                     \
        return old;                                                                                           \
    }                                                                                                         \
    DEFINE_RMW(N, T, add, old + v)                                                                            \
    DEFINE_RMW(N, T, sub, old - v)                                                                            \
    DEFINE_RMW(N, T, and, old & v)                                                                            \
    DEFINE_RMW(N, T, or, old | v)                                                                             \
    DEFINE_RMW(N, T, xor, old ^ v)                                                                            \
    DEFINE_RMW(N, T, nand, ~(old & v))

#define DEFINE_RMW(N, T, OP, EXPR)                                                                            \
    T __atomic_fetch_##OP##_##N(volatile T *p, T v, int m)                                                    \
    {                                                                                                         \
        T old = N == 8 ? (T)load8((volatile uint64_t *)p) : *p;                                               \
        (void)m;                                                                                              \
        while (!cas##N(p, &old, (T)(EXPR))) {                                                                 \
        }                                                                                                     \
        return old;                                                                                           \
    }                                                                                                         \
    T __atomic_##OP##_fetch_##N(volatile T *p, T v, int m)                                                    \
    {                                                                                                         \
        T old = N == 8 ? (T)load8((volatile uint64_t *)p) : *p;                                               \
        (void)m;                                                                                              \
        while (!cas##N(p, &old, (T)(EXPR))) {                                                                 \
        }                                                                                                     \
        return (T)(EXPR);                                                                                     \
    }

DEFINE_OPS(1, uint8_t)
DEFINE_OPS(2, uint16_t)
DEFINE_OPS(4, uint32_t)
DEFINE_OPS(8, uint64_t)

/* 8-byte loads and stores: two moves are not one, so through CAS2 */
uint64_t __atomic_load_8(volatile uint64_t *p, int m)
{
    (void)m;
    return load8(p);
}

void __atomic_store_8(volatile uint64_t *p, uint64_t v, int m)
{
    uint64_t old = load8(p);
    (void)m;
    while (!cas8(p, &old, v)) {
    }
}
