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
 * void houserail_train_testmode (int enabled);
 *
 *     Enable local traces. This is intended for unit test purpose only.
 *
 * void houserail_train_tracking (const struct TrackRange *area,
 *                                int occupied,
 *                                long long timestamp);
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
 * const struct TrackLocation *houserail_train_head (const char *id);
 * const struct TrackLocation *houserail_train_tail (const char *id);
 *
 *     Return the location of the train's head or tail. This is mostly
 *     intended for unit test's purposes.
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

#include "houseconfig.h"

#include "houserail_field.h"
#include "houserail_track.h"
#include "houserail_path.h"
#include "houserail_train.h"

static int TestMode = 0;
#define DEBUG if (TestMode || echttp_isdebug()) printf

static FleetListener *TrainNextFleetListener = 0;
static DetectionListener *TrainNextDetectionListener = 0;

static int TrainRestrictedSpeed = 20; // FIXME: make it configurable.

#define TRAINMAXCARS 16 // FIXME: arbitrary limit.
#define CARMAXSPOT    4 // FIXME: arbitrary limit.
#define TRAINMAXSPOT (TRAINMAXCARS*CARMAXSPOT) // FIXME: arbitrary limit.

#define TRAINMAXDISTANCE 50 // FIXME: make it configurable?

struct VehicleModel {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;
    int spots[CARMAXSPOT];
    int count;
};

// This holds the permanent properties of a owned vehicle
struct Vehicle {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int index;   // Self reference.

    const char *modelid;
    int model;   // Reference the array of vehicle models

    // The following is learned from the data reported by HouseDCC.
    int hasdcc;

    int consist; // -1 if the vehicle is not listed in a train consist.
};

// This holds the properties of a complete train.
struct TrainConsist {
    char id[10];
    unsigned int signature; // Seach accelerator.

    int index;   // Self reference.

    struct TrackLocation head;
    struct TrackLocation tail;

    int cars[TRAINMAXCARS]; // From head to tail.
    int carcount;
    struct TrackLocation spots[TRAINMAXSPOT]; // From head to tail.
    int spotcount;

    struct TrackPath     path; // The sections of track the train is on.

    long long updated;
    int orientation;    // -1: decreasing, 1: increasing, 0: unknown.
    int length;         // Calculated from the consist.
    int speed;          // As reported by HouseDCC.
    char parked;        // Not currently on this layout.

    // The fields below this point are live field not supposed to be saved.
    char awry;           // Location not confirmed yet.
    char active;         // 1 if reported by HouseDCC.
    char hasdcc;         // Learned from HouseDCC (DCC train consist)
};

static struct VehicleModel *LayoutVehicleModels = 0;
static int                  LayoutVehicleModelsCount = 0;

static struct Vehicle *LayoutVehicles = 0;
static int             LayoutVehiclesCount = 0;

static struct TrainConsist *LayoutTrains = 0;
static int                  LayoutTrainsCount = 0;
static int                  LayoutTrainsSize = 0;


void houserail_train_testmode (int enabled) {
    TestMode = enabled;
}

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

static int houserail_train_search_model (const char *id) {

    unsigned int signature = echttp_hash_signature (id);
    int i;
    for (i = LayoutVehicleModelsCount - 1; i >= 0; --i) {
        if (LayoutVehicleModels[i].signature != signature) continue; // Fast filter

        if (strcmp (id, LayoutVehicleModels[i].id)) continue;

        return i; // Found it.
    }
    return -1;
}

static int houserail_train_direction (struct TrainConsist *train) {
    return train->orientation * ((train->speed < 0)?-1:1);
}

static void houserail_train_fleet (const char *id, int index) {

    int speed = houserail_field_fleet_speed (index);
    DEBUG (__FILE__ ": received fleet update for %s, speed = %d\n", id, speed);

    struct TrainConsist *train = houserail_train_search (id);
    if (train) {
       // DCC consist case.
       train->hasdcc = 1;
    } else {
       struct Vehicle *vehicle = houserail_train_search_car (id);
       if (vehicle) {
           // DCC Locomotive case.
           vehicle->hasdcc = 1; // Now we can control it.
           if (vehicle->consist >= 0) {
               train = LayoutTrains + vehicle->consist;
           }
       } else {
           DEBUG (__FILE__ ": %s not found\n", id);
       }
    }

    if (train) {
       if (train->speed != speed) {
           train->speed = speed;
           houserail_path_turn (&(train->path),
                                houserail_train_direction(train));
       }
       train->active = 1;
    }
    if (TrainNextFleetListener) TrainNextFleetListener (id, index);
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
    for (i = 0; i < train->spotcount; ++i) {
       speed2 = houserail_track_civil (&(train->spots[i]), 0);
       if (speed2 < speed) speed = speed2;
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
        lead = train->spots; // First spot.
    } else {
        lead = train->spots + (train->spotcount-1);
    }
    return houserail_train_spotdistance
               (lead, area, max, direction, occupied);
}

static void houserail_train_pull (struct TrainConsist *train,
                                  int distance,
                                  long long timestamp) {

    // Move the head, tail and each car spot of the consist.
    int direction = houserail_train_direction (train);

    DEBUG (__FILE__ ": train %s moving %s by %d posts\n",
           train->id, (direction >= 0)?"up":"down", distance);

    // Extend the path first, then move each train point along
    // the extended path, and then rollup the path. This way
    // the code walks the track only once.

    houserail_path_lengthen (&(train->path), distance);

    houserail_path_move (&(train->path), &(train->head), distance, 1);
    train->head.segment = houserail_track_segment (&(train->head), direction);

    int i;
    for (i = train->spotcount - 1; i >= 0; --i) {
        houserail_path_move (&(train->path), train->spots+i, distance, 1);
        train->spots[i].segment = houserail_track_segment (train->spots+i, 0);
    }
    houserail_path_move (&(train->path), &(train->tail), distance, 1);
    train->tail.segment = houserail_track_segment (&(train->tail), 0-direction);

    houserail_path_rollup (&(train->path), &(train->tail));
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
    int found = -1;
    int i;
    for (i = 0; i < train->spotcount; ++i) {
        int distance = houserail_track_distance (train->spots+i,
                                                 &point, direction, min);
        if ((distance >= 0) && (distance < min)) {
            min = distance;
            found = i;
        }
    }
    if (found < 0) return;

    // If a spot was at the limit of a detector range, the train
    // still moved, even if not much. Make the smallest possible move.
    if (min == 0) min = 1;

    houserail_train_pull (train, min, timestamp);
}

static void houserail_train_pull_vacant (struct TrainConsist *train,
                                         const struct TrackRange *area,
                                         long long timestamp) {

    // Consider the last detectable spot that was within the detector's
    // range.. This requires iterating until no spot remain within range.

    int direction = houserail_train_direction (train);
    int first = -1;
    int last = -1;

    int i;
    for (i = train->spotcount - 1; i >= 0; --i) {
        int post = train->spots[i].post;
        if ((post < area->low) || (post > area->high)) continue;
        if (strcmp (area->line, train->spots[i].line)) continue;
        first = i;
        if (last < 0) last = first;
    }
    if (last < 0) return; // No more spot over the detector.

    if (direction < 0) last = first;

    // Move the train so that the last spot exits the detector's range.
    int distance = houserail_train_spotdistance
                       (train->spots + last,
                        area, TRAINMAXDISTANCE, direction, 0);
    if (distance <= 0) return;
    houserail_train_pull (train, distance, timestamp);

    // Iterate using tail recursion.
    houserail_train_pull_vacant (train, area, timestamp);
}

static void houserail_train_recalculate_spots (struct TrainConsist *train) {

    // Calculate the track location of each car's spots backward.
    // (Since these locations are between the head and tail, no need
    // to check if the move succeeded here.)

    int offset[TRAINMAXSPOT];
    train->spotcount = 0;
    int cursor = 0;
    int i;
    for (i = 0; i < train->carcount; ++i) {
        if (train->cars[i] < 0) continue;
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        struct VehicleModel *model = LayoutVehicleModels + vehicle->model;
        int j;
        for (j = 0; j < model->count; ++j) {
            offset[train->spotcount] = cursor + model->spots[j];
            train->spotcount += 1;
        }
        cursor += model->length;
    }
    for (i = 0; i < train->spotcount; ++i) {
        train->spots[i] = train->head;
        houserail_path_move (&(train->path), train->spots+i, offset[i], -1);
        train->spots[i].segment = houserail_track_segment (train->spots+i, 0);

        DEBUG (__FILE__ ": move spot %d by %d posts %s from %s %d, ends at %s %d\n", i, offset[i], (train->orientation >= 0)?"up":"down", train->tail.line, train->tail.post, train->spots[i].line, train->spots[i].post);
    }
}

static const char *houserail_train_adjust (struct TrainConsist *train,
                                           int reverse, int slow) {

    if (!train->active) return "No DCC locomotive detected for that consist";

    // Request a speed according to the civil speed limit of the tracks
    // below the train and the track that the train is approaching to.
    int speed = TrainRestrictedSpeed;
    if (!slow) {
        speed = houserail_train_maxspeed (train);
    }
    if (reverse) speed = 0 - speed;
    if (train->speed == speed) return 0; // No need to adjust.
    if (train->speed * speed < 0)
        return "Stop the train before reversing direction";

    if (train->hasdcc) // This is a DCC consist.
        return houserail_field_fleet_move (train->id, speed);

    // If the train does not have a DCC consist, then there must be only
    // one locomotive. Otherwise, there would be trouble..
    int i;
    for (i = 0; i < train->carcount; ++i) {
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        if (vehicle->hasdcc)
            return houserail_field_fleet_move (vehicle->id, speed);
    }
    return "Train is active but no DCC car was detected";
}

const char *houserail_train_break (struct TrainConsist *train, int emergency) {

    if (!train->active) return "No DCC locomotive detected for that consist";

    // Send the stop request even if the train was already stopped:
    // to stop is a safety concern, do it regardless of context.

    if (train->hasdcc) // This is a DCC consist.
        return houserail_field_fleet_stop (train->id, emergency);

    // If the train does not have a DCC consist, then there must be only
    // one locomotive. Otherwise, there would be trouble..
    int i;
    for (i = 0; i < train->carcount; ++i) {
        struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
        if (vehicle->hasdcc)
            return houserail_field_fleet_stop (vehicle->id, emergency);
    }
    return "Train is active but no DCC car was detected";
}

void houserail_train_tracking (const struct TrackRange *area,
                               int occupied,
                               long long timestamp) {

    DEBUG (__FILE__ ": received %s %d to %d %soccupied\n",
           area->line, area->low, area->high, occupied?"":"not ");

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
            DEBUG (__FILE__ ": train %s at %s %d to %s %d covers %s %d to %d\n",
                   train->id, train->tail.line, train->tail.post,
                              train->head.line, train->head.post,
                              area->line, area->low, area->high);
            if (train->awry) return; // Uncertain location.
            if (occupied)
               houserail_train_pull_occupied (train, area, timestamp);
            else
               houserail_train_pull_vacant (train, area, timestamp);

            // Adjust the train speed based on its new location.
            if (abs(train->speed) > TrainRestrictedSpeed)
               houserail_train_adjust (train, (train->speed < 0), 0);

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
        DEBUG (__FILE__ ": train %s at distance %d from %s %d to %d\n", train->id, distance, area->line, area->low, area->high);
        if ((distance > 0) && (distance < min)) {
           min = distance;
           closest = i;
        }
    }
    if (closest < 0) return; // FIXME: new train detected?

    // The closest train was identified. Its head must be moved to that
    // detector's location.
    DEBUG (__FILE__ ": train %s moving to %s %d to %d\n", train->id, area->line, area->low, area->high);
    train = LayoutTrains + closest;

    houserail_train_pull (train, min, timestamp);
    train->awry = 0;

    // Adjust the train speed based on its new location.
    if (abs(train->speed) > TrainRestrictedSpeed)
       houserail_train_adjust (train, (train->speed < 0), 0);
}

const char *houserail_train_initialize (int argc, const char **argv) {

    TrainNextFleetListener =
        houserail_field_fleet_subscribe (houserail_train_fleet);
    TrainNextDetectionListener =
        houserail_track_subscribe (houserail_train_tracking);
    return 0;
}

const char *houserail_train_move (const char *id, const char *dir, int slow) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "Invalid train ID";

    int reverse = 0;
    if (dir && (!strcmp ("backward", dir))) reverse = 1;

    return houserail_train_adjust (train, reverse, slow);
}

const char *houserail_train_stop (const char *id, int emergency) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "Invalid train ID";

    return houserail_train_break (train, emergency);
}

const char *houserail_train_park (const char *id) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "Unknown train";

    train->parked = 1;
    return houserail_train_break (train, 1);
}

const char *houserail_train_enter (const char *id,
                                   const char *facing, int orientation) {

    if (facing == 0) return "Please provide a track location";
    if (orientation == 0) return "Please provide a train orientation";

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "Unknown train";
    if (!train->carcount) return "This train has no consist";
    if (!train->parked) return "This train was not parked";

    if (! houserail_track_vicinity (&(train->head), facing, orientation))
        return "Unknown track location";

    train->path.size = train->path.count = 0;
    train->path.sections = 0;

    // Calculate the train's head and tail location, and its path.
    // Apply a small jolt to avoid the train to cover the device.
    // Since the position of the head is known, create the path in
    // the inverse direction, calculate the tail location and then revert.

    int inverse = (orientation >= 0)?-1:1;
    if (! houserail_path_span (&(train->path),
                               &(train->head), train->length, inverse))
        return "Invalid location (train too long?)";

    // Apply the jolt backward.
    houserail_path_move (&(train->path), &(train->head), 1, 1);
    houserail_path_lengthen (&(train->path), 1);
    houserail_path_rollup (&(train->path), &(train->head));

    train->tail = train->head;
    houserail_path_move (&(train->path),
                         &(train->tail), train->length, 1);
    train->head.segment = houserail_track_segment (&(train->head), orientation);
    train->tail.segment = houserail_track_segment (&(train->tail), inverse);
    houserail_path_turn (&(train->path), orientation);

    // Calculate the track location of each car's spots.
    // (Since these locations are within the path, no need
    // to check if the move succeeded here.)
    houserail_train_recalculate_spots (train);

    train->orientation = orientation;
    train->parked = 0;
    train->awry = 1;
    train->speed = 0;
    return 0;
}

const char *houserail_train_consist (const char *id,
                                     const char *cars[], int count) {

    struct TrainConsist *train = houserail_train_search (id);

    int v;
    for (v = 0; v < count; ++v) {
        struct Vehicle *vehicle = houserail_train_search_car (cars[v]);
        if (!vehicle) return "Unknown vehicle";
        if (vehicle->consist >= 0) {
           if ((!train) || (vehicle->consist != train->index))
               return "Conflict with another consist";
        }
    }

    if (train) {
        if (!train->parked) return "Please park this train first";

        // The consist has changed: remove all pre-existing cars for now.
        // It will be replaced by the new consist.
        for (v = 0; v < train->carcount; ++v) {
           int index = train->cars[v];
           if (index < 0) continue;
           struct Vehicle *vehicle = LayoutVehicles + index;
           if (vehicle->consist != train->index) continue; // Impossible?
           vehicle->consist = 0;
        }
    } else {
        if (LayoutTrainsCount >= LayoutTrainsSize) {
            LayoutTrainsSize += 16;
            LayoutTrains = (struct TrainConsist *)
                realloc (LayoutTrains,
                         sizeof(struct TrainConsist) * LayoutTrainsSize);
        }
        train = LayoutTrains + LayoutTrainsCount;
        strtcpy (train->id, id, sizeof(train->id));
        train->signature = echttp_hash_signature (id);
        train->index = LayoutTrainsCount++;
    }
    train->parked = 1;
    train->active = 0;

    int length = 0;
    for (v = 0; v < count; ++v) {
        struct Vehicle *vehicle = houserail_train_search_car (cars[v]);
        struct VehicleModel *model = LayoutVehicleModels + vehicle->model;
        vehicle->consist = train->index;
        if (vehicle->hasdcc) train->active = 1;
        length += model->length;
        train->cars[v] = vehicle->index;
    }
    train->hasdcc = 0; // Will reevaluate on the next DCC status report.
    train->carcount = count;
    train->length = length;

    return 0;
}

const char *houserail_train_delete (const char *id) {

    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return "Unknown train";

    // The cars are not longer part of a consist.
    int i;
    for (i = train->carcount - 1; i >= 0; --i) {
        if (train->cars[i] < 0) continue;
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
        for (i = train->carcount - 1; i >= 0; --i) {
            if (train->cars[i] < 0) continue;
            struct Vehicle *vehicle = LayoutVehicles + train->cars[i];
            vehicle->consist = index;
        }
    }
    return 0;
}

const char *houserail_train_reload (void) {

   // TBD: reload the list of vehicles and trains from the saved status, keep known locations.
   if (LayoutVehicleModels) {
       free (LayoutVehicleModels);
       LayoutVehicleModels = 0;
   }
   LayoutVehicleModelsCount = 0;

   int train = houseconfig_object (0, ".rail.train");
   if (train < 0) return "No train data";

   int models = houseconfig_array (train, ".models");
   if (models < 0) return "No train models found";

   LayoutVehicleModelsCount = houseconfig_array_length (models);
   if (LayoutVehicleModelsCount <= 0) return "Empty train model list";

   int vehicles = houseconfig_array (train, ".vehicles");
   if (vehicles < 0) return "No vehicles found";

   // Save the old list of vehicles for now, to maintain consists.
   struct Vehicle *oldvehicles = LayoutVehicles;
   int oldvehiclescount = LayoutVehiclesCount;

   LayoutVehiclesCount = houseconfig_array_length (vehicles);
   if (LayoutVehiclesCount <= 0) return "Empty vehicles list";

   int max = LayoutVehicleModelsCount;
   if (LayoutVehiclesCount > max) max = LayoutVehiclesCount;
   int *list = calloc (max, sizeof(int));

   LayoutVehicleModels = calloc (LayoutVehicleModelsCount,
                                 sizeof(struct VehicleModel));
   houseconfig_enumerate (models, list, LayoutVehicleModelsCount);

   int i;
   for (i = 0; i < LayoutVehicleModelsCount; ++i) {
      int element = list[i];
      struct VehicleModel *model = LayoutVehicleModels + i;
      model->id = houseconfig_string (element, ".id");
      model->signature = echttp_hash_signature (model->id);
      model->length = houseconfig_integer (element, ".length");

      int spots = houseconfig_array (element, ".spots");
      model->count = houseconfig_array_length (spots);
      if (model->count > CARMAXSPOT) model->count = CARMAXSPOT;

      int innerlist[CARMAXSPOT];
      houseconfig_enumerate (spots, innerlist, model->count);

      int j;
      for (j = 0; j < model->count; ++j) {
          model->spots[j] = houseconfig_integer (innerlist[j], 0);
      }
   }

   LayoutVehicles = calloc (LayoutVehiclesCount, sizeof(struct Vehicle));
   houseconfig_enumerate (vehicles, list, LayoutVehiclesCount);

   for (i = 0; i < LayoutVehiclesCount; ++i) {
      int element = list[i];
      struct Vehicle *vehicle = LayoutVehicles + i;
      vehicle->id = houseconfig_string (element, ".id");
      vehicle->signature = echttp_hash_signature (vehicle->id);
      vehicle->index = i;
      vehicle->modelid = houseconfig_string (element, ".model");
      vehicle->model = houserail_train_search_model (vehicle->modelid);
      vehicle->hasdcc = 0;
      vehicle->consist = -1;
   }

   if ((LayoutTrainsCount > 0) && oldvehicles) {

      // Restore the linkages between consists and vehicles.
      struct Vehicle **crossref =
          calloc (oldvehiclescount, sizeof(struct Vehicle *));
      for (i = 0; i < oldvehiclescount; ++i) {
          struct Vehicle *new = houserail_train_search_car (oldvehicles[i].id);
          if (new) new->consist = oldvehicles[i].consist;
          crossref[i] = new;
      }
      for (i = 0; i < LayoutTrainsCount; ++i) {
          struct TrainConsist *train = LayoutTrains + i;
          int j;
          for (j = 0; j < train->carcount; ++j) {
              struct Vehicle *car = crossref[train->cars[j]];
              if (car) {
                  train->cars[j] = car->index;
              } else {
                  train->cars[j] = -1;
              }
          }
          if (!train->parked) {
              houserail_train_recalculate_spots (train);
          }
      }
      free (oldvehicles);
      free (crossref);
   }
   return 0;
}

const struct TrackLocation *houserail_train_head (const char *id) {
    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return 0;
    return &(train->head);
}

const struct TrackLocation *houserail_train_tail (const char *id) {
    struct TrainConsist *train = houserail_train_search (id);
    if (!train) return 0;
    return &(train->tail);
}

int houserail_train_export (char *buffer, int size, const char *separator) {
    return houserail_train_status (buffer, size); // For now, same content
}

static const char *houserail_train_dccformat (struct TrainConsist *train) {

    static char dcc[32];
    dcc[0] = 0;

    const char *format = ",\"dcc\":\"%s\"";
    if (train->active) {
        if (train->hasdcc) {
            snprintf (dcc, sizeof(dcc), format, train->id);
        } else {
            int c;
            for (c = 0; c < train->carcount; ++c) {
                struct Vehicle *vehicle = LayoutVehicles + train->cars[c];
                if (vehicle->hasdcc) {
                    snprintf (dcc, sizeof(dcc), format, vehicle->id);
                    break;
                }
            }
        }
    }
    return dcc; // Must be static!
}

int houserail_train_status (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"train\":[";

    int i;
    for (i = 0; i < LayoutTrainsCount; ++i) {
        struct TrainConsist *train = LayoutTrains + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"length\":%d%s",
                            prefix, train->id, train->length,
                            houserail_train_dccformat (train));
        if (!train->parked) {
            if (train->head.segment)
               cursor += snprintf (buffer+cursor, size-cursor,
                                   ",\"head\":[\"%s\",%d,\"%s\"]",
                                   train->head.line,
                                   train->head.post, train->head.segment);

            if (train->tail.segment)
               cursor += snprintf (buffer+cursor, size-cursor,
                                   ",\"tail\":[\"%s\",%d,\"%s\"]",
                                   train->tail.line, train->tail.post, train->tail.segment);

            int direction = houserail_train_direction (train);
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"proceed\":[\"%s\",%d]",
                                (direction >= 0)?"up":"down",
                                abs(train->speed));

            const char *prefix2 = ",\"spots\":[";
            int j;
            for (j = 0; j < train->spotcount; ++j) {
                cursor += snprintf (buffer+cursor, size-cursor,
                                    "%s[\"%s\",%d,\"%s\"]",
                                    prefix2, train->spots[j].line,
                                             train->spots[j].post,
                                             train->spots[j].segment);
                prefix2 = ",";
            }
            cursor += snprintf (buffer+cursor, size-cursor, "]");
        }
        const char *prefix2 = ",\"cars\":[";
        int empty = cursor;
        int j;
        for (j = 0; j < train->carcount; ++j) {
            if (train->cars[i] < 0) continue;
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
        if (!train->head.segment) continue;
        if (!train->tail.segment) continue;

        int direction = houserail_train_direction (train);
        if (direction == 0) direction = train->orientation;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"head\":[\"%s\",%d,\"%s\"],"
                                             "\"tail\":[\"%s\",%d,\"%s\"],"
                                             "\"proceed\":[\"%s\":%d]%s}",
                            prefix, train->id,
                            train->head.line, train->head.post, train->head.segment,
                            train->tail.line, train->tail.post, train->tail.segment,
                            (direction >= 0)?"up":"down", abs(train->speed),
                            houserail_train_dccformat (train));

        prefix = ",";
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}


void houserail_train_background (time_t now) {
    // TBD: background work needed?
}

