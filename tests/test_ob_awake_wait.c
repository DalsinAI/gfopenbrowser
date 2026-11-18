/* Host test for the engine's short look for a service answer before it
 * sleeps for one (src/webcore/ob_awake_wait.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include "ob_awake_wait.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct state {
    unsigned long now;      /* the clock: one unit on at every reading */
    unsigned long readyAt;  /* the answer is there from this time */
    int never;              /* or it never comes */
    int looks;
};

static int done(void *user)
{
    struct state *s = user;
    s->looks++;
    return !s->never && s->now >= s->readyAt;
}

static unsigned long clockNow(void *user)
{
    struct state *s = user;
    return s->now++;
}

int main(void)
{
    struct state s = { 0, 0, 0, 0 };

    /* there already: one look, no waiting */
    s.now = 100; s.readyAt = 0; s.looks = 0;
    CHECK(ob_awake_wait(done, clockNow, &s, 30) == 1 && s.looks == 1);

    /* comes inside the limit: found awake */
    s.now = 100; s.readyAt = 110; s.looks = 0;
    CHECK(ob_awake_wait(done, clockNow, &s, 30) == 1);
    CHECK(s.now >= 110 && s.now <= 131 && s.looks > 1);

    /* never comes: gives up at the limit, so the caller sleeps */
    s.now = 100; s.never = 1; s.looks = 0;
    CHECK(ob_awake_wait(done, clockNow, &s, 30) == 0);
    CHECK(s.now >= 130 && s.now <= 132);

    /* a limit of 0: one look only */
    s.now = 100; s.never = 1; s.looks = 0;
    CHECK(ob_awake_wait(done, clockNow, &s, 0) == 0 && s.looks == 1);

    /* the clock wraps during the wait: still the limit, not for ever and not at once */
    s.now = ~0UL - 5; s.never = 1; s.looks = 0;
    CHECK(ob_awake_wait(done, clockNow, &s, 30) == 0);
    CHECK(s.looks >= 14 && s.looks <= 31);

    printf("test_ob_awake_wait: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
