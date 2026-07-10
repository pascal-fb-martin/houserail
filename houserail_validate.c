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
 * houserail_validate.c - validate the layout configuration
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * railvalidate -config=<layout file>
 */
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "houseconfig.h"

#include "houserail_track.h"
#include "houserail_train.h"

static const char *validate_update (void) {
    const char *error = houserail_track_reload ();
    if (error) printf ("** Cannot load track topology: %s\n", error);
    else printf ("== Track topology loaded.\n");
    error = houserail_train_reload ();
    if (error) printf ("** Cannot load train fleet: %s\n", error);
    else printf ("== Train fleet loaded.\n");
    return error;
}

int main (int argc, const char **argv) {

    houserail_track_testmode (1);
    houserail_train_testmode (1);

    houserail_track_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testtrack", validate_update, argc, argv);
    if (error) {
        printf ("** Config error: %s\n", error);
        return 1;
    }
    return 0;
}

