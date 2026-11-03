/*
 * src/webcore/ob_atomic.c on a 68040 under qemu-m68k: four threads at once
 * on every size (1, 2, 4, 8 bytes) and operation, through CAS and CAS2, and
 * through the Forbid() path (here a mutex) with the Chip RAM limit raised so
 * every address takes it.
 *   sh tests/atomic/run.sh
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
uint8_t __atomic_fetch_add_1(volatile uint8_t *, uint8_t, int);
uint16_t __atomic_fetch_add_2(volatile uint16_t *, uint16_t, int);
uint32_t __atomic_fetch_add_4(volatile uint32_t *, uint32_t, int);
uint64_t __atomic_fetch_add_8(volatile uint64_t *, uint64_t, int);
uint32_t __atomic_fetch_sub_4(volatile uint32_t *, uint32_t, int);
uint32_t __atomic_add_fetch_4(volatile uint32_t *, uint32_t, int);
uint64_t __atomic_fetch_or_8(volatile uint64_t *, uint64_t, int);
uint8_t __atomic_fetch_xor_1(volatile uint8_t *, uint8_t, int);
uint32_t __atomic_exchange_4(volatile uint32_t *, uint32_t, int);
int __atomic_compare_exchange_4(volatile uint32_t *, uint32_t *, uint32_t, int, int);
int __atomic_compare_exchange_8(volatile uint64_t *, uint64_t *, uint64_t, int, int);
uint64_t __atomic_load_8(volatile uint64_t *, int);
void __atomic_store_8(volatile uint64_t *, uint64_t, int);

#define THREADS 4
#define ROUNDS 20000
static volatile uint8_t c1;
static volatile uint16_t c2;
static volatile uint32_t c4, s4 = 1000000, lockword, inside, bad;
static volatile uint64_t c8 = 0xfffffff0ULL, bits8;
static volatile uint8_t x1;
static volatile uint64_t torn = 0;   /* written 0 or all ones, never half */
static int failed;
pthread_mutex_t ob_atomic_test_lock = PTHREAD_MUTEX_INITIALIZER;

static void *work(void *arg)
{
    int id = (int)(intptr_t)arg, i;
    for (i = 0; i < ROUNDS; i++) {
        uint32_t zero = 0;
        __atomic_fetch_add_1(&c1, 1, 5);
        __atomic_fetch_add_2(&c2, 1, 5);
        __atomic_fetch_add_4(&c4, 1, 5);
        __atomic_fetch_add_8(&c8, 1, 5);
        __atomic_fetch_sub_4(&s4, 1, 5);
        __atomic_fetch_or_8(&bits8, 1ULL << (id * 16 + (i % 16)), 5);
        __atomic_fetch_xor_1(&x1, 0xff, 5);
        /* a spin lock on compare_exchange and exchange: one inside at once */
        while (!__atomic_compare_exchange_4(&lockword, &zero, 1, 5, 5))
            zero = 0;
        if (__atomic_add_fetch_4(&inside, 1, 5) != 1)
            bad++;
        __atomic_fetch_sub_4(&inside, 1, 5);
        __atomic_exchange_4(&lockword, 0, 5);
        __atomic_store_8(&torn, (i & 1) ? ~0ULL : 0, 5);
        {
            uint64_t v = __atomic_load_8(&torn, 5);
            if (v != 0 && v != ~0ULL)
                bad++;
        }
    }
    return 0;
}

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

int main(void)
{
    pthread_t t[THREADS];
    uint64_t e, n = THREADS * (uint64_t)ROUNDS;
    int i;
    for (i = 0; i < THREADS; i++)
        pthread_create(&t[i], 0, work, (void *)(intptr_t)i);
    for (i = 0; i < THREADS; i++)
        pthread_join(t[i], 0);
    CHECK(c1 == (uint8_t)n);
    CHECK(c2 == (uint16_t)n);
    CHECK(c4 == (uint32_t)n);
    CHECK(c8 == 0xfffffff0ULL + n);                 /* across the 32-bit carry */
    CHECK(s4 == 1000000 - (uint32_t)n);
    CHECK(bits8 == ~0ULL);
    CHECK(x1 == 0);                                 /* an even number of flips */
    CHECK(!bad && !inside && !lockword);
    e = 5;
    CHECK(!__atomic_compare_exchange_8(&c8, &e, 7, 5, 5) && e == 0xfffffff0ULL + n);
    CHECK(__atomic_compare_exchange_8(&c8, &e, 7, 5, 5) && c8 == 7);
    printf("%s (%llu operations a size)\n", failed ? "test_ob_atomic FAILED" : "test_ob_atomic ok", (unsigned long long)n);
    return failed != 0;
}
