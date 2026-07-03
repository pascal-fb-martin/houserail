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

#include "echttp.h"
#include "houseconfig.h"

#include "../houserail_track.h"

#include "testlib.h"

static const char *test_update (void) {
    const char *error = houserail_track_reload ();
    if (error) printf ("** Cannot load track topology: %s\n", error);
    else printf ("== Track topology loaded.\n");
    return error;
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    // houserail_track_testmode ();

    houserail_track_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testtrack", test_update, argc, argv);
    if (error) {
        printf ("Config error: %s\n", error);
        return 1;
    }

    // Test int houserail_track_civil (const struct TrackLocation *point, int direction);

    struct TrackLocation point;
    point.line = "main";
    point.segment = 0;

    point.post = 10;
    int result = houserail_track_civil (&point, 1);
    assert (result == 60, "houserail_track_civil() == 60 at main post 10 forward");

    result = houserail_track_civil (&point, -1);
    assert (result == 30, "houserail_track_civil() == 30 at main post 10 backward");

    result = houserail_track_civil (&point, 0);
    assert (result == 60, "houserail_track_civil() == 60 at main post 10 still");

    point.post = 30;
    result = houserail_track_civil (&point, 1);
    assert (result == 60, "houserail_track_civil() == 60 at main post 30");

    point.post = 50;
    result = houserail_track_civil (&point, 1);
    assert (result == 60, "houserail_track_civil() == 60 at main post 50");

    point.post = 70;
    result = houserail_track_civil (&point, 1);
    assert (result == 30, "houserail_track_civil() == 30 at main post 70");

    point.post = 90;
    result = houserail_track_civil (&point, 1);
    assert (result == 30, "houserail_track_civil() == 30 at main post 90");

    point.post = 100;
    result = houserail_track_civil (&point, 1);
    assert (result == 30, "houserail_track_civil() == 30 at main post 100");

    point.post = 200;
    result = houserail_track_civil (&point, 1);
    assert (result == 60, "houserail_track_civil() == 60 at main post 200");

    point.post = 280;
    result = houserail_track_civil (&point, 1);
    assert (result == 30, "houserail_track_civil() == 30 at main post 280");

    point.post = 330;
    result = houserail_track_civil (&point, 1);
    assert (result <= 0, "houserail_track_civil() at invalid location main post 330");

    point.line = "_invalid_";
    point.post = 280;
    result = houserail_track_civil (&point, 1);
    assert (result <= 0, "houserail_track_civil() at invalid location _invalid_ post 330");

    // Test houserail_track_vicinity (struct TrackLocation *point, const char *id, int direction);

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "main-2", 1);
    int passed =
    assert (result == 1, "houserail_track_vicinity(main-2, forward) return status") &&
    assert (!strcmp(point.line, "main"), "houserail_track_vicinity(main-2, forward) return line") &&
    assert (point.post == 20, "houserail_track_vicinity(main-2, forward) return post");
    digest (passed, "houserail_track_vicinity(main-2, forward)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "main-2", 0);
    passed =
    assert (result == 1, "houserail_track_vicinity(main-2, still) return status") &&
    assert (!strcmp(point.line, "main"), "houserail_track_vicinity(main-2, still) return line") &&
    assert (point.post == 30, "houserail_track_vicinity(main-2, still) return post");
    digest (passed, "houserail_track_vicinity(main-2, still)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "main-2", -1);
    passed =
    assert (result == 1, "houserail_track_vicinity(main-2, backward) return status") &&
    assert (!strcmp(point.line, "main"), "houserail_track_vicinity(main-2, backward) return line") &&
    assert (point.post == 40, "houserail_track_vicinity(main-2, backward) return post");
    digest (passed, "houserail_track_vicinity(main-2, backward)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "reed-3", 1);
    passed =
    assert (result == 1, "houserail_track_vicinity(reed-3,forward) return status") &&
    assert (!strcmp(point.line, "main"), "houserail_track_vicinity(reed-3,forward) return line") &&
    assert (point.post == 49, "houserail_track_vicinity(reed-3,forward) return post");
    digest (passed, "houserail_track_vicinity(reed-3,forward)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "reed-3", 0);
    passed =
    assert (result == 1, "houserail_track_vicinity(reed-3, still) return status") &&
    assert (!strcmp(point.line, "main"), "houserail_track_vicinity(reed-3, still) return line") &&
    assert (point.post == 50, "houserail_track_vicinity(reed-3, still) return status");
    digest (passed, "houserail_track_vicinity(reed-3, still)");

    point.line = 0;
    point.post = -1;
    point.segment = 0;
    result = houserail_track_vicinity (&point, "reed-3", -1);
    passed =
    assert (result == 1, "houserail_track_vicinity(reed-3, backward) return status") &&
    assert (!strcmp(point.line, "main"), "houserail_track_vicinity(reed-3, backward) return line") &&
    assert (point.post == 51, "houserail_track_vicinity(reed-3, backward) return status");
    digest (passed, "houserail_track_vicinity(reed-3, backward)");

    // Test houserail_track_walk (struct TrackRange *path, int size,
    //                            const struct TrackLocation *limit1,
    //                            const struct TrackLocation *limit2,
    //                            int direction, int max);

    // Test houserail_track_distance (const struct TrackLocation *point1,
    //                                const struct TrackLocation *point2,
    //                                int direction, int max);

    // Test houserail_track_segment (const struct TrackLocation *point);

    // Test houserail_track_switch ()

    return summary ("houserail_track.c");
}

