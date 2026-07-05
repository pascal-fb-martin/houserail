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
 * testtrain.c - test houserail_train.c
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * testtrain
 */
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "houseconfig.h"

#include "../houserail_field.h"
#include "../houserail_track.h"
#include "../houserail_train.h"

#include "testlib.h"

static const char *test_update (void) {
    const char *error = houserail_track_reload ();
    if (error) {
        printf ("** Cannot load track topology: %s\n", error);
        return error;
    }
    printf ("== Track topology loaded.\n");
    error = houserail_train_reload ();
    if (error) {
        printf ("** Cannot load rolling stock: %s\n", error);
        return error;
    }
    printf ("== Rolling stock loaded.\n");
    return error;
}

FleetListener *houserail_field_fleet_subscribe (FleetListener *listener) {
    return 0;
}

static int LastSpeedOrder = 0;

int houserail_field_fleet_speed (int index) {
    return LastSpeedOrder;
}

const char *houserail_field_fleet_move (const char *id, int speed) {
    printf ("   houserail_field_fleet_move (%s, %d)\n", id, speed);
    LastSpeedOrder = speed;
    return 0;
}

const char *houserail_field_fleet_stop (const char *id, int emergency) {
    printf ("   houserail_field_fleet_stop (%s, %s)\n",
            id, emergency?"emergency":"normal");
    LastSpeedOrder = 0;
    return 0;
}

const char *houserail_field_switch_set (const char *id, const char *state) {
    printf ("   houserail_field_switch_set (%s, %s)\n", id, state);
    return 0;
}

const char *houserail_field_signal_set (const char *id, const char *state) {
    printf ("   houserail_field_signal_set (%s, %s)\n", id, state);
    return 0;
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    // houserail_track_testmode ();

    houseconfig_default ("--config=./testloop.json");
    houserail_track_initialize (argc, argv);
    houserail_train_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testtrain", test_update, argc, argv);
    if (error) {
        printf ("** Config error: %s\n", error);
        return Errors + 1;
    }

    // Test const char *houserail_train_consist (const char *id,
    //                                           const char *cars[], int count);
    const char *cars[] = {"pfm4001", "pfm1001", "pfm1002", "pfm1003"};
    error = houserail_train_consist ("train1", cars, 4);
    int passed =
    assert (error == 0, "houserail_train_consist (train1) status");
    digest (passed, "houserail_train_consist (train1)");
    if (!passed) printf ("   error: %s\n", error);

    // Test const char *houserail_train_enter (const char *id,
    //                                         const char *facing, int orientation);

    // Test const char *houserail_train_park (const char *id);

    // Test const char *houserail_train_delete (const char *id);

    return summary ("houserail_train.c");
}

