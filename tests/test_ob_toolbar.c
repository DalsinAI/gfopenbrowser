/*
 * The toolbar row's arithmetic (src/webcore/ob_toolbar.h): where the
 * address field goes beside the group of icon buttons, and the room the
 * group may take. The buttons themselves are GFOpenGadTools' toolbar,
 * tested there; how they look on a screen is a lab's to show.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>

#include "ob_toolbar.h"

static int failures;

#define CHECK(what) \
    do { \
        if (!(what)) { \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
            failures++; \
        } \
    } while (0)

int main(void)
{
    obt_row row;

    /* five buttons: Back, Forward, Reload, Stop and Find, in the order the
     * text buttons had */
    CHECK(OBT_BACK == 0 && OBT_FORWARD == 1 && OBT_RELOAD == 2 && OBT_STOP == 3 && OBT_FIND == 4);
    CHECK(OBT_COUNT == 5);

    /* a group taller than the field (topaz 8 beside 24 pixel icons): the
     * row is the group's height, the field is centred on it and takes the
     * rest of the window */
    obt_place(4, 20, 636, 150, 30, 8, &row);
    CHECK(row.height == 30);
    CHECK(row.barTop == 20);
    CHECK(row.fieldHeight == 14);
    CHECK(row.fieldTop == 28);
    CHECK(row.fieldLeft == 4 + 150 + OBT_FIELD_GAP);
    CHECK(row.fieldLeft + row.fieldWidth == 636 - OBT_FIELD_GAP);

    /* a field taller than the group (a 40 pixel font beside names only):
     * the group is the one centred */
    obt_place(4, 20, 1276, 200, 30, 40, &row);
    CHECK(row.height == 46);
    CHECK(row.fieldTop == 20);
    CHECK(row.barTop == 28);
    CHECK(row.fieldLeft + row.fieldWidth == 1276 - OBT_FIELD_GAP);

    /* the same height: both at the top */
    obt_place(0, 0, 640, 100, 14, 8, &row);
    CHECK(row.height == 14 && row.barTop == 0 && row.fieldTop == 0);

    /* a narrow window: the field keeps its least width */
    obt_place(4, 20, 204, 150, 30, 8, &row);
    CHECK(row.fieldWidth == OBT_FIELD_MIN);

    /* the room the group gets leaves the field that least width: a group
     * as wide as its room and the field beside it end inside the window */
    {
        int inside, room;
        for (inside = 200; inside <= 1920; inside += 40) {
            room = obt_room(inside);
            obt_place(4, 0, 4 + inside, room, 30, 8, &row);
            CHECK(room > 0 && room < inside);
            CHECK(row.fieldWidth >= OBT_FIELD_MIN);
            CHECK(row.fieldLeft + row.fieldWidth <= 4 + inside);
        }
        /* and a window too small for any button still gets a room of 1,
         * which no button fits */
        CHECK(obt_room(40) == 1);
        CHECK(obt_room(0) == 1);
    }

    if (failures) {
        printf("test_ob_toolbar: %d FAILED\n", failures);
        return 1;
    }
    printf("test_ob_toolbar: PASS\n");
    return 0;
}
