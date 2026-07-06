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
#include <sys/time.h>

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

static FleetListener *TestListener = 0;

FleetListener *houserail_field_fleet_subscribe (FleetListener *listener) {
    TestListener = listener;
    return 0;
}

static long long now (void) {
    struct timeval tv;
    gettimeofday (&tv, 0);
    long long result = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    return result;
}

static int LastSpeedOrder = 0;

int houserail_field_fleet_speed (int index) {
    return LastSpeedOrder;
}

const char *houserail_field_fleet_move (const char *id, int speed) {
    printf ("   houserail_field_fleet_move (%s, %d)\n", id, speed);
    LastSpeedOrder = speed;
    if (TestListener) TestListener (id, 1);
    return 0;
}

const char *houserail_field_fleet_stop (const char *id, int emergency) {
    printf ("   houserail_field_fleet_stop (%s, %s)\n",
            id, emergency?"emergency":"normal");
    LastSpeedOrder = 0;
    if (TestListener) TestListener (id, 1);
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

static int trainlist (const char *label) {
    char buffer[16000];
    int size = houserail_train_status (buffer, sizeof(buffer));
    if (size > 0) {
       buffer[size] = 0;
       printf ("== Trains data (%d bytes): %s\n", size, buffer);
    }
    int size2 = houserail_train_locate (buffer, sizeof(buffer));
    if (size2 > 0) { // This may return nothing if all trains are parked.
       buffer[size2] = 0;
       printf ("== Trains location (%d bytes): %s\n", size2, buffer);
    }
    return assert (size > 0, label);
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    // houserail_track_testmode ();

    starting ("Loading layout configuration");
    houseconfig_default ("--config=./testloop.json");
    houserail_track_initialize (argc, argv);
    houserail_train_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testtrain", test_update, argc, argv);
    if (error) {
        printf ("** Config error: %s\n", error);
        return Errors + 1;
    }
    if (TestListener) TestListener ("pfm4001", 1);

    // Test const char *houserail_train_consist (const char *id,
    //                                           const char *cars[], int count);

    starting ("houserail_train_consist(train1, pfm4001 pfm1001 pfm1002 pfm1003)");
    const char *cars[] = {"pfm4001", "pfm1001", "pfm1002", "pfm1003"};
    error = houserail_train_consist ("train1", cars, 4);
    int passed =
    assert (error == 0, "houserail_train_consist (train1) status") &&
    trainlist ("After houserail_train_consist (train1)");
    digest (passed, "houserail_train_consist (train1)");
    if (error) printf ("   error: %s\n", error);


    // Test const char *houserail_train_enter (const char *id,
    //                                         const char *facing, int orientation);

    starting ("houserail_train_enter(train1)");
    error = houserail_train_enter ("train1", "reed-2", 1);
    passed =
    assert (error == 0, "houserail_train_enter (train1) status") &&
    trainlist ("After houserail_train_enter (train1)");
    digest (passed, "houserail_train_enter (train1)");
    if (error) printf ("   error: %s\n", error);

    // Test const char *houserail_train_park (const char *id);

    starting ("houserail_train_park(train1)");
    error = houserail_train_park ("train1");
    passed =
    assert (error == 0, "houserail_train_park (train1) status") &&
    trainlist ("After houserail_train_park (train1)");
    digest (passed, "houserail_train_park (train1)");
    if (error) printf ("   error: %s\n", error);

    error = houserail_train_park ("train1");
    passed =
    assert (error == 0, "2nd houserail_train_park (train1) status");

    // Test const char *houserail_train_delete (const char *id);

    starting ("houserail_train_delete()");
    error = houserail_train_delete ("train2");
    passed =
    assert (error != 0, "houserail_train_delete (train2) status");

    char buffer[16000];
    error = houserail_train_delete ("train1");
    passed =
    assert (error == 0, "houserail_train_delete (train1) status") &&
    assert (houserail_train_status (buffer, sizeof(buffer)) == 0,
            "houserail_train_delete (train1) train list");
    digest (passed, "houserail_train_delete (train1)");
    if (error) printf ("   error: %s\n", error);

    error = houserail_train_delete ("train1");
    passed =
    assert (error != 0, "2nd houserail_train_delete (train1) status");

    // Test void houserail_train_track (const struct TrackRange *area,
    //                                  int occupied,
    //                                  long long timestamp);

    // Create a new train and set it moving.
    starting ("Prepare for houserail_train_track() test");
    error = houserail_train_consist ("train3", cars, 4);
    if (!assert (error == 0, "houserail_train_consist(train3) return")) {
        printf ("   error: %s\n", error);
        goto canceltest;
    }
    error = houserail_train_enter ("train3", "reed-2", 1);
    if (!assert (error == 0, "houserail_train_enter(train3, reed-2) return")) {
        printf ("   error: %s\n", error);
        goto canceltest;
    }
    error = houserail_train_move ("train3", 0, 0);
    if (!assert (error == 0, "houserail_train_move(train3) return")) {
        printf ("   error: %s\n", error);
        goto canceltest;
    }
    if (!assert (LastSpeedOrder == 30, "houserail_train_move (train3) speed")) {
        goto canceltest;
    }
    trainlist ("Before houserail_train_track (reed-2 occupied)");

    starting ("houserail_train_track (reed-2 occupied)");
    struct TrackRange detected;
    detected.line = "main";
    detected.low = 29; // reed-2
    detected.high = 31;
    houserail_train_track (&detected, 1, now());
    const struct TrackLocation *after = houserail_train_head ("train3");
    passed =
    assert ((after != 0) && (after->post == 31), "houserail_train_track (reed-2 occupied) head");
    digest (passed, "houserail_train_track (reed-2 occupied)");
    if ((!passed) && (after != 0)) printf ("   Head at %s %d\n", after->line, after->post);

    trainlist ("After houserail_train_track (reed-2 occupied)");

    starting ("houserail_train_track (reed-1 occupied)");
    detected.low = 9; // reed-1
    detected.high = 11;
    houserail_train_track (&detected, 1, now());
    after = houserail_train_head ("train3");
    passed =
    assert ((after != 0) && (after->post == 33), "houserail_train_track (reed-1 occupied) head");
    digest (passed, "houserail_train_track (reed-1 occupied)");
    if ((!passed) && (after != 0)) printf ("   Head at %s %d\n", after->line, after->post);

    trainlist ("After houserail_train_track (reed-1 occupied)");

    starting ("houserail_train_track (reed-1 vacant)");
    houserail_train_track (&detected, 0, now());
    after = houserail_train_head ("train3");
    passed =
    assert ((after != 0) && (after->post == 37), "houserail_train_track (reed-1 vacant) head");
    digest (passed, "houserail_train_track (reed-1 vacant)");
    if ((!passed) && (after != 0)) printf ("   Head at %s %d\n", after->line, after->post);

    trainlist ("After houserail_train_track (reed-1 vacant)");

    starting ("houserail_train_track (reed-3 occupied)");
    detected.low = 49; // reed-3
    detected.high = 51;
    houserail_train_track (&detected, 1, now());
    after = houserail_train_head ("train3");
    passed =
    assert ((after != 0) && (after->post == 51), "houserail_train_track (reed-3 occupied) head");
    digest (passed, "houserail_train_track (reed-3 occupied)");
    if ((!passed) && (after != 0)) printf ("   Head at %s %d\n", after->line, after->post);

    trainlist ("After houserail_train_track (reed-3 occupied)");

    // Test void houserail_train_stop (const char *id, int emergency);

    starting ("houserail_train_stop (train3)");
    error = houserail_train_stop ("train3", 0);
    passed =
    assert (error == 0, "houserail_train_stop (train3) status") &&
    assert (LastSpeedOrder == 0, "houserail_train_stop (train3) speed");
    digest (passed, "houserail_train_stop (train3)");

canceltest:
    return summary ("houserail_train.c");
}

