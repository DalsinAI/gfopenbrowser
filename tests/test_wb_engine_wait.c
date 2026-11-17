/* Host test for webbrowser.library's wait for its engine (src/webbrowser/wb_wait.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include "wb_wait.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    int seen = 0, s;

    /* a cold start: the process is there for a while, then the port */
    CHECK(wb_engine_wait(0, 1, &seen, 0, 180) == WB_WAIT_ON && seen == 1);
    CHECK(wb_engine_wait(0, 1, &seen, 60, 180) == WB_WAIT_ON);
    CHECK(wb_engine_wait(1, 1, &seen, 61, 180) == WB_WAIT_READY);

    /* refused: the process is there while the question is up (60 seconds), then gone with no port */
    seen = 0;
    for (s = 0; s < 60; s++)
        CHECK(wb_engine_wait(0, 1, &seen, s, 180) == WB_WAIT_ON);
    CHECK(wb_engine_wait(0, 0, &seen, 60, 180) == WB_WAIT_ENDED);

    /* the port wins, whatever else: the engine left its Shell's process, or was there already */
    seen = 1;
    CHECK(wb_engine_wait(1, 0, &seen, 5, 180) == WB_WAIT_READY);
    seen = 0;
    CHECK(wb_engine_wait(1, 0, &seen, 0, 180) == WB_WAIT_READY);

    /* a process never seen is never taken for one that ended: only the time ends that wait */
    seen = 0;
    CHECK(wb_engine_wait(0, 0, &seen, 0, 180) == WB_WAIT_ON && seen == 0);
    CHECK(wb_engine_wait(0, 0, &seen, 179, 180) == WB_WAIT_ON);
    CHECK(wb_engine_wait(0, 0, &seen, 180, 180) == WB_WAIT_LATE);

    /* still there when the time is over */
    seen = 0;
    CHECK(wb_engine_wait(0, 1, &seen, 180, 180) == WB_WAIT_LATE);

    printf("test_wb_engine_wait: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
