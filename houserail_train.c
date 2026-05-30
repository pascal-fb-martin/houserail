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
 * houserail_train.c - Train management (attributes, tracking, etc.)
 *
 * SYNOPSYS:
 *
 * const char *houserail_train_initialize (int argc, const char **argv);
 *
 *     Initialize this module.
 *
 * void houserail_train_track (const char *line, int lowpost, int highpost,
 *                             const char *segment,
 *                             int occupied, long long timestamp);
 *
 *     Update the location of trains based on track occupancy.
 *
 * const char *houserail_train_move (const char *id, const char *to, int slow);
 * const char *houserail_train_stop (const char *id, int emergency);
 *
 *     Control the movements of one train.
 *
 * const char *houserail_train_enter (const char *id,
 *                                    const char *facing, int orientation);
 *
 *     Place a train on the layout, facing a detector point.
 *
 * const char *houserail_train_park (const char *id);
 *
 *     Park a train, i.e. remove it from the layout but keep it as a consist
 *     in the database.
 *
 * const char *houserail_train_delete (const char *id);
 *
 *     Delete a train, i.e. remove it from the consist database. All vehicles
 *     listed in the consist are disconnected from that consist.
 *
 * const char *houserail_train_reload (void);
 *
 *     Import a new configuration.
 *
 * int houserail_train_export (char *buffer, int size, const char *separator);
 *
 *     Export the current configuration in JSON format.
 *
 * int houserail_train_status (char *buffer, int size);
 *
 *     Return the live status of trains in JSON format.
 *
 * void houserail_train_background (time_t now);
 *
 *     Periodic update function.
 */

#include <time.h>
#include <stdio.h>
#include <string.h>

#include <echttp.h>
#include <echttp_hash.h>

#include "houserail_fleet.h"
#include "houserail_train.h"
#include "houserail_track.h"

#define DEBUG if (echttp_isdebug()) printf

static FleetListener *TrainNextFleetListener = 0;
static DetectionListener *TrainNextDetectionListener = 0;

static int TrainRestrictedSpeed = 3; // TBD: make it configurable.

#define TRAINMAXCARS 16 // FIXME: arbitrary limit.
#define TRAINMAXSPOT 4  // FIXME: arbitrary limit.

#define TRAINMAXDISTANCE 4 // FIXME: make it configurable?

struct CarModel {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;
    int spots[TRAINMAXSPOT];
    int count;
};

// This holds the permanent properties of a owned vehicle
struct CarVehicle {
    char id[10];
    unsigned int signature; // Seach accelerator.

    char model[10];

    // The following is learned from the data reported by HouseDCC.
    int hasdcc;

    int consist; // -1 if the vehicle is not listed in a train consist.
};

// This holds the properties of a vehicle as part of a train consist.
struct TrainCar {
    char id[10];
    unsigned int signature; // Seach accelerator.

    int vehicle; // Reference the array of vehicles.

    int head;
    int tail;
    struct TrackLocation spots[TRAINMAXSPOT]; // From head to tail.
    int count;
};

// This holds the properties of a complete train.
struct TrainConsist {
    char id[10];
    unsigned int signature; // Seach accelerator.

    int parked;         // Not currently on this layout.
    struct TrackLocation head;
    struct TrackLocation tail;
    struct TrainCar      cars[TRAINMAXCARS]; // From head to tail.
    int                  count;
    long long updated;

    int orientation;    // -1: decreasing, 1: increasing, 0: unknown.
    int speed;          // Reported by HouseDCC.
    int active;         // 1 if reported by HouseDCC.
};

static struct CarModel *LayoutCarModels = 0;
static int              LayoutCarModelsCount = 0;

static struct CarVehicle *LayoutVehicles = 0;
static int                LayoutVehiclesCount = 0;

static struct TrainConsist *LayoutTrains = 0;
static int                  LayoutTrainsCount = 0;

static struct TrainConsist *houserail_train_search (const char *id) {

    unsigned int signature = echttp_hash_signature (id);
    int i;
    for (i = LayoutTrainsCount - 1; i >= 0; --i) {
        if (LayoutTrains[i].signature != signature) continue; // Fast filter

        struct TrainConsist *train = LayoutTrains + i;
        if (strcmp (id, train->id)) continue;
        return train;
    }
    return 0;
}

static void houserail_train_fleet (const char *id, int index) {

    int speed = houserail_fleet_speed (index);
    DEBUG (__FILE__ ": received fleet update for %s, speed = %d\n", id, speed);

    unsigned int signature = echttp_hash_signature (id);
    int i;
    for (i = LayoutVehiclesCount - 1; i >= 0; --i) {
        if (LayoutVehicles[i].signature != signature) continue; // Fast filter

        struct CarVehicle *vehicle = LayoutVehicles + i;
        if (strcmp (id, vehicle->id)) continue;

        vehicle->hasdcc = 1; // Now we can control it.
        if (vehicle->consist >= 0) {
            struct TrainConsist *train = LayoutTrains + vehicle->consist;
            train->speed = speed;
            train->active = 1;
        }
        break;
    }
    if (i < 0) DEBUG (__FILE__ ": %s not found\n", id);

    if (TrainNextFleetListener) TrainNextFleetListener (id, index);
}

static int houserail_train_direction (struct TrainConsist *train) {
    return train->orientation * ((train->speed < 0)?-1:1);
}

static int houserail_train_covers (struct TrainConsist *train,
                                   const char *segment, int low, int high) {

    if (!train->head.segment) return 0;
    if (!train->tail.segment) return 0;
    return houserail_track_covered (segment, low, high,
                                    train->tail.segment, train->tail.post,
                                    train->head.segment, train->head.post);
}

static int houserail_train_distance (struct TrainConsist *train,
                                     const char *segment,
                                     int lowpost, int highpost, int max,
                                     int occupied) {

    int direction = houserail_train_direction (train);

    int post;
    if (direction >= 0) post = occupied?lowpost:highpost;
    else if (direction < 0) post = occupied?highpost:lowpost;

    // Depending on the train speed sign, choose the first or last detection
    // spot of the consist.
    struct TrackLocation *lead;
    if (train->speed >= 0) {
        lead = &(train->cars[0].spots[0]); // First spot.
    } else {
        struct TrainCar *car = train->cars + train->count - 1;
        lead = &(car->spots[car->count-1]); // Last spot.
    }

    int distance = houserail_track_distance (lead->segment, lead->post,
                                             segment, post, max);
    distance *= direction;
    if (distance < 0) return -1; // Not the direction of travel
    return distance;
}

static void houserail_train_pull (struct TrainConsist *train,
                                  int distance,
                                  long long timestamp) {

    // Move the head, tail and each car spot of the consist.
    int direction = houserail_train_direction (train);

    houserail_track_move (&(train->head), distance, direction);
    int i;
    for (i = train->count - 1; i >= 0; --i) {
        struct TrainCar *car = train->cars + i;
        int j;
        for (j = car->count - 1; j >= 0; --j) {
            houserail_track_move (car->spots+j, distance, direction);
        }
    }
    houserail_track_move (&(train->tail), distance, direction);
    train->updated = timestamp;
}

static void houserail_train_pull_occupied (struct TrainConsist *train,
                                           const char *segment,
                                           int lowpost, int highpost,
                                           long long timestamp) {

    // Find the car spot closest to the location and moving toward it,
    // then move the train by the distance found.

    int direction = houserail_train_direction (train);

    int post = (direction >= 0) ? lowpost : highpost;

    int min = TRAINMAXDISTANCE;
    int found = 0;
    int i;
    for (i = 0; i < train->count; ++i) {
        int j;
        struct TrainCar *car = train->cars + i;
        for (j = car->count - 1; j >= 0; --j) {
            int distance;
            distance = houserail_track_distance (car->spots[j].segment,
                                                 car->spots[j].post,
                                                 segment, post, min);
            distance *= direction;
            if (distance < 0) continue; // Not the direction of travel
            if ((distance > 0) && (distance < min)) {
                min = distance;
                found = 1;
            }
        }
    }
    if (!found) return;

    houserail_train_pull (train, min, timestamp);
}

static void houserail_train_pull_vacant (struct TrainConsist *train,
                                         const char *segment,
                                         int lowpost, int highpost,
                                         long long timestamp) {
    // Consider the last detectable spot that could have been within
    // the detector's range.. This requires iterating until no spot remain
    // within range.
    // TBD.
}

void houserail_train_track (const char *line, int lowpost, int highpost,
                            const char *segment,
                            int occupied, long long timestamp) {

    DEBUG (__FILE__ ": received update for on segment %s, %soccupied\n", segment, occupied?"":"not ");

    // Find the active train located on this segment, or leading to it.
    // This is looking for either a train already covering the designated
    // location, or else the nearest train travelling toward the designed
    // location.
    // FIXME: a loop over all active train may become inefficient when
    // there are lots of trains. If this becomes a problem, this might
    // need an index of trains on each track segment (a per track segment
    // link to a list of trains).
    // NOTE: if the detector has a wide range, multiple trains may cover it.
    // For now, assume that only one train may cover a detector's location.

    int i;
    for (i = 0; i < LayoutTrainsCount; ++i) {
        struct TrainConsist *train = LayoutTrains + i;
        if (houserail_train_covers (train, segment, lowpost, highpost)) {
            if (occupied)
               houserail_train_pull_occupied
                  (train, segment, lowpost, highpost, timestamp);
            else
               houserail_train_pull_vacant
                  (train, segment, lowpost, highpost, timestamp);
            return; // Assume only one train within range.
        }
    }
    if (!occupied)
        return; // If no train was within range, why was it occupied??

    // Find the nearest train heading toward the designed location.
    // Limit the search to a maximum distance. Each time we found a train,
    // that train's distance become the new limit: it would make no sense
    // to search for a farther train.
    int min = TRAINMAXDISTANCE;
    int closest = -1;
    int distance;
    struct TrainConsist *train;
    for (i = 0; i < LayoutTrainsCount; ++i) {
        train = LayoutTrains + i;
        if (train->speed == 0) continue;
        distance = houserail_train_distance (train, segment, lowpost, highpost, min, occupied);
        if ((distance > 0) && (distance < min)) {
           min = distance;
           closest = i;
        }
    }
    if (closest < 0) return; // TBD: new train detected?

    // The closest train was identified. Its head must be moved to that
    // detector's location.
    train = LayoutTrains + closest;

    // If the actual orientation of the train is not known yet, deduct it
    // from the observed movement.
    if (train->orientation == 0) {
        if ((train->speed > 0) && (highpost < train->tail.post)) {
            // Moving forward in the backward direction: reverse orientation.
            train->orientation = -1;
        } else if ((train->speed < 0) && (lowpost > train->head.post)) {
            // Moving backward in the forward direction: reverse orientation.
            train->orientation = -1;
        } else {
            train->orientation = 1;
        }
        if (train->orientation < 0) {
            // Reverse the train orientation. The default consist assumed
            // that the orientation was going to be positive.
            // TBD.
        }
    }
    houserail_train_pull (train, min, timestamp);
}

const char *houserail_train_initialize (int argc, const char **argv) {

    TrainNextFleetListener = houserail_fleet_subscribe (houserail_train_fleet);
    TrainNextDetectionListener = houserail_track_subscribe (houserail_train_track);
    return 0;
}

const char *houserail_train_move (const char *id, const char *to, int slow) {
    // TBD: set normal speed according to upcoming track speed limit.
    int speed = slow?TrainRestrictedSpeed:5;
    return houserail_fleet_move (id, speed);
}

const char *houserail_train_stop (const char *id, int emergency) {
    return houserail_fleet_stop (id, emergency);
}

const char *houserail_train_park (const char *id) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "unknown train";

    train->parked = 1;
    return houserail_train_stop (id, 1);
}

const char *houserail_train_enter (const char *id,
                                   const char *facing, int orientation) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "unknown train";

    // TBD
    return 0;
}

const char *houserail_train_delete (const char *id) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "unknown train";

    // TBD
    return 0;
}

const char *houserail_train_reload (void) {
   // TBD
   LayoutCarModelsCount = 0;
   LayoutCarModels = 0;
   return "Not yet implemented";
}

int houserail_train_export (char *buffer, int size, const char *separator) {
    return 0; // TBD
}

int houserail_train_status (char *buffer, int size) {
    return 0; // TBD
}


void houserail_train_background (time_t now) {
    // TBD
}

