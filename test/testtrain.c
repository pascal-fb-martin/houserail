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

int houserail_field_fleet_iterate   (FleetListener *listener) {
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
    int passed = (size > 0);
    if (label) assert (passed, label);
    return passed;
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    // houserail_track_testmode (1);

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

    // Test void houserail_train_tracking (const struct TrackRange *area,
    //                                     int occupied,
    //                                     long long timestamp);

    // Create a new train and set it moving.
    starting ("Prepare for houserail_train_tracking() test");
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
        printf ("   speed: %d\n", LastSpeedOrder);
        goto canceltest;
    }
    trainlist ("Before houserail_train_tracking (reed-2 occupied)");

    starting ("houserail_train_tracking (reed-1 occupied) -- no move");
    struct TrackRange detected;
    detected.line = "main";
    detected.low = 9; // reed-1
    detected.high = 11;
    houserail_train_tracking (&detected, 1, now());
    const struct TrackLocation *head = houserail_train_head ("train3");
    passed =
    assert ((head != 0) && (head->post == 28), "houserail_train_tracking (reed-1 occupied) head");
    digest (passed, "houserail_train_tracking (reed-1 occupied)");
    if ((!passed) && (head != 0)) printf ("   Head at %s %d\n", head->line, head->post);

    trainlist ("After houserail_train_tracking (reed-1 occupied)");

    starting ("houserail_train_tracking (reed-2 occupied)");
    detected.line = "main";
    detected.low = 29; // reed-2
    detected.high = 31;
    houserail_train_tracking (&detected, 1, now());
    head = houserail_train_head ("train3");
    passed =
    assert ((head != 0) && (head->post == 31), "houserail_train_tracking (reed-2 occupied) head");
    digest (passed, "houserail_train_tracking (reed-2 occupied)");
    if ((!passed) && (head != 0)) printf ("   Head at %s %d\n", head->line, head->post);

    trainlist ("After houserail_train_tracking (reed-2 occupied)");

    // houserail_track_testmode (1);
    // houserail_train_testmode (1);
    starting ("houserail_train_tracking (reed-1 occupied) -- triggerred by middle spot");
    detected.low = 9; // reed-1
    detected.high = 11;
    houserail_train_tracking (&detected, 1, now());
    head = houserail_train_head ("train3");
    passed =
    assert ((head != 0) && (head->post == 33), "houserail_train_tracking (reed-1 occupied) head");
    digest (passed, "houserail_train_tracking (reed-1 occupied)");
    if ((!passed) && (head != 0)) printf ("   Head at %s %d\n", head->line, head->post);

    trainlist ("After houserail_train_tracking (reed-1 occupied)");

    starting ("houserail_train_tracking (reed-1 vacant)");
    houserail_train_tracking (&detected, 0, now());
    head = houserail_train_head ("train3");
    passed =
    assert ((head != 0) && (head->post == 37), "houserail_train_tracking (reed-1 vacant) head");
    digest (passed, "houserail_train_tracking (reed-1 vacant)");
    if ((!passed) && (head != 0)) printf ("   Head at %s %d\n", head->line, head->post);

    trainlist ("After houserail_train_tracking (reed-1 vacant)");

    starting ("houserail_train_tracking (reed-3 occupied)");
    detected.low = 49; // reed-3
    detected.high = 51;
    houserail_train_tracking (&detected, 1, now());
    head = houserail_train_head ("train3");
    passed =
    assert ((head != 0) && (head->post == 51), "houserail_train_tracking (reed-3 occupied) head");
    digest (passed, "houserail_train_tracking (reed-3 occupied)");
    if ((!passed) && (head != 0)) printf ("   Head at %s %d\n", head->line, head->post);

    trainlist ("After houserail_train_tracking (reed-3 occupied)");

    // Test void houserail_train_stop (const char *id, int emergency);

    starting ("houserail_train_stop (train3)");
    error = houserail_train_stop ("train3", 0);
    passed =
    assert (error == 0, "houserail_train_stop (train3) status") &&
    assert (LastSpeedOrder == 0, "houserail_train_stop (train3) speed");
    digest (passed, "houserail_train_stop (train3)");

    // Test train tracking through switches

    starting ("preparing for the up trip");
    houserail_train_stop ("train3", 0);
    houserail_train_park ("train3");
    houserail_train_delete ("train3");

    houserail_train_consist ("train3", cars, 4);
    houserail_train_enter ("train3", "reed-15", 1);
    houserail_train_move ("train3", 0, 0);

    error = houserail_track_switch ("main-1", "reverse");
    assert (error == 0, "houserail_track_switch (main-1 reverse) status");
    error = houserail_track_switch ("main-4", "reverse");
    assert (error == 0, "houserail_track_switch (main-4 reverse) status");
    trainlist ("after preparing for the up trip");

    struct TrackLocation detector;
    const char *uptrip[] = {"reed-15", "reed-16", "reed-17", "reed-18", "reed-19", "reed-20", "reed-21", "reed-22", "reed-5", "reed-6", "reed-7", "reed-8", 0};

    // houserail_track_testmode (1);
    // houserail_train_testmode (1);
    int original = Errors;
    head = houserail_train_head ("train3");
    int step;
    for (step = 0; uptrip[step] != 0; ++step) {
        char action[128];
        char message[256];
        snprintf (action, sizeof(action),
                  "houserail_train_tracking (%s occupied)", uptrip[step]);

        starting (action);
        houserail_track_vicinity (&detector, uptrip[step], 1);
        detected.line = detector.line;
        detected.low = detector.post;
        detected.high = detected.low + 2;
        printf ("   Train train3 moving from %s %d to %s %d\n",
                head->line, head->post, detector.line, detector.post);

        houserail_train_tracking (&detected, 1, now());
        trainlist (0);
        head = houserail_train_head ("train3");
        snprintf (message, sizeof(message), "%s head", action);
        assert ((head != 0) && (head->post == detected.low+2), message);
    }
    houserail_train_stop ("train3", 0);
    houserail_train_park ("train3");
    houserail_train_delete ("train3");
    digest (Errors == original, "moving up through the switches");

    starting ("preparing for the down trip");

    houserail_train_consist ("train3", cars, 4);
    houserail_train_enter ("train3", "reed-8", -1);
    houserail_train_move ("train3", "forward", 0);
    trainlist ("after preparing for the down trip");

    starting ("moving down through the switches");
    const char *downtrip[] = {"reed-8", "reed-7", "reed-6", "reed-5", "reed-22", "reed-21", "reed-20", "reed-19", "reed-18", "reed-17", "reed-16", "reed-15", 0};

    // houserail_track_testmode (1);
    // houserail_train_testmode (1);
    original = Errors;
    head = houserail_train_head ("train3");
    for (step = 0; downtrip[step] != 0; ++step) {
        char action[128];
        char message[256];
        snprintf (action, sizeof(action),
                  "houserail_train_tracking (%s occupied)", downtrip[step]);

        starting (action);
        houserail_track_vicinity (&detector, downtrip[step], -1);
        detected.line = detector.line;
        detected.high = detector.post;
        detected.low = detected.high - 2;
        printf ("   Train train3 moving from %s %d to %s %d\n",
                head->line, head->post, detector.line, detector.post);

        houserail_train_tracking (&detected, 1, now());
        trainlist (0);
        head = houserail_train_head ("train3");
        snprintf (message, sizeof(message), "%s head", action);
        assert ((head != 0) && (head->post == detected.high-2), message);
    }
    houserail_train_stop ("train3", 0);
    houserail_train_park ("train3");
    houserail_train_delete ("train3");
    digest (Errors == original, "moving down through the switches");

    starting ("preparing for a trip against a switch");

    houserail_train_consist ("train3", cars, 4);
    houserail_train_enter ("train3", "reed-2", 1);
    houserail_train_move ("train3", "forward", 0);
    trainlist ("after preparing for a trip against a switch");

    starting ("moving against a switch");
    const char *againsttrip[] = {"reed-2", "reed-3", 0};

    // houserail_track_testmode (1);
    // houserail_train_testmode (1);
    original = Errors;
    head = houserail_train_head ("train3");
    for (step = 0; againsttrip[step] != 0; ++step) {
        char action[128];
        char message[256];
        snprintf (action, sizeof(action),
                  "houserail_train_tracking (%s occupied)", againsttrip[step]);

        starting (action);
        houserail_track_vicinity (&detector, againsttrip[step], -1);
        detected.line = detector.line;
        detected.low = detector.post;
        detected.high = detected.low + 2;
        printf ("   Train train3 moving from %s %d to %s %d\n",
                head->line, head->post, detector.line, detector.post);

        houserail_train_tracking (&detected, 1, now());
        trainlist (0);
        head = houserail_train_head ("train3");
        snprintf (message, sizeof(message), "%s head", action);
        assert ((head != 0) && (head->post == detected.low+2), message);
    }
    assert (LastSpeedOrder == 0, "Stopped at reed-3 before the switch");
    digest (Errors == original, "moving against a switch");

    return summary ("testtrain");

canceltest:
    trainlist ("after test cancelled");
    return summary ("testtrain cancelled");
}

