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
 * const char *houserail_train_move (const char *id, const char *to, int slow);
 * const char *houserail_train_stop (const char *id, int emergency);
 *
 *     Control the movement of one train or engine.
 *
 * void houserail_train_track (const char *line, int lowpost, int highpost,
 *                             const char *segment,
 *                             int occupied, long long timestamp);
 *
 *     Update the location of trains based on track occupancy.
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

#include <echttp.h>

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

struct TrainCar {
    char id[10];
    int head;
    int tail;
    int spot[TRAINMAXSPOT];
};

struct TrainLocation {
    const char *segment;
    const char *line;
    int post;
};

struct TrainActiveConsist {
    char id[10];
    struct TrainCar cars[TRAINMAXCARS];

    struct TrainLocation head;
    struct TrainLocation tail;
    int orientation;    // -1: decreasing, 1: increasing, 0: unknown.
    int speed;
    long long updated;
};

static struct TrainActiveConsist *LayoutActiveTrains = 0;
static int                        LayoutActiveTrainsCount = 0;

static void houserail_train_fleet (const char *id, int index) {

    DEBUG (__FILE__ ": received fleet update for %s, speed = %d\n", id, houserail_fleet_speed (index));
    if (TrainNextFleetListener) TrainNextFleetListener (id, index);
}

static int houserail_train_direction (struct TrainActiveConsist *train) {
    return train->orientation * ((train->speed < 0)?-1:1);
}

static int houserail_train_covers (struct TrainActiveConsist *train,
                                   const char *segment) {

    if (!train->head.segment) return 0;
    if (!train->tail.segment) return 0;
    return houserail_track_isbetween
               (segment, train->tail.segment, train->tail.post,
                         train->head.segment, train->head.post);
}

static int houserail_train_distance (struct TrainActiveConsist *train,
                                     const char *segment, int lowpost, int highpost, int max) {
    int direction = houserail_train_direction (train);
    int post = (direction >= 0) ? lowpost : highpost;

    struct TrainLocation *lead =
        (train->speed >= 0) ? &(train->head) : &(train->tail);

    return houserail_track_distance (lead->segment, lead->post,
                                     segment, post, max);
}

static void houserail_train_move_head (struct TrainActiveConsist *train,
                                       const char *segment,
                                       int lowpost, int highpost,
                                       int occupied, long long timestamp) {
    // TBD
}

static void houserail_train_move_car (struct TrainActiveConsist *train,
                                      const char *segment,
                                      int lowpost, int highpost,
                                      int occupied, long long timestamp) {
    // TBD
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

    int i;
    for (i = 0; i < LayoutActiveTrainsCount; ++i) {
        struct TrainActiveConsist *train = LayoutActiveTrains + i;
        if (houserail_train_covers (train, segment)) {
            houserail_train_move_car
               (train, segment, lowpost, highpost, occupied, timestamp);
            train->updated = timestamp;
            return;
        }
    }

    // Find the nearest train heading toward the designed location.
    // Limit the search to a maximum distance. Each time we found a train,
    // that train's distance become the new limit: it makes no sense to search
    // for a farther train.
    int min = TRAINMAXDISTANCE;
    int closest = -1;
    int distance;
    struct TrainActiveConsist *train;
    int direction;
    for (i = 0; i < LayoutActiveTrainsCount; ++i) {
        train = LayoutActiveTrains + i;
        if (train->speed == 0) continue;
        distance = houserail_train_distance (train, segment, lowpost, highpost, min);
        if ((distance >= 0) && (distance < min)) {
           min = distance;
           closest = i;
        }
    }
    if (closest < 0) return; // TBD: new train detected?

    // The closest train was identified. Its head must be moved to that
    // detector's location.
    train = LayoutActiveTrains + closest;

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
    direction = houserail_train_direction (train);
    int post = -1;
    if (direction > 0) post = occupied?lowpost:highpost;
    else if (direction < 0) post = occupied?highpost:lowpost;
    if (post >= 0)
        houserail_train_move_head
               (train, segment, lowpost, highpost, occupied, timestamp);
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

const char *houserail_train_reload (void) {
   // TBD
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

