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

#include <echttp.h>
#include <echttp_libc.h>

#include "../houserail_track.h"
#include "../houserail_path.h"

#include "testlib.h"

// Mockup track topology for testing.
//
int houserail_track_walk (struct TrackRange *path, int size,
                          const struct TrackLocation *limit1,
                          const struct TrackLocation *limit2,
                          int direction, int max) {

    path[0].line = limit1->line;
    path[0].segment = 0;
    path[0].low  = limit1->post;

    if (!limit2) {
        if (max <= 0) {
            printf ("      invalid walk!\n");
            return 0;
        }
        printf ("      walk distance %d: ", max);
        if (max == 10) {
            path[0].high = (direction >= 0)?limit1->post+max:limit1->post-max;
            printf ("segment %s [%d, %d]\n",
                    limit1->line, path[0].low, path[0].high);
            return 1;
        }
        if (max == 100) {
            path[0].high = (direction >= 0)?limit1->post+10:limit1->post-10;
            path[1].line = "xxx";
            path[1].segment = 0;
            path[1].low  = (direction >= 0)?0:200;
            path[1].high  = (direction >= 0)?90:110;
            printf ("segments %s [%d, %d], %s [%d, %d]\n",
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
            printf ("segments %s [%d, %d], %s [%d, %d]\n",
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
            printf ("segments %s [%d, %d], %s [%d, %d]\n",
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
            printf ("segments %s [%d, %d], %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high,
                    path[2].line, path[2].low, path[2].high);
            return 3;
        }
        printf ("unsupported length %d!\n", max);
        return 0;
    }
    printf ("      walk from %s, %d to %s, %d: ", limit1->line, limit1->post, limit2->line, limit2->post);
    if (strsame (limit2->line, "ccc")) {
        if ((limit2->post > 200) && (limit2->post < 300)) {
            path[0].high = (direction >= 0)?limit1->post+10:limit1->post-10;
            path[1].line = "bbb";
            path[1].segment = 0;
            path[1].low = (direction >= 0)?100:130;
            path[1].high = (direction >= 0)?130:100;
            path[2].line = limit2->line;
            path[2].segment = 0;
            path[2].low = (direction >= 0)?200:limit2->post;
            path[2].high = (direction >= 0)?limit2->post:200;
            printf ("segments %s [%d, %d], %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high,
                    path[2].line, path[2].low, path[2].high);
            return 3;
        }
        printf ("unsupported limit %d!\n", limit2->post);
        return 0;
    }
    if (strsame (limit2->line, "ddd")) {
        if ((limit2->post > 300) && (limit2->post < 400)) {
            path[0].high = (direction >= 0)?300:200;
            path[1].line = limit2->line;
            path[1].segment = 0;
            path[1].low = (direction >= 0)?300:limit2->post;
            path[1].high = (direction >= 0)?limit2->post:300;
            printf ("segments %s [%d, %d], %s [%d, %d]\n",
                    path[0].line, path[0].low, path[0].high,
                    path[1].line, path[1].low, path[1].high);
            return 2;
        }
        printf ("unsupported limit %d!\n", limit2->post);
        return 0;
    }
    printf ("unsupported case!\n");
    return 0; // TBD
}

static void dumppath (const struct TrackPath *path) {

    int i;
    printf ("      path direction %s\n", (path->direction >= 0)?"up":"down");
    for (i = 0; i < path->count; ++i) {
        printf ("      path[%02d] %s [%d, %d]\n", i,
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

    starting ("houserail_path_span(up) and houserail_path_lengthen()");
    int done = houserail_path_span (&testpath, &start, 10, 1);
    int passed =
    assert (((done == 1) &&
             (testpath.count == 1) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10)),
            "houserail_path_span(start, distance 10, up)");
    if (!passed) dumppath (&testpath);

    done = houserail_path_lengthen (&testpath, 100);
    passed =
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 20) &&
             (strsame (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 90)),
            "houserail_path_lengthen(up, distance 100)");
    if (!passed) dumppath (&testpath);

    done = houserail_path_lengthen (&testpath, 110);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 20) &&
             (strsame (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 100) &&
             (strsame (testpath.sections[2].line, "zzz")) &&
             (testpath.sections[2].low == 0) &&
             (testpath.sections[2].high == 90)),
            "houserail_path_lengthen(up, distance 110) -- 2nd extension");
    if (!passed) dumppath (&testpath);

    houserail_path_span (&testpath, &start, 10, 1);
    done = houserail_path_lengthen (&testpath, 200);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 20) &&
             (strsame (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 90) &&
             (strsame (testpath.sections[2].line, "yyy")) &&
             (testpath.sections[2].low == 0) &&
             (testpath.sections[2].high == 100)),
            "houserail_path_lengthen(up, distance 200)");
    if (!passed) dumppath (&testpath);

    done = houserail_path_span (&testpath, &start, 10, -1);
    passed =
    assert (((done == 1) &&
             (testpath.count == 1) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post - 10)),
            "houserail_path_span(start, distance 10, down)");
    if (!passed) dumppath (&testpath);

    starting ("houserail_path_lengthen(down)");
    done = houserail_path_lengthen (&testpath, 100);
    passed =
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == 0) &&
             (strsame (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 200) &&
             (testpath.sections[1].high == 110)),
            "houserail_path_lengthen(down, distance 100)");
    if (!passed) dumppath (&testpath);

    done = houserail_path_span (&testpath, &start, 100, 1);
    passed =
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10) &&
             (strsame (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 0) &&
             (testpath.sections[1].high == 90)),
            "houserail_path_span(start, distance 100, up)");
    if (!passed) dumppath (&testpath);

    done = houserail_path_span (&testpath, &start, 100, -1);
    passed =
    assert (((done == 1) &&
             (testpath.count == 2) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post - 10) &&
             (strsame (testpath.sections[1].line, "xxx")) &&
             (testpath.sections[1].low == 200) &&
             (testpath.sections[1].high == 110)),
            "houserail_path_span(start, distance 100, down)");
    if (!passed) dumppath (&testpath);

    starting ("houserail_path_covers()");
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

    starting ("houserail_path_set()");
    struct TrackLocation end;
    end.line = "ccc";
    end.post = 230;

    houserail_path_erase (&testpath);
    done = houserail_path_set (&testpath, &start, &end, 1);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (testpath.sections[2].line == end.line) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == end.post)),
             "houserail_path_set(start, end, up)");
    if (!passed) dumppath (&testpath);

    struct TrackLocation rollup = start;
    rollup.post += 5;
    struct TrackLocation truncate = end;
    truncate.post -= 5;

    done = houserail_path_set (&testpath, &rollup, &truncate, 1);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post+5) &&
             (testpath.sections[0].high == start.post + 10) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (testpath.sections[2].line == end.line) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == end.post-5)),
             "houserail_path_set(rollup, truncate, up)");
    if (!passed) dumppath (&testpath);

    houserail_path_set (&testpath, &start, &end, 1);
    struct TrackLocation extend = end;
    extend.post += 5;

    done = houserail_path_set (&testpath, &rollup, &extend, 1);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == rollup.line) &&
             (testpath.sections[0].low == rollup.post) &&
             (testpath.sections[0].high == rollup.post + 5) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (testpath.sections[2].line == extend.line) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == extend.post)),
             "houserail_path_set(rollup, extend, up)");
    if (!passed) dumppath (&testpath);

    starting ("houserail_path_turn()");
    houserail_path_turn (&testpath, -1);
    passed =
    assert (((testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == extend.line) &&
             (testpath.sections[0].low == extend.post) &&
             (testpath.sections[0].high == 200) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 130) &&
             (testpath.sections[1].high == 100) &&
             (testpath.sections[2].line == rollup.line) &&
             (testpath.sections[2].low == rollup.post+5) &&
             (testpath.sections[2].high == rollup.post)),
             "houserail_path_turn(down)");
    if (!passed) dumppath (&testpath);

    starting ("houserail_path_rollup()");
    houserail_path_set (&testpath, &start, &end, 1);

    done = houserail_path_rollup (&testpath, &rollup);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == rollup.line) &&
             (testpath.sections[0].low == rollup.post) &&
             (testpath.sections[0].high == rollup.post + 5) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (testpath.sections[2].line == end.line) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == end.post)),
             "houserail_path_rollup(up)");
    if (!passed) dumppath (&testpath);

    houserail_path_set (&testpath, &start, &end, 1);

    rollup.line = "bbb";
    rollup.post = 120;

    done = houserail_path_rollup (&testpath, &rollup);
    passed =
    assert (done == 1, "houserail_path_rollup(complete segment) status") &&
    assert (testpath.count == 2, "houserail_path_rollup(complete segment) count") &&
    assert (testpath.count < testpath.size, "houserail_path_rollup(complete segment) size") &&
    assert(strsame (testpath.sections[0].line, "bbb"), "houserail_path_rollup(complete segment) section 0 line") &&
    assert (testpath.sections[0].low == 120, "houserail_path_rollup(complete segment) section 0 low") &&
    assert (testpath.sections[0].high == 130, "houserail_path_rollup(complete segment) section 0 high") &&
    assert (testpath.sections[1].line == end.line, "houserail_path_rollup(complete segment) section 1 line") &&
    assert (testpath.sections[1].low == 200, "houserail_path_rollup(complete segment) section 1 low") &&
    assert (testpath.sections[1].high == end.post, "houserail_path_rollup(complete segment) section 1 high");
    digest (passed, "houserail_path_rollup(complete segment)");
    if (!passed) dumppath (&testpath);

    starting ("houserail_path_truncate()");
    houserail_path_set (&testpath, &start, &end, 1);

    done = houserail_path_truncate (&testpath, &truncate);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (testpath.sections[2].line == truncate.line) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == truncate.post)),
             "houserail_path_truncate(up)");
    if (!passed) dumppath (&testpath);

    starting ("houserail_path_extend()");
    houserail_path_set (&testpath, &start, &end, 1);

    done = houserail_path_extend (&testpath, &extend);
    passed =
    assert (((done == 1) &&
             (testpath.count == 3) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (testpath.sections[2].line == extend.line) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == extend.post)),
             "houserail_path_extend()");
    if (!passed) dumppath (&testpath);

    extend.line = "ddd";
    extend.post = 310;

    done = houserail_path_extend (&testpath, &extend);
    passed =
    assert (((done == 1) &&
             (testpath.count == 4) && (testpath.count < testpath.size) &&
             (testpath.sections[0].line == start.line) &&
             (testpath.sections[0].low == start.post) &&
             (testpath.sections[0].high == start.post + 10) &&
             (strsame (testpath.sections[1].line, "bbb")) &&
             (testpath.sections[1].low == 100) &&
             (testpath.sections[1].high == 130) &&
             (strsame (testpath.sections[2].line, end.line)) &&
             (testpath.sections[2].low == 200) &&
             (testpath.sections[2].high == 300) &&
             (strsame (testpath.sections[3].line, extend.line)) &&
             (testpath.sections[3].low == 300) &&
             (testpath.sections[3].high == extend.post)),
             "houserail_path_extend(up) -- new segments");
    if (!passed) dumppath (&testpath);

    // Test houserail_path_move (const struct TrackPath *path,
    //                           struct TrackLocation *point,
    //                           int distance, int direction);

    starting ("Test houserail_path_move() with a path going up");
    dumppath (&testpath);
    struct TrackLocation cursor;
    cursor.line = start.line;
    cursor.post = start.post;
    printf ("   Starting point: line %s post %d\n", cursor.line, cursor.post);
    done = houserail_path_move (&testpath, &cursor, 5, 1);
    passed =
    assert (done, "houserail_path_move (+5 up) status") &&
    assert (cursor.post == start.post + 5, "houserail_path_move (+5 up) post") &&
    assert (strsame (cursor.line, start.line), "houserail_path_move (+5 up) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    done = houserail_path_move (&testpath, &cursor, 10, 1);
    passed =
    assert (done, "houserail_path_move (+10 up) status") &&
    assert (cursor.post == 105, "houserail_path_move (+10 up) post") &&
    assert (strsame (cursor.line, "bbb"), "houserail_path_move (+10 up) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    done = houserail_path_move (&testpath, &cursor, 2, -1);
    passed =
    assert (done, "houserail_path_move (-2 up) status") &&
    assert (cursor.post == 103, "houserail_path_move (-2 up) post") &&
    assert (strsame (cursor.line, "bbb"), "houserail_path_move (-2 up) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    done = houserail_path_move (&testpath, &cursor, 5, -1);
    passed =
    assert (done, "houserail_path_move (-5 up) status") &&
    assert (cursor.post == 28, "houserail_path_move (-5 up) post") &&
    assert (strsame (cursor.line, start.line), "houserail_path_move (-5 up) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    // Do the same moves, but with a path oriented in the other direction.

    starting ("Test houserail_path_move() with a path going down");
    done = houserail_path_set (&testpath, &start, &end, -1);
    dumppath (&testpath);

    cursor.line = start.line;
    cursor.post = start.post;
    printf ("   Starting point: line %s post %d\n", cursor.line, cursor.post);
    done = houserail_path_move (&testpath, &cursor, 5, 1);
    passed =
    assert (done, "houserail_path_move (+5 down) status") &&
    assert (cursor.post == start.post - 5, "houserail_path_move (+5 down) post") &&
    assert (strsame (cursor.line, start.line), "houserail_path_move (+5 down) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    done = houserail_path_move (&testpath, &cursor, 10, 1);
    passed =
    assert (done, "houserail_path_move (+10 down) status") &&
    assert (cursor.post == 125, "houserail_path_move (+10 down) post") &&
    assert (strsame (cursor.line, "bbb"), "houserail_path_move (+10 down) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    done = houserail_path_move (&testpath, &cursor, 2, -1);
    passed =
    assert (done, "houserail_path_move (-2 down) status") &&
    assert (cursor.post == 127, "houserail_path_move (-2 down) post") &&
    assert (strsame (cursor.line, "bbb"), "houserail_path_move (-2 down) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    done = houserail_path_move (&testpath, &cursor, 5, -1);
    passed =
    assert (done, "houserail_path_move (-5 down) status") &&
    assert (cursor.post == 12, "houserail_path_move (-5 down) post") &&
    assert (strsame (cursor.line, start.line), "houserail_path_move (-5 down) line");
    printf ("   after moving: line %s post %d\n", cursor.line, cursor.post);

    return summary ("houserail_path.c");
}

