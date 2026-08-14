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
 * houserail_signal.c - Signal logic and rules.
 *
 * SYNOPSYS:
 *
 * void houserail_signal_testmode (int enabled);
 *
 *    Enable debug traces, for unit tests only.
 *
 * const char *houserail_signal_initialize (int argc, const char **argv);
 *
 * const char *houserail_signal_slow (const struct TrackPath *path);
 *
 *     Return the name of an upcoming stop signal, if within approach distance,
 *     or a null pointer otherwise.
 *
 * const char *houserail_signal_stop (const struct TrackPath *path);
 *
 *     Return the name of an upcoming stop signal, if within stop distance,
 *     or a null pointer otherwise.
 *
 * int houserail_signal_cleared (const char *name);
 *
 *     Return 1 if the named signal is in the cleared state, 0 otherwise.
 *
 * int houserail_signal_direction (const char *name);
 *
 *     Return the direction of the named signal.
 *
 * int houserail_signal_status (char *buffer, int size);
 *
 *     Return the live status of signals in JSON format.
 *     This is designed as a subset of the track status.
 *
 * const char *houserail_signal_reload (void);
 *
 *     Apply a newly reloaded configuration. Must be called right after
 *     houserail_topology_reload().
 *
 * void houserail_signal_background (time_t now);
 *
 *     Periodic update function.
 *
 * void houserail_signal_protect (const char *name);
 *
 *     Cancel (set to stop) all signals protecting the named resource.
 *
 * const char *houserail_signal_set (const char *name, const char *state);
 *
 *     Set a signal to the specified state, locally and in the field (if any).
 *     This is designed to be used as a listener or through a web request.
 *     This function handles null pointers. Return 0 on success, an error
 *     message on failure.
 *
 *     The valid states are "stop" and "clear". Any other state causes the
 *     signal to turn off. If the state is "clear", other signals protecting
 *     the same feature are forced to "stop".
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_libc.h>
#include <echttp_hash.h>

#include <houselog.h>
#include <houseconfig.h>

#include "houserail_topology.h"
#include "houserail_path.h"
#include "houserail_scout.h"
#include "houserail_field.h"
#include "houserail_track.h"
#include "houserail_signal.h"

static int TestMode = 0;
#define DEBUG if (TestMode || echttp_isdebug()) printf

// This data structure "augments" the TrackSignal table with current status.
//
#define SIGNAL_OFF    0
#define SIGNAL_STOP   1
#define SIGNAL_CLEAR  2

struct TrackSignalLive {

    char *id;
    int state;           // See SIGNAL_OFF, SIGNAL_STOP, SIGNAL_CLEAR
    long long timestamp;
};

static const struct TrackOptions *LayoutOptions = 0;

static const struct TrackSegment *LayoutSegments = 0;
static int                        LayoutSegmentsCount = 0;

static const struct TrackSignal *LayoutSignals = 0;
static struct TrackSignalLive   *LayoutSignalsLive = 0;
static int                       LayoutSignalsCount = 0;


void houserail_signal_testmode (int enabled) {
    TestMode = enabled;
}

const char *houserail_signal_initialize (int argc, const char **argv) {
    return 0;
}

const char *houserail_signal_reload (void) {

    struct TrackSignalLive *oldsignals = LayoutSignalsLive;
    int oldsignalscount = LayoutSignalsCount;

    LayoutOptions = houserail_topology_options ();

    LayoutSegments      = houserail_topology_segments ();
    LayoutSegmentsCount = houserail_topology_segment_count ();

    LayoutSignals      = houserail_topology_signals ();
    LayoutSignalsCount = houserail_topology_signal_count ();

    if (LayoutSignalsCount > 0)
        LayoutSignalsLive =
            calloc (LayoutSignalsCount, sizeof(struct TrackSignalLive));

    // Initialize the signal status.

    int i;
    for (i = 0; i < LayoutSignalsCount; ++i) {

        struct TrackSignalLive *status = LayoutSignalsLive + i;
        status->id = strdup (LayoutSignals[i].id);
        status->state = SIGNAL_OFF;
        status->timestamp = 0;
    }

    // Recover the (old) live status of signals, if applicable.

    for (i = 0; i < oldsignalscount; ++i) {
        int index = houserail_topology_search_signal (oldsignals[i].id);
        if (index < 0) continue;
        LayoutSignalsLive[index].state = oldsignals[i].state;
        LayoutSignalsLive[index].timestamp = oldsignals[i].timestamp;
    }

    for (i = 0; i < oldsignalscount; ++i) free (oldsignals[i].id);
    if (oldsignals) free (oldsignals);

    houselog_event ("LAYOUT", "SIGNALS", "LOADED", "");
    return 0;
}

int houserail_signal_status (char *buffer, int size) {

    static const char *namedstate[] = {"off", "stop", "clear"};

    int cursor = 0;
    const char *prefix = ",\"signal\":[";

    int i;
    for (i = 0; i < LayoutSignalsCount; ++i) {
        const struct TrackSignal *signal = LayoutSignals + i;
        struct TrackSignalLive *status = LayoutSignalsLive + i;
        const char *state = namedstate[status->state];
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s[\"%s\",\"%s\"]", prefix, signal->id, state);
        prefix = ",";
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}

void houserail_signal_background (time_t now) {
    // TBD: background work needed?
}


static void houserail_signal_cancel (int protected, int operated) {

    if (protected >= 0) {
        int i;
        for (i = 0; i < LayoutSignalsCount; ++i) {
            // Skip the signal being operated and signals not in its group
            if (i == operated) continue;
            if (LayoutSignals[i].protected != protected) continue;

            // Force this signal to stop, even if it was already marked
            // as stopped to allow manual retries.
            LayoutSignalsLive[i].state = SIGNAL_STOP;
            houserail_field_signal_set (LayoutSignals[i].id, "stop");
        }
    }
}

void houserail_signal_protect (const char *name) {
    int index = houserail_topology_search_by_id (name);
    houserail_signal_cancel (index, -1);
}

const char *houserail_signal_set (const char *name, const char *state) {

    int index = houserail_topology_search_signal (name);
    if (index < 0) return "Invalid signal";

    LayoutSignalsLive[index].timestamp = time(0);

    // To set an individual signal to stop does not impact other signals
    // since this is a safe operation.
    if (strsame (state, "stop")) {
        LayoutSignalsLive[index].state = SIGNAL_STOP;
        houserail_field_signal_set (name, state);
        return 0;
    }

    // If the signal to set to "clear" protects a switch, this switch must
    // be aligned with the signal's segment, otherwise a derail is certain.
    // To set an individual signal to "clear" also requires cancelling the
    // other signals protecting the same feature, to avoid train collisions.

    if (strsame (state, "clear")) {
        int protected = LayoutSignals[index].protected;
        if ((protected >= 0) && (protected < LayoutSegmentsCount)) {
           const char *error =
               houserail_track_safe (LayoutSignals[index].location.segment,
                                     LayoutSegments[protected].id);
           if (error) return error;
        }
        houserail_signal_cancel (protected, index);
        LayoutSignalsLive[index].state = SIGNAL_CLEAR;
        houserail_field_signal_set (name, state);
        return 0;
    }

    LayoutSignalsLive[index].state = 0;
    houserail_field_signal_set (name, "off");
    return 0;
}

const char *houserail_signal_near (const struct TrackPath *path, int max) {

    if (path->count <= 0) return 0; // Should never happen.
    const struct TrackRange *section = path->sections + path->count - 1;
    int i = houserail_topology_search_by_location (section->line,
                                                   section->high);
    if (i < 0) return 0; // Should never happen.

    // Search this segment and the next segments along the tracks.
    const struct TrackSegment *segment = LayoutSegments + i;
    int j;
    int start = section->high;
    for (;;) {
        for (j = segment->signals; j >= 0; j = LayoutSignals[j].nextonsegment) {
            if (LayoutSignals[j].direction != path->direction) continue;
            if (LayoutSignalsLive[j].state == SIGNAL_CLEAR) continue;
            if (abs(start - LayoutSignals[j].location.post) > max) continue;
        }
        int end = (path->direction > 0)? segment->high : segment->low;
        max -= abs (end - start);
        if (max <= 0) return 0;

        // This is a very crude "next" segment, but who has signals placed
        // within a switch anyway?
        i = (path->direction > 0)? segment->next : segment->previous;
        if (i < 0) return 0;
        segment = LayoutSegments + i;
        start = (path->direction > 0)? segment->low : segment->high;
    }
    return 0;
}

const char *houserail_signal_slow (const struct TrackPath *path) {
    return houserail_signal_near (path, LayoutOptions->slowDistance);
}

const char *houserail_signal_stop (const struct TrackPath *path) {
    return houserail_signal_near (path, LayoutOptions->stopDistance);
}

int houserail_signal_cleared (const char *name) {
    int i = houserail_topology_search_signal (name);
    if (i < 0) return 0;
    return (LayoutSignalsLive[i].state == SIGNAL_CLEAR);
}

int houserail_signal_direction (const char *name) {
    int i = houserail_topology_search_signal (name);
    if (i < 0) return 0;
    return LayoutSignals[i].direction;
}

