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
 * houserail_field.c - The client stub to access field equipment.
 *
 * SYNOPSYS:
 *
 * This module hides the specific web request syntax used to access field
 * equipment:
 * - querying fleet, switch and signal status,
 * - sending fleet commands,
 * - sending switch commands.
 * (Signal is future.)
 *
 * const char *houserail_field_initialize (const char *group,
 *                                         int argc, const char **argv);
 *
 *    Initialize this module. Must be called first and only once.
 *    Returns 0 on success, an error message on failure.
 *
 * typedef void FleetListener (const char *id, int index);
 *
 * FleetListener *houserail_field_fleet_subscribe (FleetListener *listener);
 *
 *    Subscribe to vehicle state changes. The listener is only called
 *    when a change has occurred. If the listener is called with
 *    a negative index, the vehicle has disappeared.
 *
 *    This function returns the previous listener function, or 0.
 *    This can be used to chain listener calls.
 *
 *    The index provided to the listener is only valid during the call.
 *    Its value may change in between two calls.
 *
 * int houserail_field_fleet_iterate (FleetListener *listener);
 *
 *    Retrieve the current list of active vehicles. The provided listener
 *    will be called once for each known vehicle. This listener does not need
 *    to be the same as the one declared using houserail_field_subscribe().
 *
 *    This returns the number of vehicles currently known.
 *
 * int houserail_field_fleet_search (const char *id);
 *
 *    Return the index of the specified vehicle, or -1 when not found.
 *
 *    The returned index represents a transiant location: it may change
 *    on a subsequent periodic update. The index should be used immediately
 *    and never stored.
 *
 * const char *houserail_field_fleet_model (int index);
 * int         houserail_field_fleet_speed (int index);
 *
 *    Get information about a specific vehicle.
 *
 * const char *houserail_field_fleet_move (const char *id, int speed);
 *
 *    Order the designated vehicle to move at the given speed. The sign
 *    of the speed decide the direction of the movement.
 *    Returns 0 on success, an error message on failure.
 *
 * const char *houserail_field_fleet_stop (const char *id, int emergency);
 *
 *    Request the vehicle to stop. An emergency stop is immediate, a normal
 *    stop follows a desceleration curve. If id is null, this becomes a "stop
 *    all" request. Returns 0 on success, an error message on failure.
 *
 * const char *houserail_field_switch_set (const char *id, const char *state);
 * const char *houserail_field_signal_set (const char *id, const char *state);
 *
 *    Change the state of the specified switch or signal to the requested state.
 *    Returns 0 on success or if the accessory is not known, an error message
 *    on failure. Unknown accessories are not reported as failure in order to
 *    accomodate reporting the state of manual switches and signals.
 *
 * int houserail_field_status (char *buffer, int size);
 *
 *     Return the live status of field accessories in JSON format.
 *
 * void houserail_field_background (time_t now);
 *
 *    Periodic update of train information.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <echttp.h>
#include <echttp_json.h>
#include <echttp_hash.h>
#include <echttp_libc.h>

#include "houseportalclient.h"
#include "houselog.h"
#include "housediscover.h"

#include "houserail_field.h"

#define DEBUG if (echttp_isdebug()) printf

struct FleetUnit {
    char id[15];
    char model[15];
    unsigned int signature;
    int  speed;
    short present;
    short updated;
};

static struct FleetUnit *FleetDb = 0;
static int               FleetDbCount = 0;
static int               FleetDbSize = 0;

static FleetListener *FleetSubscribed = 0;

static const char *FieldLayout = 0;
static long long FieldKnown = 0;

static char *FieldControlUri = 0;

static void houserail_field_fleet_noop (const char *id, int index) {
    // Do nothing.
}

const char *houserail_field_initialize (const char *group,
                                        int argc, const char **argv) {
    FieldLayout = group;
    FleetSubscribed = houserail_field_fleet_noop;
    return 0;
}

FleetListener *houserail_field_fleet_subscribe (FleetListener *listener) {

    FleetListener *previous = FleetSubscribed;
    FleetSubscribed = listener;
    return previous;
}

int houserail_field_iterate (FleetListener *listener) {

    int i;
    int count = 0;
    for (i = FleetDbCount-1; i >= 0; --i) {
       if (!FleetDb[i].id[0]) continue;
       if (listener) listener (FleetDb[i].id, i);
       count += 1;
    }
    return count;
}

const char *houserail_field_fleet_model (int index) {
    if (index < 0 || index >= FleetDbCount) return "";
    if (!FleetDb[index].id[0]) return "";
    return FleetDb[index].model;
}

int houserail_field_fleet_speed (int index) {
    if (index < 0 || index >= FleetDbCount) return 0;
    if (!FleetDb[index].id[0]) return 0;
    return FleetDb[index].speed;
}

int houserail_field_fleet_find (const char *id, int update) {

    int i;
    int empty = -1;
    unsigned int signature = echttp_hash_signature (id);
    for (i = FleetDbCount-1; i >= 0; --i) {
        if (!FleetDb[i].id[0]) {
            empty = i;
        } else if (signature == FleetDb[i].signature) {
            if (strsame (FleetDb[i].id, id)) break;
        }
    }
    if (!update) return i;

    if (i < 0) { // Not found
        if (empty >= 0) {
            i = empty;
        } else {
           if (FleetDbCount >= FleetDbSize) {
               FleetDbSize += 16;
               FleetDb = realloc (FleetDb, sizeof(FleetDb[0])*FleetDbSize);
           }
           i = FleetDbCount++;
        }
        strtcpy (FleetDb[i].id, id, sizeof(FleetDb[i].id));
        FleetDb[i].signature = signature;
        FleetDb[i].model[0] = 0;
        FleetDb[i].speed = 0;
        FleetDb[i].updated = 1;
    }
    FleetDb[i].present = 1;
    return i;
}

int houserail_field_fleet_search (const char *id) {
    return houserail_field_fleet_find (id, 0);
}

static ParserToken *houserail_field_prepare (int count) {
 
    static ParserToken *EventTokens = 0;
    static int EventTokensAllocated = 0;
 
    if (count > EventTokensAllocated) {
        int need = EventTokensAllocated = count + 128;
        EventTokens = realloc (EventTokens, need*sizeof(ParserToken));
    }
    return EventTokens;
}

static void houserail_field_update (const char *origin, char *data, int length) {

    int count = echttp_json_estimate(data);
    ParserToken *tokens = houserail_field_prepare (count);

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, origin, "JSON syntax error, %s", error);
        return;
    }

    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, origin, "no data");
        return;
    }

    int idx = echttp_json_search (tokens, ".trains.layout");
    if (idx < 0) return;
    if (!strsame (tokens[idx].value.string, FieldLayout)) return;

    idx = echttp_json_search (tokens, ".latest");
    if (idx < 0) idx = echttp_json_search (tokens, ".trains.latest");
    if (idx >= 0) FieldKnown = tokens[idx].value.integer;
    else FieldKnown = 0;

    idx = echttp_json_search (tokens, ".trains.vehicles");
    if (idx < 0) return;
    ParserToken *vehicles = tokens + idx;

    int n = vehicles->length;
    if (n <= 0) return;

    int *vehiclelist = calloc (n, sizeof(int));
    error = echttp_json_enumerate (vehicles, vehiclelist, n);
    if (error) goto cleanup;

    int i;
    for (i = 0; i < FleetDbCount; ++i) {
         FleetDb[i].present = 0;
    }

    for (i = 0; i < n; ++i) {
         ParserToken *item = vehicles + vehiclelist[i];
         idx = echttp_json_search (item, ".id");
         if (idx < 0) continue;
         int index = houserail_field_fleet_find (item[idx].value.string, 1);
         if (index < 0) continue;
         FleetDb[index].present = 1;

         idx = echttp_json_search (item, ".speed");
         int speed;
         if (idx >= 0)
             speed = item[idx].value.integer;
         else
             speed = 0;
         if (FleetDb[index].speed != speed) {
             FleetDb[index].speed = speed;
             FleetDb[index].updated = 1;
         }

         idx = echttp_json_search (item, ".model");
         if (idx >= 0)
             strtcpy (FleetDb[index].model,
                      item[idx].value.string, sizeof(FleetDb[0].model));
         else
             FleetDb[0].model[0] = 0;
    }

    // Decrease the count if the last vehicles have disappeared.
    //
    for (i = FleetDbCount-1; i >= 0; --i) {
         if (FleetDb[i].present) break;
         if (FleetDb[i].id[0]) {
             FleetSubscribed (FleetDb[i].id, -1);
             FleetDb[i].id[0] = 0;
         }
         FleetDbCount -= 1;
    }

    // Detect changes and call the application's listener.
    for (i = FleetDbCount-1; i >= 0; --i) {
         if (!FleetDb[i].id[0]) continue;
         if (!FleetDb[i].present) {
             FleetSubscribed (FleetDb[i].id, -1);
             FleetDb[i].id[0] = 0;
         } else if (FleetDb[i].updated) {
             FleetSubscribed (FleetDb[i].id, i);
             FleetDb[i].updated = 0;
         }
    }

cleanup:
    free (vehiclelist);
}

static void houserail_field_discovered
               (void *origin, int status, char *data, int length) {

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, houserail_field_discovered, origin);
        return;
    }

    if (status == 421) return; // Not the layout requested.
    if (status == 304) return; // No update.

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, (const char *)origin,
                        "HTTP error %d", status);
        return;
    }

    const char *uri = (const char *)origin;
    if (uri != FieldControlUri) {
        if (FieldControlUri) {
            if (strcmp (FieldControlUri, uri)) {
                free (FieldControlUri);
                FieldControlUri = strdup (uri);
            }
        } else {
            FieldControlUri = strdup (uri);
        }
    }

    houserail_field_update ((const char *)origin, data, length);
}

static const char *houserail_field_request (const char *url, const char *uri) {

    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, uri, "%s", error);
        return error;
    }
    echttp_submit (0, 0, houserail_field_discovered, (void *)uri);
    return 0;
}

static void houserail_field_scan_server
                (const char *service, void *context, const char *uri) {

    char url[256];
    if (FieldKnown > 0) {
        snprintf (url, sizeof(url),
                  "%s/status?layout=%s&known=%lld",
                  uri, FieldLayout, FieldKnown);
    } else {
        snprintf (url, sizeof(url),
                  "%s/status?layout=%s", uri, FieldLayout);
    }

    const char *error = houserail_field_request (url, uri);
    if (error) houselog_trace (HOUSE_FAILURE, uri, "%s", error);
}

const char *houserail_field_fleet_move (const char *id, int speed) {

    if (!FieldControlUri) return "No train server identified yet";

    char url[256];
    snprintf (url, sizeof(url),
              "%s/fleet/move?id=%s&speed=%d", FieldControlUri, id, speed);

    return houserail_field_request (url, FieldControlUri);
}

const char *houserail_field_fleet_stop (const char *id, int emergency) {

    if (!FieldControlUri) return "No train server identified yet";

    char url[256];
    if (!id) // Stop all.
        snprintf (url, sizeof(url),
                  "%s/fleet/stop?urgent=%d", FieldControlUri, emergency);
    else
        snprintf (url, sizeof(url),
                 "%s/fleet/stop?id=%s&urgent=%d", FieldControlUri, id, emergency);

    return houserail_field_request (url, FieldControlUri);
}

const char *houserail_field_switch_set (const char *id, const char *state) {

    if (!FieldControlUri) return "No train server identified yet";

    char url[256];
    snprintf (url, sizeof(url),
              "%s/switch/set?id=%s&cmd=%s", FieldControlUri, id, state);

    return houserail_field_request (url, FieldControlUri);
}

const char *houserail_field_signal_set (const char *id, const char *state) {

    if (!FieldControlUri) return "No train server identified yet";

    char url[256];
    snprintf (url, sizeof(url),
              "%s/signal/set?id=%s&cmd=%s", FieldControlUri, id, state);

    return houserail_field_request (url, FieldControlUri);
}

int houserail_field_status (char *buffer, int size) {
    return 0;
}

void houserail_field_background (time_t now) {

    static time_t latestdiscovery = 0;

    // If any new train service was detected, force a scan now.
    if ((latestdiscovery > 0) &&
        housediscover_changed ("train", latestdiscovery)) {
        latestdiscovery = 0;
    }

    // Even if nothing new was detected, still scan every seconds, in case
    // Anything had changed.
    if (now <= latestdiscovery) return;
    latestdiscovery = now;

    housediscovered ("train", 0, houserail_field_scan_server);
}

