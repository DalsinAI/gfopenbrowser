/* Staying awake for an answer that is about to come, in plain C so the host
 * tests read it (tests/test_ob_awake_wait.c).
 *
 * A service call's answer takes a few milliseconds. A caller that sleeps for
 * it (DoIO) lets the whole Amiga go idle, and on a Cradle an idle Amiga was
 * handed the answer 40 ms to 1.3 s late: 60 small pictures took 20.7 s
 * asleep, and 40 took 0.6 s when the caller kept looking (17 November 2026,
 * media.decode/1). So the caller looks for the answer for a short time first
 * and only then sleeps for it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OB_AWAKE_WAIT_H
#define OB_AWAKE_WAIT_H

/* done: whether the answer is there. clock: the time now, in any unit that
 * only goes up (it may wrap). Looks until the answer is there (1) or limit
 * units have passed (0: the caller sleeps for it). The answer is looked for
 * at least once, and a limit of 0 looks once only. */
static inline int ob_awake_wait(int (*done)(void *), unsigned long (*clock)(void *), void *user, unsigned long limit)
{
    unsigned long start = clock(user);
    for (;;) {
        if (done(user))
            return 1;
        if (clock(user) - start >= limit)
            return 0;
    }
}

#endif
