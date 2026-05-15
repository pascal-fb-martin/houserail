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
 * void houserail_train_track (const char *name, long long timestamp,
 *                             int lowpost, int highpost);
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

#define DEBUG if (echttp_isdebug()) printf

static FleetListener *TrainNextListener = 0;

static int TrainRestrictedSpeed = 3; // TBD: set it up.

static void houserail_train_fleet (const char *id, int index) {

    DEBUG (__FILE__ ": received update for %s, speed = %d\n", id, houserail_fleet_speed (index));
    if (TrainNextListener) TrainNextListener (id, index);
}

const char *houserail_train_initialize (int argc, const char **argv) {

    TrainNextListener = houserail_fleet_subscribe (houserail_train_fleet);
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


void houserail_train_track (const char *name, long long timestamp,
                            int occupied, int lowpost, int highpost) {
    // TBD
    printf (__FILE__ ": track %s %soccupied from %d to %d at %lld\n",
            name, occupied?"":"not ", lowpost, highpost, timestamp);
}


const char *houserail_train_reload (void) {
   return "Not yet implemented";
}

int houserail_train_export (char *buffer, int size, const char *separator) {
    return 0; // TBD
}

int houserail_train_status (char *buffer, int size) {
    return 0;
}


void houserail_train_background (time_t now) {
}

