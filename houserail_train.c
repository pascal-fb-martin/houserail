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
 * This module manages vehicles and train. A car is a vehicle that is part
 * of a train consist.
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
 * const char *houserail_train_consist (const char *id,
 *                                      const char *cars[], int count);
 *
 *     Set (or change) a train consist. This creates a new train if none
 *     existed; the new train is made parked. if the train exists, it must
 *     be parked.
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
 * int houserail_train_locate (char *buffer, int size);
 *
 *     Return a subset of the live status of trains in JSON format.
 *     That subset represents enough information to show the train ID at
 *     the right location on a track display.
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
#include <echttp_libc.h>
#include <echttp_hash.h>

#include "houserail_field.h"
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

struct VehicleModel {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;
    int spots[TRAINMAXSPOT];
    int count;
};

// This holds the permanent properties of a owned vehicle
struct Vehicle {
    char id[10];
    unsigned int signature; // Seach accelerator.

    int index;   // Self reference.
    int model;   // Reference the array of vehicle models

    // The following is learned from the data reported by HouseDCC.
    int hasdcc;

    int consist; // -1 if the vehicle is not listed in a train consist.

    // This holds the properties of a vehicle as part of a train consist.
    int head;
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

    int cars[TRAINMAXCARS]; // From head to tail.
    int count;

    struct TrackPath     path; // The sections of track the train is on.

    long long updated;
    int orientation;    // -1: decreasing, 1: increasing, 0: unknown.
    int length;         // Calculated from the consist.
    int speed;          // Reported by HouseDCC.
    int active;         // 1 if reported by HouseDCC.
};

static struct VehicleModel *LayoutVehicleModels = 0;
static int                  LayoutVehicleModelsCount = 0;

static struct Vehicle *LayoutVehicles = 0;
static int             LayoutVehiclesCount = 0;

static struct TrainConsist *LayoutTrains = 0;
static int                  LayoutTrainsCount = 0;
static int                  LayoutTrainsSize = 0;

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

static struct Vehicle *houserail_train_search_car (const char *id) {

    unsigned int signature = echttp_hash_signature (id);
    int i;
    for (i = LayoutVehiclesCount - 1; i >= 0; --i) {
        if (LayoutVehicles[i].signature != signature) continue; // Fast filter

        struct Vehicle *vehicle = LayoutVehicles + i;
        if (strcmp (id, vehicle->id)) continue;

        return vehicle; // Found it.
    }
    return 0;
}

static void houserail_train_fleet (const char *id, int index) {

    int speed = houserail_field_fleet_speed (index);
    DEBUG (__FILE__ ": received fleet update for %s, speed = %d\n", id, speed);

    struct Vehicle *vehicle = houserail_train_search_car (id);
    if (vehicle) {
        vehicle->hasdcc = 1; // Now we can control it.
        if (vehicle->consist >= 0) {
            struct TrainConsist *train = LayoutTrains + vehicle->consist;
            train->speed = speed;
            train->active = 1;
        }
    } else {
        DEBUG (__FILE__ ": %s not found\n", id);
    }

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
       struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
       int count = vehicle->count;
       int j;
       for (j = 0; j < count; ++j) {
           speed2 = houserail_track_civil (&(vehicle->spots[j]), 0);
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
        lead = &(LayoutVehicles[train->cars[0]].spots[0]); // First spot.
    } else {
        struct Vehicle *vehicle = LayoutVehicles + train->cars[train->count];
        lead = &(vehicle->spots[vehicle->count-1]); // Last spot.
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
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        int j;
        for (j = vehicle->count - 1; j >= 0; --j) {
            houserail_path_move (&(train->path),
                                 vehicle->spots+j, distance, direction);
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
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        for (j = vehicle->count - 1; j >= 0; --j) {
            int distance;
            distance = houserail_track_distance (vehicle->spots+j,
                                                 &point, direction, min);
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
        int vehicle;
        int spot;
    } first, last;
    last.spot = -1;

    int i;
    for (i = train->count - 1; i >= 0; --i) {
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        int j;
        for (j = vehicle->count - 1; j >= 0; --j) {
            int post = vehicle->spots[j].post;
            if ((post < area->low) || (post > area->high)) continue;
            if (strcmp (area->line, vehicle->spots[j].line)) continue;
            first.vehicle = train->cars[i];
            first.spot = j;
            if (last.spot < 0) last = first;
        }
    }
    if (last.spot < 0) return; // No more spot over the detector.

    if (direction < 0) last = first;

    // Move the train so that the last spot exits the detector's range.
    int distance = houserail_train_spotdistance
                       (&(LayoutVehicles[last.vehicle].spots[last.spot]),
                        area, TRAINMAXDISTANCE, direction, 0);
    if (distance < 0) return;
    houserail_train_pull (train, distance, timestamp);

    // Iterate using tail recursion.
    houserail_train_pull_vacant (train, area, timestamp);
}

static void houserail_train_recalculate_spots (struct TrainConsist *train,
                                               int orientation) {

    // Calculate the track location of each car's spots backward.
    // (Since these locations are between the head and tail, no need
    // to check if the move succeeded here.)

    int i;
    for (i = 0; i < train->count; ++i) {
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        struct VehicleModel *model = LayoutVehicleModels + vehicle->model;
        vehicle->count = model->count;
        int offset = vehicle->head;
        int j;
        for (j = 0; j < vehicle->count; ++j) {
            houserail_path_move (&(train->path),
                                 vehicle->spots+j,
                                 offset+model->spots[j], orientation);
        }
    }
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
               houserail_field_fleet_move (train->id,
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
            struct TrackLocation temp = train->head;
            train->head = train->tail;
            train->tail = temp;
            houserail_train_recalculate_spots (train, 1);
            houserail_path_reverse (&(train->path));
        }
    }
    houserail_train_pull (train, min, timestamp);

    // Adjust the train speed based on its new location.
    if (abs(train->speed) > TrainRestrictedSpeed)
       houserail_field_fleet_move (train->id,
                                   houserail_train_maxspeed (train));
}

const char *houserail_train_initialize (int argc, const char **argv) {

    TrainNextFleetListener =
        houserail_field_fleet_subscribe (houserail_train_fleet);
    TrainNextDetectionListener =
        houserail_track_subscribe (houserail_train_track);
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
    return houserail_field_fleet_move (id, speed);
}

const char *houserail_train_stop (const char *id, int emergency) {
    return houserail_field_fleet_stop (id, emergency);
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
    if (!train->count) return "this train has no consist";
    if (!train->parked) return "this train was not parked";

    if (! houserail_track_vicinity (&(train->head), facing, orientation))
        return "unknown track location";

    train->path.size = train->path.count = 0;
    train->path.sections = 0;

    // Calculate the train's head and tail location, and its (reverse) path.

    int reverse = (orientation >= 0)?-1:1;
    if (! houserail_path_span (&(train->path),
                               &(train->head), reverse, train->length))
        return "invalid location (train too long?)";
    houserail_path_move (&(train->path),
                         &(train->tail), train->length, reverse);

    // Calculate the track location of each car's spots backward.
    // (Since these locations are between the head and tail, no need
    // to check if the move succeeded here.)
    houserail_train_recalculate_spots (train, reverse);

    houserail_path_reverse (&(train->path));
    train->orientation = orientation;
    train->parked = 0;
    train->speed = 0;
    return 0;
}

const char *houserail_train_consist (const char *id,
                                     const char *cars[], int count) {

    struct TrainConsist *train = houserail_train_search (id);
    if (train && (!train->parked)) return "Please park this train first";

    if (LayoutTrainsCount >= LayoutTrainsSize) {
        LayoutTrainsSize += 16;
        LayoutTrains = (struct TrainConsist *)
            realloc (LayoutTrains,
                     sizeof(struct TrainConsist) * LayoutTrainsSize);
    }

    int v;
    for (v = 0; v < count; ++v) {
        struct Vehicle *vehicle = houserail_train_search_car (cars[v]);
        if (!vehicle) return "unknown vehicle";
        if (vehicle->consist >= 0) return "conflict with another consist";
    }
    int i = LayoutTrainsCount++;
    train = LayoutTrains + i;

    strtcpy (train->id, id, sizeof(train->id));
    train->signature = echttp_hash_signature (id);
    train->parked = 1;

    int length = 0;
    for (v = 0; v < count; ++v) {
        struct Vehicle *vehicle = houserail_train_search_car (cars[v]);
        struct VehicleModel *model = LayoutVehicleModels + vehicle->model;
        vehicle->consist = i;
        vehicle->head = length;
        length += model->length;
    }
    train->count = count;
    train->length = length;
    train->active = 0;

    return 0;
}

const char *houserail_train_delete (const char *id) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "unknown train";

    // The cars are not longer part of a consist.
    int i;
    for (i = train->count - 1; i >= 0; --i) {
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        vehicle->consist = -1;
    }

    // Move the last train to the new empty train slot, unless the deleted
    // train was the last one, and decrease the count.
    struct TrainConsist *last = LayoutTrains + (--LayoutTrainsCount);
    if (train != last) {
        // Move the last train to the new empty slot.
        int index = train - LayoutTrains;
        *train = *last;
        for (i = train->count - 1; i >= 0; --i) {
            struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
            vehicle->consist = index;
        }
    }
    return 0;
}

const char *houserail_train_reload (void) {
   // TBD: reload the list of vehicles and trains from the saved status, keep known locations.
   LayoutVehicleModelsCount = 0;
   LayoutVehicleModels = 0;
   return "Not yet implemented";
}

int houserail_train_export (char *buffer, int size, const char *separator) {
    return houserail_train_status (buffer, size); // For now, same content
}

int houserail_train_status (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"train\":[";

    int i;
    for (i = 0; i < LayoutTrainsCount; ++i) {
        struct TrainConsist *train = LayoutTrains + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\"", prefix, train->id);
        if (!train->parked) {
            const char *segment = houserail_track_segment (&(train->head));
            train->head.segment = segment; // Cache the result.
            if (segment)
               cursor += snprintf (buffer+cursor, size-cursor,
                                   ",\"head\":[\"%s\",%d,\"%s\"]",
                                   train->head.line, train->head.post, segment);

            segment = houserail_track_segment (&(train->tail));
            train->tail.segment = segment; // Cache the result.
            if (segment)
               cursor += snprintf (buffer+cursor, size-cursor,
                                   ",\"tail\":[\"%s\",%d,\"%s\"]",
                                   train->tail.line, train->tail.post, segment);

            int direction = houserail_train_direction (train);
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"proceed\":[\"%s\",%d]",
                                (direction >= 0)?"up":"down",
                                abs(train->speed));
        }
        const char *prefix2 = "\"cars\":[";
        int empty = cursor;
        int j;
        for (j = 0; j < train->count; ++j) {
            struct Vehicle *vehicle = LayoutVehicles + train->cars[j];
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s\"%s\"", prefix2, vehicle->id);
            prefix2 = ",";
        }
        if (cursor > empty)
            cursor += snprintf (buffer+cursor, size-cursor, "]}");
        else
            cursor += snprintf (buffer+cursor, size-cursor, "}");
        prefix = ",";
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}

int houserail_train_locate (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"train\":[";

    int i;
    for (i = 0; i < LayoutTrainsCount; ++i) {
        struct TrainConsist *train = LayoutTrains + i;

        if (train->parked) continue;
        const char *segment = houserail_track_segment (&(train->head));
        train->head.segment = segment; // Cache the result.
        if (!segment) continue;

        int direction = houserail_train_direction (train);
        if (direction == 0) direction = train->orientation;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"head\":[\"%s\",%d,\"%s\"],"
                                             "\"proceed\":[\"%s\":%d]}",
                            prefix, train->id,
                            train->head.line, train->head.post, segment,
                            (direction >= 0)?"up":"down", abs(train->speed));
        prefix = ",";
    }
    return cursor;
}


void houserail_train_background (time_t now) {
    // TBD: background work needed?
}

