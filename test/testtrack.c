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

#include "echttp.h"
#include "houseconfig.h"

#include "../houserail_track.h"

#include "testlib.h"

static const char *test_update (void) {
    const char *error = houserail_track_reload ();
    if (error) printf ("Cannot load track topology: %s\n", error);
    else printf ("Track topology loaded.\n");
    return error;
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    houserail_track_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testtrack", test_update, argc, argv);
    if (error) {
        printf ("Config error: %s\n", error);
        return 1;
    }

    // Test houserail_track_civil()

    // Test houserail_track_vicinit()

    // Test houserail_track_walk()

    // Test houserail_track_distance()

    // Test houserail_track_segment()

    // Test houserail_track_switch ()

    return summary ("houserail_track.c");
}

