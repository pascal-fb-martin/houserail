/* HouseRail - a simple web server to control model trains traffic.
 *
 * Copyright 2026, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * testpath.c - test houserail_path.c
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * testpath
 */
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "echttp.h"

#include "../houserail_track.h"
#include "../houserail_path.h"

#include "testlib.h"

// Mockup track topology for testing.
//
int houserail_track_walk (struct TrackRange *path, int size,
                          const struct TrackLocation *limit1,
                          const struct TrackLocation *limit2,
                          int direction, int max) {

    if (!limit2) {
        path[0].line = limit1->line;
        path[0].segment = 0;
        path[0].low  = limit1->post;
        printf ("      distance %d: ", max);
        if (max == 10) {
            path[0].high = (direction >= 0)?limit1->post+max:limit1->post-max;
            printf ("single segment %s [%d, %d]\n",
                    limit1->line, path[0].low, path[0].high);
            return 1;
        }
        if (max == 100) {
            path[0].high = (direction >= 0)?limit1->post+10:limit1->post-10;
            path[1].line = "xxx";
            path[1].segment = 0;
            path[1].low  = (direction >= 0)?0:200;
            path[1].high  = (direction >= 0)?90:110;
            printf ("two segments %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high);
            return 2;
        }
        if (max == 110) {
            path[0].high = (direction >= 0)?limit1->post+10:limit1->post-10;
            path[1].line = "zzz";
            path[1].segment = 0;
            path[1].low  = (direction >= 0)?0:200;
            path[1].high  = (direction >= 0)?90:110;
            printf ("two segments %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high);
            return 2;
        }
        if (max == 190) {
            path[0].high = (direction >= 0)?100:0;
            path[1].line = "zzz";
            path[1].segment = 0;
            path[1].low  = (direction >= 0)?0:90;
            path[1].high  = (direction >= 0)?90:0;
            printf ("two segments %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high);
            return 2;
        }
        if (max == 200) {
            path[0].high = (direction >= 0)?limit1->post+10:limit1->post-10;
            path[1].line = "xxx";
            path[1].segment = 0;
            path[1].low  = (direction >= 0)?0:200;
            path[1].high  = (direction >= 0)?90:110;
            path[2].line = "yyy";
            path[2].segment = 0;
            path[2].low  = (direction >= 0)?0:100;
            path[2].high  = (direction >= 0)?100:0;
            printf ("three segments %s [%d, %d], %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high,
                    path[2].line, path[2].low, path[2].high);
            return 3;
        }
        printf ("unsupported length %d!\n", max);
        return 0;
    }
    printf ("      Unsupported case!\n");
    return 0; // TBD
}

static void dumppath (const struct TrackPath *path) {

    int i;
    for (i = 0; i < path->count; ++i) {
        printf ("      path[%02d] line %s [%d, %d]\n", i,
                path->sections[i].line,
                path->sections[i].low,
                path->sections[i].high);
    }
}

int main (int argc, const char **argv) {

    struct TrackPath testpath;
    struct TrackLocation start;

    testpath.size = 0;
    testpath.count = 0;
    testpath.sections = 0;

    start.line = "aaa";
    start.post = 20;

    int done = houserail_path_span (&testpath, &start, 1, 10);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 1) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10)),
            "houserail_path_span(start, up, distance 10)");

    done = houserail_path_lengthen (&testpath, 100, 1);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 20) &&
             (!strcmp (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 90)),
            "houserail_path_lengthen(up, distance 100)");

    done = houserail_path_lengthen (&testpath, 110, 1);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 20) &&
             (!strcmp (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 100) &&
             (!strcmp (testpath.sections[2].line, "zzz")) &&
             (testpath.sections[2].low == 0) &&
             (testpath.sections[2].high == 90)),
            "houserail_path_lengthen(up, distance 110) -- 2nd extension");

    houserail_path_span (&testpath, &start, 1, 10);
    dumppath (&testpath);
    done = houserail_path_lengthen (&testpath, 200, 1);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 20) &&
             (!strcmp (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 90) &&
             (!strcmp (testpath.sections[2].line, "yyy")) &&
             (testpath.sections[2].low == 0) &&
             (testpath.sections[2].high == 100)),
            "houserail_path_lengthen(up, distance 200)");

    done = houserail_path_span (&testpath, &start, -1, 10);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 1) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post - 10)),
            "houserail_path_span(start, down, distance 10)");

    done = houserail_path_lengthen (&testpath, 100, -1);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == 0) &&
             (!strcmp (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 200) &&
             (testpath.sections[1].high == 110)),
            "houserail_path_lengthen(down, distance 100)");

    done = houserail_path_span (&testpath, &start, 1, 100);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10) &&
             (!strcmp (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 90)),
            "houserail_path_span(start, up, distance 100)");

    done = houserail_path_span (&testpath, &start, -1, 100);
    dumppath (&testpath);
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post - 10) &&
             (!strcmp (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 200) &&
             (testpath.sections[1].high == 110)),
            "houserail_path_span(start, down, distance 100)");

    struct TrackRange testrange;
    testrange.line = "xxx";
    testrange.low = 120;
    testrange.high = 140;

    done = houserail_path_covers (&testpath, &testrange);
    assert ((done == 1), "houserail_path_covers (xxx [120, 140])");

    testrange.low = 100;
    done = houserail_path_covers (&testpath, &testrange);
    assert ((done == 1), "houserail_path_covers (xxx [100, 140])");

    testrange.low = 210;
    testrange.high = 240;
    done = houserail_path_covers (&testpath, &testrange);
    assert ((done == 0), "houserail_path_covers (xxx [210, 240])");

    testrange.line = "www";
    testrange.low = 120;
    testrange.high = 140;
    done = houserail_path_covers (&testpath, &testrange);
    assert ((done == 0), "houserail_path_covers (www [120, 140])");

    return summary ("houserail_path.c");
}

