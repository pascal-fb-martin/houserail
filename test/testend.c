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
 * testend.c - test train speed management when coming to the end of the line.
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * testend
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
    int passed = (size > 0);
    if (label) assert (passed, label);
    return passed;
}

int main (int argc, const char **argv) {

    // Initialize the track module.

    // houserail_track_testmode (1);

    starting ("Loading layout configuration");
    houseconfig_default ("--config=../pgmtrack.json");
    houserail_track_initialize (argc, argv);
    houserail_train_initialize (argc, argv);
    const char *error = houseconfig_initialize ("testend", test_update, argc, argv);
    if (error) {
        printf ("** Config error: %s\n", error);
        return Errors + 1;
    }
    if (TestListener) TestListener ("PFM4001", 1);

    // Create a new train and set it moving upward.

    const char *cars[] = {"PFM4001", "PFM1001"};

    starting ("preparing for the up trip");
    error = houserail_train_consist ("train3", cars, 2);
    if (error) {
        printf ("** Consist error: %s\n", error);
        return Errors + 1;
    }
    error = houserail_train_enter ("train3", "main.5", 1);
    if (error) {
        printf ("** Consist error: %s\n", error);
        return Errors + 1;
    }
    // houserail_track_testmode (1);
    // houserail_train_testmode (1);
    houserail_train_move ("train3", 0, 0);
    // houserail_track_testmode (0);
    // houserail_train_testmode (0);
    trainlist (0);

    starting ("moving up to the end of track");
    struct TrackLocation detector;
    const char *uptrip[] = {"main.5", "main.6", "main.7", "main.8", 0};

    // houserail_track_testmode (1);
    // houserail_train_testmode (1);
    int original = Errors;
    const struct TrackLocation *head = houserail_train_head ("train3");
    int step;
    struct TrackRange detected;
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
        assert ((head != 0) && (head->post == detected.low+4), message);
        int expected = 50;
        if (!strcmp (uptrip[step], "main.6")) expected = 10;
        else if (!strcmp (uptrip[step], "main.7")) expected = 10;
        else if (!strcmp (uptrip[step], "main.8")) expected = 0;
        snprintf (message, sizeof(message), "%s speed", action);
        assert (LastSpeedOrder == expected, message);
    }
    houserail_train_stop ("train3", 0);
    houserail_train_park ("train3");
    houserail_train_delete ("train3");

    digest (Errors == original, "moving up");

    starting ("preparing for the down trip");

    houserail_train_consist ("train3", cars, 2);
    houserail_train_enter ("train3", "main.4", -1);
    houserail_train_move ("train3", "forward", 0);
    trainlist ("after preparing for the down trip");

    starting ("moving down to the end of track");
    const char *downtrip[] = {"main.4", "main.3", "main.2", "main.1", 0};

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
        assert ((head != 0) && (head->post == detected.high-4), message);
        int expected = 50;
        if (!strcmp (downtrip[step], "main.2")) expected = 10;
        else if (!strcmp (downtrip[step], "main.1")) expected = 0;
        snprintf (message, sizeof(message), "%s speed", action);
        assert (LastSpeedOrder == expected, message);
    }
    digest (Errors == original, "moving down through the switches");
    return summary ("houserail_train.c");
}

