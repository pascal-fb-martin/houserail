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
 * testtrack.c - test houserail_track.c
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * testtrack -config=<layout file>
 */
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <echttp.h>
#include <echttp_libc.h>
#include <houseconfig.h>

#include "../houserail_catalog.h"
#include "../houserail_topology.h"
#include "../houserail_track.h"

#include "testlib.h"

static const char *test_update (void) {

    const char *error = houserail_topology_reload ();
    if (error) {
        printf ("** Cannot load track topology: %s\n", error);
    } else {
        error = houserail_track_reload ();
        if (error) printf ("** Cannot load track topology: %s\n", error);
        else printf ("== Track topology loaded.\n");
    }
    return error;
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    // houserail_track_testmode (1);

    starting ("Loading layout configuration");
    houseconfig_default ("--config=./testloop.json");
    houserail_catalog_default ("--catalog=.");
    houserail_catalog_initialize (argc, argv);
    houserail_topology_initialize (argc, argv);
    houserail_track_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testtrack", test_update, argc, argv);
    if (error) {
        printf ("** Config error: %s\n", error);
        return Errors + 1;
    }

    // Test int houserail_track_civil (const struct TrackLocation *point, int direction);

    starting ("houserail_track_civil()");
    struct TrackLocation point;
    const char *cause;
    point.line = "main";
    point.segment = 0;

    point.post = 10;
    int result = houserail_track_civil (&point, 1, &cause);
    assert (result == 60, "houserail_track_civil(main 10, forward) == 60");

    result = houserail_track_civil (&point, -1, &cause);
    assert (result == 30, "houserail_track_civil(main 10, backward) == 30");

    result = houserail_track_civil (&point, 0, &cause);
    assert (result == 60, "houserail_track_civil(main 10, still) == 60");

    point.post = 30;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 60, "houserail_track_civil(main 30, forward) == 60");

    point.post = 50;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 60, "houserail_track_civil(main 50, forward) == 60");

    point.post = 70;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 70, forward) == 30");

    point.post = 90;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 90, forward) == 30");

    point.post = 110;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 110, forward) == 30");

    point.post = 130;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 130, forward) == 30");

    point.post = 150;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 150, forward) == 30");

    point.post = 170;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 60, "houserail_track_civil(main 170, forward) == 60");

    point.post = 190;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 60, "houserail_track_civil(main 190, forward) == 60");

    point.post = 210;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 60, "houserail_track_civil(main 210, forward) == 60");

    point.post = 230;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 230, forward) == 30");

    point.post = 250;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 250, forward) == 30");

    point.post = 270;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 270, forward) == 30");

    point.post = 290;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 290, forward) == 30");

    point.post = 310;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result == 30, "houserail_track_civil(main 310, forward) == 30");

    point.post = 330;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result <= 0, "houserail_track_civil() at invalid location main post 330 forward");

    point.line = "_invalid_";
    point.post = 280;
    result = houserail_track_civil (&point, 1, &cause);
    assert (result <= 0, "houserail_track_civil() at invalid location _invalid_ post 330");

    // Test houserail_track_vicinity (struct TrackLocation *point, const char *id, int direction);

    starting ("houserail_track_vicinity()");
    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "main-2", 1);
    int passed =
    assert (result == 1, "houserail_track_vicinity(main-2, forward) status") &&
    assert (strsame(point.line, "main"), "houserail_track_vicinity(main-2, forward) line") &&
    assert (point.post == 20, "houserail_track_vicinity(main-2, forward) post");
    digest (passed, "houserail_track_vicinity(main-2, forward)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "main-2", 0);
    passed =
    assert (result == 1, "houserail_track_vicinity(main-2, still) status") &&
    assert (strsame(point.line, "main"), "houserail_track_vicinity(main-2, still) line") &&
    assert (point.post == 30, "houserail_track_vicinity(main-2, still) post");
    digest (passed, "houserail_track_vicinity(main-2, still)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "main-2", -1);
    passed =
    assert (result == 1, "houserail_track_vicinity(main-2, backward) status") &&
    assert (strsame(point.line, "main"), "houserail_track_vicinity(main-2, backward) line") &&
    assert (point.post == 40, "houserail_track_vicinity(main-2, backward) post");
    digest (passed, "houserail_track_vicinity(main-2, backward)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "reed-3", 1);
    passed =
    assert (result == 1, "houserail_track_vicinity(reed-3,forward) status") &&
    assert (strsame(point.line, "main"), "houserail_track_vicinity(reed-3,forward) line") &&
    assert (point.post == 49, "houserail_track_vicinity(reed-3,forward) post");
    digest (passed, "houserail_track_vicinity(reed-3,forward)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "reed-3", 0);
    passed =
    assert (result == 1, "houserail_track_vicinity(reed-3, still) status") &&
    assert (strsame(point.line, "main"), "houserail_track_vicinity(reed-3, still) line") &&
    assert (point.post == 50, "houserail_track_vicinity(reed-3, still) status");
    digest (passed, "houserail_track_vicinity(reed-3, still)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "reed-3", -1);
    passed =
    assert (result == 1, "houserail_track_vicinity(reed-3, backward) status") &&
    assert (strsame(point.line, "main"), "houserail_track_vicinity(reed-3, backward) line") &&
    assert (point.post == 51, "houserail_track_vicinity(reed-3, backward) status");
    digest (passed, "houserail_track_vicinity(reed-3, backward)");

    // Test houserail_track_walk (struct TrackRange *path, int size,
    //                            const struct TrackLocation *limit1,
    //                            const struct TrackLocation *limit2,
    //                            int direction, int max);

    starting ("houserail_track_walk()");
    struct TrackLocation limit1;
    struct TrackLocation limit2;
    struct TrackRange path[16];

    limit1.line = limit2.line = "main";
    limit1.post = 30;
    limit2.post = 280;
    result = houserail_track_walk (path, 16, &limit1, &limit2, 1, 0);
    passed =
    assert (result == 1, "houserail_track_walk(main from 30 to 280, forward) status") &&
    assert (strsame(path[0].line, "main"), "houserail_track_walk(main from 30 to 280, forward) line") &&
    assert (path[0].low == 30, "houserail_track_walk(main from 30 to 280, forward) start post") &&
    assert (path[0].high == 280, "houserail_track_walk(main from 30 to 280, forward) end post");
    digest (passed, "houserail_track_walk(main from 30 to 280, forward)");
    if (!passed) printf ("   path[0].low == %d, path[0].high == %d\n",
                         path[0].low, path[0].high);

    result = houserail_track_walk (path, 16, &limit2, &limit1, -1, 0);
    passed =
    assert (result == 1, "houserail_track_walk(main from 280 to 30, backward) status") &&
    assert (strsame(path[0].line, "main"), "houserail_track_walk(main from 280 to 30, backward) line") &&
    assert (path[0].low == 280, "houserail_track_walk(main from 280 to 30, backward) start post") &&
    assert (path[0].high == 30, "houserail_track_walk(main from 280 to 30, backward) end post");
    digest (passed, "houserail_track_walk(main from 280 to 30, backward)");
    if (!passed) printf ("   path[0].low == %d, path[0].high == %d\n",
                         path[0].low, path[0].high);

    result = houserail_track_walk (path, 16, &limit1, &limit2, 1, 100);
    passed =
    assert (result <= 0, "houserail_track_walk(main from 30 to 280 max 100, forward) status");
    if (!passed) printf ("   count == %d, path[0].low == %d, path[0].high == %d\n",
                         result, path[0].low, path[0].high);

    result = houserail_track_walk (path, 16, &limit2, &limit1, -1, 100);
    passed =
    assert (result <= 0, "houserail_track_walk(main from 280 to 30 max 100, backward) status");
    if (!passed) printf ("   count == %d, path[0].low == %d, path[0].high == %d\n",
                         result, path[0].low, path[0].high);

    result = houserail_track_walk (path, 16, &limit1, 0, 1, 200);
    passed =
    assert (result == 1, "houserail_track_walk(main from 30 max 200, forward) status") &&
    assert (strsame(path[0].line, "main"), "houserail_track_walk(main from 30 max 200, forward) line") &&
    assert (path[0].low == 30, "houserail_track_walk(main from 30 max 200, forward) start post") &&
    assert (path[0].high == 230, "houserail_track_walk(main from 30 max 200, forward) end post");
    digest (passed, "houserail_track_walk(main from 30 max 200, forward)");
    if (!passed) printf ("   path[0].low == %d, path[0].high == %d\n",
                         path[0].low, path[0].high);

    result = houserail_track_walk (path, 16, &limit2, 0, -1, 200);
    passed =
    assert (result == 1, "houserail_track_walk(main from 280 max 200, backward) status") &&
    assert (strsame(path[0].line, "main"), "houserail_track_walk(main from 280 max 200, backward) line") &&
    assert (path[0].low == 280, "houserail_track_walk(main from 280 max 200, backward) start post") &&
    assert (path[0].high == 80, "houserail_track_walk(main from 280 max 200, backward) end post");
    digest (passed, "houserail_track_walk(main from 280 max 200, backward)");
    if (!passed) printf ("   path[0].low == %d, path[0].high == %d\n",
                         path[0].low, path[0].high);

    // These two specific tests walk over the loop junction, where the post
    // resets.
    // houserail_track_testmode (1);
    result = houserail_track_walk (path, 16, &limit2, 0, 1, 200);
    passed =
    assert (result == 2, "houserail_track_walk(main from 280 max 200, forward) status") &&
    assert (strsame(path[0].line, "main"), "houserail_track_walk(main from 280 max 200, forward) line section 0") &&
    assert (path[0].low == 280, "houserail_track_walk(main from 280 max 200, forward) start post section 0") &&
    assert (path[0].high == 320, "houserail_track_walk(main from 280 max 200, forward) end post section 0") &&
    assert (strsame(path[1].line, "main"), "houserail_track_walk(main from 280 max 200, forward) line section 1") &&
    assert (path[1].low == 0, "houserail_track_walk(main from 280 max 200, forward) start post section 1") &&
    assert (path[1].high == 160, "houserail_track_walk(main from 280 max 200, forward) end post section 1");
    digest (passed, "houserail_track_walk(main from 280 max 200, forward)");
    if (!passed) {
        int i;
        for (i = 0; i < result; ++i)
            printf ("   path[%d].low == %d, path[%d].high == %d\n",
                    i, path[i].low, i, path[i].high);
    }

    result = houserail_track_walk (path, 16, &limit1, 0, -1, 200);
    passed =
    assert (result == 2, "houserail_track_walk(main from 30 max 200, backward) status") &&
    assert (strsame(path[0].line, "main"), "houserail_track_walk(main from 30 max 200, backward) line section 0") &&
    assert (path[0].low == 30, "houserail_track_walk(main from 30 max 200, backward) start post section 0") &&
    assert (path[0].high == 0, "houserail_track_walk(main from 30 max 200, backward) end post section 0") &&
    assert (strsame(path[1].line, "main"), "houserail_track_walk(main from 30 max 200, backward) line section 1") &&
    assert (path[1].low == 320, "houserail_track_walk(main from 30 max 200, backward) start post section 1") &&
    assert (path[1].high == 150, "houserail_track_walk(main from 30 max 200, backward) end post section 1");
    digest (passed, "houserail_track_walk(main from 30 max 200, backward)");
    if (!passed) {
        int i;
        for (i = 0; i < result; ++i)
            printf ("   path[%d].low == %d, path[%d].high == %d\n",
                    i, path[i].low, i, path[i].high);
    }

    // Test houserail_track_distance (const struct TrackLocation *point1,
    //                                const struct TrackLocation *point2,
    //                                int direction, int max);
    // houserail_track_testmode (1);
    starting ("houserail_track_distance()");
    result = houserail_track_distance (&limit1, &limit2, 1, 0);
    passed =
    assert (result == 250, "houserail_track_distance (main from 30 to 280, forward)");
    if (!passed) printf ("   distance = %d\n", result);

    result = houserail_track_distance (&limit1, &limit2, 1, 260);
    passed =
    assert (result == 250, "houserail_track_distance (main from 30 to 280 max 260, forward)");
    if (!passed) printf ("   distance = %d\n", result);

    result = houserail_track_distance (&limit1, &limit2, 1, 240);
    passed =
    assert (result == -1, "houserail_track_distance (main from 30 to 280 max 240, forward)");
    if (!passed) printf ("   distance = %d\n", result);

    // Test houserail_track_segment (const struct TrackLocation *point,
    //                               int direction);

    starting ("houserail_track_segment()");
    const char *segment = houserail_track_segment (&limit1, -1);
    passed =
    assert (strsame (segment, "main-2"), "houserail_track_segment (main 30 backward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit1, 0);
    passed =
    assert (strsame (segment, "main-2"), "houserail_track_segment (main 30 still)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit1, 1);
    passed =
    assert (strsame (segment, "main-2"), "houserail_track_segment (main 30 forward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, -1);
    passed =
    assert (strsame (segment, "main-15"), "houserail_track_segment (main 280 backward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 1);
    passed =
    assert (strsame (segment, "main-14"), "houserail_track_segment (main 280 forward)");
    if (!passed) printf ("   segment %s\n", segment);

    limit2.post = 270;
    segment = houserail_track_segment (&limit2, -1);
    passed =
    assert (strsame (segment, "main-14"), "houserail_track_segment (main 270 backward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 0);
    passed =
    assert (strsame (segment, "main-14"), "houserail_track_segment (main 270 still)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 1);
    passed =
    assert (strsame (segment, "main-14"), "houserail_track_segment (main 270 forward)");
    if (!passed) printf ("   segment %s\n", segment);

    limit2.post = 260;
    segment = houserail_track_segment (&limit2, -1);
    passed = (strsame (segment, "main-14"));
    assert (passed, "houserail_track_segment (main 260 backward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 1);
    passed = (strsame (segment, "main-13"));
    assert (passed, "houserail_track_segment (main 260, forward)");
    if (!passed) printf ("   segment %s\n", segment);

    limit2.post = 320;
    segment = houserail_track_segment (&limit2, -1);
    passed =
    assert (segment != 0, "houserail_track_segment (main 320 backward) valid") &&
    assert (strsame (segment, "main-16"), "houserail_track_segment (main 320 backward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 0);
    passed = (strsame (segment, "main-16"));
    assert (passed, "houserail_track_segment (main 320 still)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 1);
    passed = (strsame (segment, "main-16"));
    assert (passed, "houserail_track_segment (main 320 forward)");
    if (!passed) printf ("   segment %s\n", segment);

    limit2.post = 0;
    segment = houserail_track_segment (&limit2, -1);
    passed = (strsame (segment, "main-1"));
    assert (passed, "houserail_track_segment (main 0 backward)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 0);
    passed = (strsame (segment, "main-1"));
    assert (passed, "houserail_track_segment (main 0 still)");
    if (!passed) printf ("   segment %s\n", segment);

    segment = houserail_track_segment (&limit2, 1);
    passed = (strsame (segment, "main-1"));
    assert (passed, "houserail_track_segment (main 0 forward)");
    if (!passed) printf ("   segment %s\n", segment);

    // Test houserail_track_switch ()
    // TBD..
    return summary ("houserail_track.c");
}

