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
 * void houserail_train_track (const struct TrackRange *area,
 *                             int occupied,
 *                             long long timestamp);
 *
 *     Update the location of trains based on track occupancy.
 *
 * const char *houserail_train_move (const char *id, const char *dir, int slow);
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
#include <stdlib.h>
#include <string.h>

#include <echttp.h>
#include <echttp_hash.h>

#include "houserail_fleet.h"
#include "houserail_track.h"
#include "houserail_path.h"
#include "houserail_train.h"

#define DEBUG if (echttp_isdebug()) printf

static FleetListener *TrainNextFleetListener = 0;
static DetectionListener *TrainNextDetectionListener = 0;

static int TrainRestrictedSpeed = 3; // FIXME: make it configurable.

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

    struct TrackPath     path; // The sections of track the train is on.

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

static int houserail_train_maxspeed (struct TrainConsist *train) {

    int direction = houserail_train_direction (train);
    if (!direction) return TrainRestrictedSpeed; // In doubt, go slow.

    // Set the max speed according to the track speed limit of the tracks
    // below the train and the track the train is approaching to.

    int speed = houserail_track_civil (&(train->head), direction);
    int speed2 = houserail_track_civil (&(train->tail), direction);
    if (speed2 < speed) speed = speed2;

    int i;
    for (i = 0; i < train->count; ++i) {
       int count = train->cars[i].count;
       int j;
       for (j = 0; j < count; ++j) {
           speed2 = houserail_track_civil (&(train->cars[i].spots[j]), 0);
           if (speed2 < speed) speed = speed2;
       }
    }
    if (direction != train->orientation) speed = 0 - speed;
    return speed;
}

static int houserail_train_covers (struct TrainConsist *train,
                                   const struct TrackRange *area) {

    if (!train->head.segment) return 0;
    if (!train->tail.segment) return 0;
    if (train->path.count <= 0) return 0;
    return houserail_path_covers (&(train->path), area);
}

static int houserail_train_spotdistance (struct TrackLocation *spot,
                                         const struct TrackRange *area,
                                         int max, int direction, int occupied) {

    struct TrackLocation point;
    point.segment = area->segment;
    point.line = area->line;
    if (direction >= 0) point.post = occupied?area->low:area->high;
    else if (direction < 0) point.post = occupied?area->high:area->low;

    int distance = houserail_track_distance (spot, &point, direction, max);
    return distance;
}

static int houserail_train_distance (struct TrainConsist *train,
                                     const struct TrackRange *area,
                                     int max, int occupied) {

    int direction = houserail_train_direction (train);

    // Depending on the train speed sign, choose the first or last detection
    // spot of the consist.
    struct TrackLocation *lead;
    if (train->speed >= 0) {
        lead = &(train->cars[0].spots[0]); // First spot.
    } else {
        struct TrainCar *car = train->cars + train->count - 1;
        lead = &(car->spots[car->count-1]); // Last spot.
    }

    return houserail_train_spotdistance
               (lead, area, max, direction, occupied);
}

static void houserail_train_pull (struct TrainConsist *train,
                                  int distance,
                                  long long timestamp) {

    // Move the head, tail and each car spot of the consist.
    int direction = houserail_train_direction (train);

    // Extend the path first, then move each train point along
    // the extended path, and then rollup the path. This way
    // the code walks the track only once.

    houserail_path_lengthen (&(train->path), distance, direction);

    houserail_path_move (&(train->path), &(train->head), distance, direction);
    int i;
    for (i = train->count - 1; i >= 0; --i) {
        struct TrainCar *car = train->cars + i;
        int j;
        for (j = car->count - 1; j >= 0; --j) {
            houserail_path_move (&(train->path), car->spots+j, distance, direction);
        }
    }
    houserail_path_move (&(train->path), &(train->tail), distance, direction);

    houserail_path_rollup (&(train->path), &(train->tail), direction);
    train->updated = timestamp;
}

static void houserail_train_pull_occupied (struct TrainConsist *train,
                                           const struct TrackRange *area,
                                           long long timestamp) {

    // Find the car spot closest to the location and moving toward it,
    // then move the train by the distance found.

    int direction = houserail_train_direction (train);

    struct TrackLocation point;
    point.segment = area->segment;
    point.line = area->line;
    point.post = (direction >= 0) ? area->low : area->high;

    int min = TRAINMAXDISTANCE;
    int found = 0;
    int i;
    for (i = 0; i < train->count; ++i) {
        int j;
        struct TrainCar *car = train->cars + i;
        for (j = car->count - 1; j >= 0; --j) {
            int distance;
            distance = houserail_track_distance (car->spots+j, &point, direction, min);
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
                                         const struct TrackRange *area,
                                         long long timestamp) {

    // Consider the last detectable spot that was within the detector's
    // range.. This requires iterating until no spot remain within range.

    int direction = houserail_train_direction (train);

    struct {
        int car;
        int spot;
    } first, last;
    first.spot = -1;

    int i;
    for (i = 0; i < train->count; ++i) {
        int j;
        struct TrainCar *car = train->cars + i;
        for (j = 0; j < car->count; ++j) {
            int post = car->spots[j].post;
            if ((post < area->low) || (post > area->high)) continue;
            if (strcmp (area->segment, car->spots[j].segment)) continue;
            if (first.spot < 0) {
                first.car = i;
                first.spot = j;
            }
            last.car = i;
            last.spot = j;
        }
    }
    if (first.spot < 0) return; // No more spot over the detector.

    if (direction < 0) last = first;

    // Move the train so that the last spot exits the detector's range.
    int distance = houserail_train_spotdistance
                       (&(train->cars[last.car].spots[last.spot]),
                        area, TRAINMAXDISTANCE, direction, 0);
    if (distance < 0) return;
    houserail_train_pull (train, distance, timestamp);

    // Iterate using tail recursion.
    houserail_train_pull_vacant (train, area, timestamp);
}

void houserail_train_track (const struct TrackRange *area,
                            int occupied,
                            long long timestamp) {

    DEBUG (__FILE__ ": received update for on segment %s, %soccupied\n", area->segment, occupied?"":"not ");

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
        if (houserail_train_covers (train, area)) {
            if (occupied)
               houserail_train_pull_occupied (train, area, timestamp);
            else
               houserail_train_pull_vacant (train, area, timestamp);

            // Adjust the train speed based on its new location.
            if (abs(train->speed) > TrainRestrictedSpeed)
               houserail_fleet_move (train->id,
                                     houserail_train_maxspeed (train));

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
        distance = houserail_train_distance (train, area, min, occupied);
        if ((distance > 0) && (distance < min)) {
           min = distance;
           closest = i;
        }
    }
    if (closest < 0) return; // FIXME: new train detected?

    // The closest train was identified. Its head must be moved to that
    // detector's location.
    train = LayoutTrains + closest;

    // If the actual orientation of the train is not known yet, deduct it
    // from the observed movement.
    if (train->orientation == 0) {
        if ((train->speed > 0) && (area->high < train->tail.post)) {
            // Moving forward in the backward direction: reverse orientation.
            train->orientation = -1;
        } else if ((train->speed < 0) && (area->low > train->head.post)) {
            // Moving backward in the forward direction: reverse orientation.
            train->orientation = -1;
        } else {
            train->orientation = 1;
        }
        if (train->orientation < 0) {
            // Reverse the train orientation. The default consist assumed
            // that the orientation was going to be positive.
            houserail_path_reverse (&(train->path));
            // TBD: reverse head and tail, detectors.
        }
    }
    houserail_train_pull (train, min, timestamp);

    // Adjust the train speed based on its new location.
    if (abs(train->speed) > TrainRestrictedSpeed)
       houserail_fleet_move (train->id,
                             houserail_train_maxspeed (train));
}

const char *houserail_train_initialize (int argc, const char **argv) {

    TrainNextFleetListener = houserail_fleet_subscribe (houserail_train_fleet);
    TrainNextDetectionListener = houserail_track_subscribe (houserail_train_track);
    return 0;
}

const char *houserail_train_move (const char *id, const char *dir, int slow) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "invalid train ID";

    // Set normal speed according to the track speed limit of the tracks
    // below the train and the track the train is approaching to.
    int speed = TrainRestrictedSpeed;
    if (!slow) {
        speed = houserail_train_maxspeed (train);
    }
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

    // TBD: create new train or activate an existing train
    return 0;
}

const char *houserail_train_delete (const char *id) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "unknown train";

    // TBD: delete a train entry.
    return 0;
}

const char *houserail_train_reload (void) {
   // TBD: reload the list of trains from the saved status, keep known locations.
   LayoutCarModelsCount = 0;
   LayoutCarModels = 0;
   return "Not yet implemented";
}

int houserail_train_export (char *buffer, int size, const char *separator) {
    return 0; // TBD: save train state in JSON.
}

int houserail_train_status (char *buffer, int size) {
    return 0; // TBD: export train status in JSON.
}


void houserail_train_background (time_t now) {
    // TBD: background work needed?
}

