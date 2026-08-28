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
 * houserail_track.c - Track topology and signaling devices.
 *
 * SYNOPSYS:
 *
 * void houserail_track_testmode (int enabled);
 *
 *    Enable debug traces, for unit tests only.
 *
 * const char *houserail_track_initialize (int argc, const char **argv);
 *
 * typedef int DetectionListener (const struct TrackRange *area,
 *                                int occupied, long long timestamp);
 *
 * DetectionListener *houserail_track_subscribe (DetectionListener *listener);
 *
 *    Subscribe to track detection changes. This returns the previous listener
 *    as a way to chain listeners. It is up to the caller to maintain that
 *    chain. That previous listener might be null, i.e. no previous listener.
 *
 *    NOTE: the exact location is (line, low post, high post). The segment
 *    parameter is a pre-calculated accelerator. The line parameter could
 *    be needed if the segment is part of an interlocking (more than one
 *    branch in that segment).
 *
 *    This same listener is also called with a null area pointer as a way
 *    to signal the end of a burst and allow the client to perform some
 *    flush actions.
 *
 *    A listener returns 1 when it consumed the event (i.e. it changed
 *    something) or 0 when it ignored the event. Events that are not
 *    consumed will trigger a log entry, while those consumed will not
 *    to limit the volume of log entries generated.
 *
 * void houserail_track_input (const char *name,
 *                             long long timestamp, const char *state);
 *
 *     Update track detection based on detector input. This is a listener
 *     to the field input changes. See the housecontrol.c module.
 *
 * void houserail_track_flush (void);
 *
 *     This function is to be called after a complete input change message
 *     was processed, at the end of a burst of input changes notifications.
 *
 * int houserail_track_status (char *buffer, int size);
 *
 *     Return the live status of tracks in JSON format.
 *
 * int houserail_track_detectors (char *buffer, int size);
 *
 *     Return the list of detectors in JSON format.
 *
 * const char *houserail_track_reload (void);
 *
 *     Apply a newly reloaded configuration. Must be called right after
 *     houserail_topology_reload().
 *
 * void houserail_track_background (time_t now);
 *
 *     Periodic update function.
 *
 * int houserail_track_civil (const struct TrackLocation *point,
 *                            int direction, const char **cause);
 *
 *     Return the civil speed limit applicable to the specified location in
 *     the specified direction. The cause string indicates what caused a speed
 *     restriction.
 *
 *     If the direction is 0 (i.e. none), the limit is the civil speed for
 *     the segment at that location. Otherwise, the limit is the smallest of:
 *     - the civil speed for the segment at this location,
 *     - the slow or stop zones speed for the direction and the segment at
 *       this location,
 *     - and the civil speed on the approaching segment in that direction.
 *
 * int houserail_track_restricted (void);
 *
 *     Return the restricted speed defined for this layout.
 *
 * int houserail_track_poll (void);
 *
 *     Return the field polling period as configured, or else a default value.
 *     The value returned here is always valid, even during initialization.
 *
 * The functions below are used to move a train along the track. The path
 * followed depend on the position of the switches, like a train would.
 *
 * int houserail_track_vicinity (struct TrackLocation *point,
 *                               const char *id, int direction);
 *
 *     Retrieve a location near the specified detector, or within
 *     the specified segment. Return 0 on failure, 1 otherwise.
 *
 * int houserail_track_walk (struct TrackRange *path, int size,
 *                           const struct TrackLocation *limit1,
 *                           const struct TrackLocation *limit2,
 *                           int direction, int max);
 *
 *     This is the main function to walk along the track starting at limit1 in
 *     the specified direction. The logic follows the state of the switches,
 *     as a train would. Return the number of track sections on success, or
 *     0 if the path could not be retrieved.
 *
 *     Three modes are supported here:
 *     Limit based:
 *         limit2 is provided, max is 0. The logic follows the tracks until
 *         it finds limit2 or the end of the rails.
 *     Distance based:
 *         max is provided, limit2 is 0. The logic follows the tracks until
 *         it reaches the specified distance or the end of the rails.
 *     Limit based with a distance bound:
 *         limit2 and max are defined. The logic follows the tracks until it
 *         finds limit2, reaches the specified distance or the end of the rails.
 *
 * int houserail_track_distance (const struct TrackLocation *point1,
 *                               const struct TrackLocation *point2,
 *                               int direction, int max);
 *
 *     Return the distance a train would have to move by between the two
 *     track points provided.
 *
 * const char *houserail_track_segment (const struct TrackLocation *point,
 *                                      int direction);
 *
 *     Return the name of the segment where the specified point is located.
 *     The direction indicates a preferred orientation:
 *     >0:  if the point is at the limit between two segments, prefer the
 *         the segment where this point is the high limit, if any.
 *     0:  no preference.
 *     <0: if the point is at the limit between two segments, prefer the
 *         the segment where this point is the low limit, if any.
 *
 *     The logic here is that one want to locate a train on the segment that
 *     it covers, not the segment it is going to enter or has just vacated.
 *
 * const char *houserail_track_switch (const char *name, const char *state);
 *
 *     Set a switch to the specified position, locally and in the field
 *     (if any). This is designed to be used as a listener or through a web
 *     request. This function handles null pointers. Return 0 on success,
 *     an error message on failure.
 *
 *     The valid states are "normal" and "reverse".
 *
 * const char *houserail_track_safe (const char *from,
 *                                   const char *to, const char **exit);
 *
 *     If the two names refer to valid segments, and a movement between
 *     the two segment is deemed safe, return a null pointer. Otherwise
 *     return a description of the safety violation. On success, provides
 *     a reference to the ID of the exit segment (the segment that caused
 *     the search to stop).
 *
 *     The logic is transitive as long as segment "to" is a switch leading
 *     from segment "from" to another switch. If any switch along the way
 *     is found misaligned, then a safety violation is raised. The logic
 *     stops at the first segment that is not a switch.
 *
 *     This function is meant to be called from module houserail_signal.c.
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
#include "houserail_scout.h"
#include "houserail_field.h"
#include "houserail_track.h"
#include "houserail_signal.h"

static int TestMode = 0;
#define DEBUG if (TestMode || echttp_isdebug()) printf

// This data structure "augments" the TrackSegment table with current status.
//
struct TrackSegmentLive {

    char *id;
    int needle;   // The adjacent segment connected to the needle's position.
};

// This data structure "augments" the TrackDetector table with current status.
//
struct TrackDetectorLive {

    char *id;
    int occupied;
    long long timestamp;
};

// This data structure "augments" the TrackSignal table with current status.
//
struct TrackSignalLive {

    char *id;
    int state;           // 0: off, 1 stop, 2 go.
    long long timestamp;
};

static DetectionListener *TrackNextListener = 0;

static const struct TrackOptions *LayoutOptions = 0;

static const struct TrackModel *LayoutModels = 0;
static int                      LayoutModelsCount = 0;

static const struct TrackSegment *LayoutSegments = 0;
static struct TrackSegmentLive   *LayoutSegmentsLive = 0;
static int                        LayoutSegmentsCount = 0;

static const struct TrackDetector *LayoutDetectors = 0;
static struct TrackDetectorLive   *LayoutDetectorsLive = 0;
static int                         LayoutDetectorsCount = 0;

void houserail_track_testmode (int enabled) {
    TestMode = enabled;
}

static const struct TrackDetector *houserail_track_search_detector (const char *id) {

    int index = houserail_topology_search_detector (id);
    if (index < 0) return 0;
    return LayoutDetectors + index;
}

const char *houserail_track_initialize (int argc, const char **argv) {

    return 0;
}

DetectionListener *houserail_track_subscribe (DetectionListener *listener) {

    if (!listener) return 0;
    DetectionListener *previous = TrackNextListener;
    TrackNextListener = listener;
    return previous;
}

void houserail_track_input (const char *name,
                            long long timestamp, const char *state) {

    int detectorindex = houserail_topology_search_detector (name);
    if (detectorindex < 0) return;

    const struct TrackDetector *detector = LayoutDetectors + detectorindex;
    if (detector->segment < 0) return;

    struct TrackDetectorLive *status = LayoutDetectorsLive + detectorindex;
    int occupied = strsame (state, "on");
    if (occupied == status->occupied) return;
    status->occupied = occupied;
    status->timestamp = timestamp;

    int consumed = 0;
    if (TrackNextListener)
        consumed = TrackNextListener (&(detector->area), occupied, timestamp);

    if (!consumed) {
        int timing = (int) (timestamp % 10000);
        houselog_event ("DETECTOR", name, state, "AT %d.%03d (%s %d TO %d)",
                        timing / 1000, timing % 1000, detector->area.line,
                        detector->area.low, detector->area.high);
    }
}

void houserail_track_flush (void) {
    if (TrackNextListener) TrackNextListener (0, 0, 0);
}

const char *houserail_track_reload (void) {

    struct TrackSegmentLive *oldsegments = LayoutSegmentsLive;
    int oldsegmentscount = LayoutSegmentsCount;

    struct TrackDetectorLive *olddetectors = LayoutDetectorsLive;
    int olddetectorscount = LayoutDetectorsCount;

    LayoutOptions = houserail_topology_options ();

    LayoutModels      = houserail_topology_models ();
    LayoutModelsCount = houserail_topology_model_count ();

    LayoutSegments      = houserail_topology_segments ();
    LayoutSegmentsCount = houserail_topology_segment_count ();

    LayoutSegmentsLive =
        calloc (LayoutSegmentsCount, sizeof(struct TrackSegmentLive));

    LayoutDetectors      = houserail_topology_detectors ();
    LayoutDetectorsCount = houserail_topology_detector_count ();

    LayoutDetectorsLive =
        calloc (LayoutDetectorsCount, sizeof(struct TrackDetectorLive));

    // Initialize the segment status.
    //
    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {

        const struct TrackSegment *segment = LayoutSegments + i;
        struct TrackSegmentLive *status = LayoutSegmentsLive + i;

        status->id = strdup (segment->id);
        status->needle = -1;
        if (segment->common >= 0) {
            // Default state of a switch is 'normal'.
            status->needle =
               (segment->common == segment->next)? segment->previous : segment->next;
        }
    }

    // Recover the (old) live status of segments, if applicable.

    for (i = 0; i < oldsegmentscount; ++i) {
        int index = houserail_topology_search_by_id (oldsegments[i].id);
        if (index < 0) continue;
        if (LayoutSegments[index].branch >= 0)
            LayoutSegmentsLive[index].needle = oldsegments[i].needle;
    }

    // Initialize the detectors status.

    for (i = 0; i < LayoutDetectorsCount; ++i) {

        struct TrackDetectorLive *status = LayoutDetectorsLive + i;
        status->id = strdup (LayoutDetectors[i].id);
        status->occupied = 0;
        status->timestamp = 0;
    }

    // Recover the (old) live status of detectors, if applicable.

    for (i = 0; i < olddetectorscount; ++i) {
        int index = houserail_topology_search_detector (olddetectors[i].id);
        if (index < 0) continue;
        LayoutDetectorsLive[index].occupied = olddetectors[i].occupied;
        LayoutDetectorsLive[index].timestamp = olddetectors[i].timestamp;
    }

    for (i = 0; i < oldsegmentscount; ++i) free (oldsegments[i].id);
    for (i = 0; i < olddetectorscount; ++i) free (olddetectors[i].id);
    if (oldsegments) free (oldsegments);
    if (olddetectors) free (olddetectors);

    houselog_event ("LAYOUT", "TRACK", "LOADED", "");
    return 0;
}

static int houserail_track_status_segments (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"segment\":[";

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        const struct TrackSegment *segment = LayoutSegments + i;
        const char *occupancy = "off";
        int j;
        for (j = segment->detector; j >= 0; j = LayoutDetectors[j].next) {
            if (LayoutDetectorsLive[j].occupied) {
                occupancy = "on";
                break;
            }
        }
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s[\"%s\",\"%s\"]",
                            prefix, segment->id, occupancy);
        prefix = ",";
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}

int houserail_track_detectors (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"detector\":[";

    int i;
    for (i = 0; i < LayoutDetectorsCount; ++i) {
        const struct TrackDetector *detector = LayoutDetectors + i;
        struct TrackDetectorLive *status = LayoutDetectorsLive + i;
        const char *state = status->occupied?"on":"off";
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s[\"%s\",\"%s\"]", prefix, detector->id, state);
        prefix = ",";
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}

static int houserail_track_status_switches (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"switch\":[";

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        const struct TrackSegment *segment = LayoutSegments + i;
        struct TrackSegmentLive *status = LayoutSegmentsLive + i;
        if (segment->branch >= 0) {
            const char *state = "invalid";
            if (status->needle == segment->branch)
                state = "reverse";
            else if (status->needle == segment->next)
                state = "normal";
            else if (status->needle == segment->previous)
                state = "normal";
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s[\"%s\",\"%s\"]",
                                prefix, segment->id, state);
            prefix = ",";
        }
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}

int houserail_track_status (char *buffer, int size) {

    int cursor = houserail_track_status_segments (buffer, size);
    cursor += houserail_track_status_switches (buffer+cursor, size-cursor);
    cursor += houserail_track_detectors (buffer+cursor, size-cursor);
    cursor += houserail_signal_status (buffer+cursor, size-cursor);
    return cursor;
}


void houserail_track_background (time_t now) {
    // TBD: background work needed?
}

const char *houserail_track_segment (const struct TrackLocation *point,
                                     int direction) {

    if (point->segment) return point->segment;
    int index = houserail_topology_search_by_location (point->line, point->post);
    if (index < 0) return 0;
    if (!direction) return LayoutSegments[index].id;

    // If the point is at the limit between two segments, each of these two
    // segments would be a valid response. The direction parameter is used
    // to indicate which of the two segments is preferred.

    const struct TrackSegment *segment = LayoutSegments + index;
    if (direction > 0) {
        if ((segment->low == point->post) && (segment->previous >= 0)) {
            DEBUG (__FILE__ ": On the low edge of segment %s\n", segment->id);
            int alternative = segment->previous;
            segment = LayoutSegments + alternative;
            DEBUG (__FILE__ ": Trying segment %s\n", segment->id);
            if ((segment->high == point->post) &&
                strsame (segment->line, point->line)) {
                index = alternative;
            }
        }
    } else {
        if ((segment->high == point->post) && (segment->next >= 0)) {
            DEBUG (__FILE__ ": on the high edge of segment %s\n", segment->id);
            int alternative = segment->next;
            segment = LayoutSegments + alternative;
            DEBUG (__FILE__ ": Trying segment %s\n", segment->id);
            if ((segment->low == point->post) &&
                strsame (segment->line, point->line)) {
                index = alternative;
            }
        }
    }
    return LayoutSegments[index].id;
}

static int houserail_track_locate (const struct TrackLocation *point) {

    DEBUG (__FILE__ ": houserail_track_locate(): use location %s.%d\n", point->line, point->post);
    return houserail_topology_search_by_location (point->line, point->post);
}

// Retrieve the track range covered by the specified segment.
// This function handles switches.
//
static void houserail_track_limits (const char *line, int direction,
                                    const struct TrackSegment *segment,
                                    struct TrackRange *range) {

    struct TrackSegmentLive *status = LayoutSegmentsLive + segment->index;

    // Consider the segment's 'normal' range as the default.
    range->line = segment->line;
    range->segment = segment->id;
    range->low = segment->low;
    range->high = segment->high;

    if (segment->branch < 0) return; // No ambiguity: straight segment.

    const struct TrackSegment *branch = LayoutSegments + segment->branch;

    // What is the geometry of the switch: increasing or decreasing posts?
    int geometry = (segment->common == segment->previous)?1:-1;

    int onbranch = 0;
    if (direction == geometry) {
        // Follow the needle on a divergent switch.
        if (status->needle == segment->branch) onbranch = 1;
    } else {
        // Does this come from the branch of a convergent switch?
        if (strsame (line, branch->line)) onbranch = 1;
    }
    if (onbranch) {
        range->line = branch->line;
        const struct TrackModel *model = LayoutModels + segment->model;
        if (geometry > 0) {
            range->low = branch->low - model->reverse;
            range->high = branch->low;
        } else {
            range->low = branch->high;
            range->high = branch->high + model->reverse;
        }
    }
}

int houserail_track_vicinity (struct TrackLocation *point,
                              const char *id, int direction) {

    struct TrackRange range = {0, 0, -1, -1};

    int index = houserail_topology_search_by_id (id);
    if (index >= 0) {
        const struct TrackSegment *segment = LayoutSegments + index;

        // Assume the common case first (simple segment or normal track).
        range.line = segment->line;
        range.segment = segment->id;
        range.low = segment->low;
        range.high = segment->high;

        if (segment->branch >= 0) {
            // This is a switch: we must select the active track.
            const struct TrackSegmentLive *status = LayoutSegmentsLive + index;
            if (status->needle == segment->branch) {
                const char *line = LayoutSegments[segment->branch].line;
                houserail_track_limits (line, direction, segment, &range);
            }
        }

    } else {
        const struct TrackDetector *detector =
                           houserail_track_search_detector (id);
        if (detector) {
           range.segment = LayoutSegments[detector->segment].id;

           int index = houserail_topology_search_by_id (range.segment);
           const struct TrackSegment *segment = LayoutSegments + index;
           if (segment->branch >= 0) {
               // Cannot be positioned on the inactive track of a switch.
               const struct TrackSegmentLive *status = LayoutSegmentsLive + index;
               int on_normal = strsame (detector->area.line, segment->line);
               if (status->needle == segment->branch) {
                   if (on_normal) return 0;
               } else {
                   if (!on_normal) return 0;
               }
           }
           range.line = detector->area.line;
           range.low = detector->area.low;
           range.high = detector->area.high;
        }
    }
    if (!range.line) return 0; // Not found.

    point->line = range.line;
    point->segment = range.segment;

    if (! direction) { // Not moving: choose a point in the middle.

        point->post = (range.high + range.low) / 2;

    } else { // Choose the limit according to the direction of travel.

        if (range.low < range.high) {
            point->post = (direction > 0)? range.low : range.high;
        } else {
            point->post = (direction > 0)? range.high : range.low;
        }
    }
    return 1;
}

// Make one step to the next segment. This handles switches.
//
static int houserail_track_step (const struct TrackSegment *segment,
                                 int direction) {

    struct TrackSegmentLive *status = LayoutSegmentsLive + segment->index;

    // The default is to follow the 'normal' direction
    //
    int next = (direction > 0) ? segment->next : segment->previous;
    if (segment->branch < 0) return next; // No ambiguity.

    // The segment is a switch. What is it geometry compare to the direction?
    //
    if (next < 0) return next; // Switches to nowhere don't exist.
    if (next != segment->common)
        return status->needle; // Follow the needle on a divergent switch
    return next;
}

int houserail_track_civil (const struct TrackLocation *point,
                           int direction, const char **cause) {

    DEBUG (__FILE__ ": houserail_track_civil (%s %d, %d)\n", point->line, point->post, direction);

    int speed, speed2;
    int index = houserail_track_locate (point);
    if (index < 0) {
        DEBUG (__FILE__ ": invalid location %s.%d\n", point->line, point->post);
        *cause = "invalid location";
        return 0; // Stop whenever there is any doubt.
    }

    const struct TrackSegment *segment = LayoutSegments + index;
    struct TrackSegmentLive *status = LayoutSegmentsLive + index;
    speed = LayoutModels[segment->model].civil;
    *cause = "civil speed";
    DEBUG (__FILE__ ": Consider civil speed %d for segment %s (model %s)\n",
           speed, segment->id, LayoutModels[segment->model].id);

    if ((speed > LayoutOptions->switchReverseSpeed) &&
        (segment->branch >= 0) && (status->needle == segment->branch)) {
       *cause = "reverse branch";
       speed = LayoutOptions->switchReverseSpeed;
       DEBUG (__FILE__ ": use reverse civil speed %d instead for switch %s in reverse state",
              speed, segment->id);
    }
    if (!direction) return speed;

    // slow down and stop when coming to the end of the line.
    //
    if (segment->ending == direction) {
        if (segment->stop.line &&
            (point->post >= segment->stop.low) &&
            (point->post < segment->stop.high)) {
            DEBUG (__FILE__ ": Arriving at the end of line %s, stop\n", segment->line);
            *cause = "end of line";
            return 0; // Inside the stop zone.
        }
        if (segment->slow.line &&
            (point->post >= segment->slow.low) &&
            (point->post < segment->slow.high)) {
            DEBUG (__FILE__ ": Approaching the end of line %s, speed restricted to %d\n", segment->line, LayoutOptions->restrictedSpeed);
            *cause = "end of line";
            return LayoutOptions->restrictedSpeed; // Inside the slow zone.
        }
    }

    // Look at the civil speed for the next segment, if close enough.
    //
    int goal = (direction > 0)?segment->high:segment->low;
    int d = abs (point->post - goal);
    DEBUG (__FILE__ ": distance from segment after %s is %d\n", segment->id, d);
    if (d < LayoutOptions->stopDistance) {
        int index2 = houserail_track_step (segment, direction);
        if (index2 < 0) return speed;

        segment = LayoutSegments + index2;
        status = LayoutSegmentsLive + index2;
        speed2 = LayoutModels[segment->model].civil;
        DEBUG (__FILE__ ": Consider civil speed %d from segment %s (model %s)\n",
               speed2, segment->id, LayoutModels[segment->model].id);
        if (speed2 < speed) speed = speed2;

        if (segment->branch >= 0) {

            // No train can enter a switch positioned for the opposite
            // direction: how is that segment connected to the original one?
            if ((index != status->needle) && (index != segment->common)) {
                DEBUG (__FILE__ ": stop before entering opposite switch %s\n", segment->id);
                *cause = "switch misaligned";
                speed = 0;
            }

            if ((speed > LayoutOptions->switchReverseSpeed) &&
                (status->needle == segment->branch)) {
                  DEBUG (__FILE__ ": use reverse civil speed %d instead for switch %s in reverse state",
                         speed, segment->id);
                *cause = "reverse branch";
                speed = LayoutOptions->switchReverseSpeed;
            }
        }
    }
    return speed;
}

int houserail_track_walk (struct TrackRange *path, int size,
                          const struct TrackLocation *limit1,
                          const struct TrackLocation *limit2,
                          int direction, int max) {

    if (!limit1) return 0; // Must have a starting point.
    if ((!limit2) && (!max)) return 0; // Must have at least one end criteria.

    // Walk the tracks from one limit until we meet the other limit, the
    // max distance or the end of the rails.
    //
    int index = houserail_track_locate (limit1);
    if (index < 0) return 0;
    const struct TrackSegment *segment = LayoutSegments + index;

    int cursor = 0;
    const char *line = path[0].line = limit1->line;
    path[0].segment = segment->id;
    path[0].low = path[0].high = limit1->post;

    int behind = 0;
    int distance = 0;

    DEBUG (__FILE__ ": Start walking at segment %s\n", segment->id);
    struct TrackRange current;
    houserail_track_limits (line, direction, segment, &current);

    for (;;) {

        // Was a limit condition reached?
        //
        DEBUG (__FILE__ ": Walking segment %s\n", segment->id);
        if (limit2 &&
            (limit2->post >= current.low) &&
            (limit2->post <= current.high) &&
            (strsame (limit2->line, current.line))) {

            path[cursor].high = limit2->post;
            return cursor+1; // Reached the destination.
        }
        if (max > 0) {
            int end = (direction > 0)? current.high : current.low;
            behind = distance;
            distance += abs (end - path[cursor].high);
            DEBUG (__FILE__ ": Walked %d posts so far\n", distance);
            if (distance >= max) goto toofar;
        }

        // No end condition reached: keep going.
        //
        int join1 = (direction > 0)? current.high : current.low;

        index = houserail_track_step (segment, direction);
        if (index < 0) break;
        const struct TrackSegment *next = LayoutSegments + index;

        struct TrackRange upcoming;
        houserail_track_limits (current.line, direction, next, &upcoming);
        DEBUG (__FILE__ ": Walking to segment %s (%s %d to %d)\n", next->id, upcoming.line, upcoming.low, upcoming.high);

        int join2 = (direction > 0)? upcoming.low : upcoming.high;
        DEBUG (__FILE__ ": Segments %s and %s join at %s.%d and %s.%d\n",
               segment->id, next->id, current.line, join1, upcoming.line, join2);

        if ((join1 != join2) || (!strsame (line, upcoming.line))) {

           // The name of the line changed or a loop junction was reached:
           // finalize the current section and create a new one.

           path[cursor].high = (direction > 0) ? current.high : current.low;
           DEBUG (__FILE__ ": Section %d end at %s %d\n", cursor, path[cursor].line, path[cursor].high);
           if (++cursor >= size) return 0; // Overflow.

           path[cursor].line = line = upcoming.line;
           path[cursor].segment = next->id;
           path[cursor].high = path[cursor].low =
               (direction > 0)? upcoming.low : upcoming.high;
           DEBUG (__FILE__ ": Section %d starts at %s %d\n", cursor, path[cursor].line, path[cursor].low);
        } else {
           // Remember the point that was reached in the path.. for now.
           path[cursor].segment = next->id;
           path[cursor].high = (direction > 0) ? current.high : current.low;
        }

        segment = next;
        current = upcoming;
    }
    if (limit2 && (index < 0)) return 0; // Could not find the endpoint.
    return cursor+1;

toofar:

    if (limit2) return 0; // Max limit reached before the endpoint.
    int left = max - behind;
    path[cursor].segment = segment->id;
    path[cursor].high =
      (direction > 0) ? path[cursor].high + left : path[cursor].high - left;
    return cursor+1;
}

int houserail_track_distance (const struct TrackLocation *point1,
                              const struct TrackLocation *point2,
                              int direction, int max) {

   struct TrackRange path[16]; // FIXME: arbitrary limit.
   int count = houserail_track_walk (path, 16, point1, point2, direction, max);
   if (count <= 0) return -1;

   int distance = 0;
   int i;
   for (i = 0; i < count; ++i) {
       distance += abs(path[i].high - path[i].low);
       DEBUG (__FILE__ ": distance %d after section %d: %s from %d to %d\n",
              distance, i, path[i].line, path[i].low, path[i].high);
       if (max && (distance > max)) return -1;
   }
   return distance;
}

const char *houserail_track_switch (const char *name, const char *state) {

    // Standardize the state string to one of only two strings. This makes
    // the loop faster by allowing to compare pointers instead of strings.
    //
    static const char Normal[] = "normal";
    static const char Reverse[] = "reverse";
    if (strsame (state, Normal)) state = Normal;
    else if (strsame (state, Reverse)) state = Reverse;
    else return "Invalid switch command";

    int index = houserail_topology_search_by_id (name);
    if (index < 0) return "Invalid name";

    const struct TrackSegment *segment = LayoutSegments + index;
    if (segment->branch < 0) return "Not a switch";

    struct TrackSegmentLive *status = LayoutSegmentsLive + index;
    int oldposition = status->needle;

    // Propagate the new state to all the switches that share that same control
    const char *control = segment->control;
    const char *protected = LayoutSegments[segment->protected].id;
    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        segment = LayoutSegments + i;
        if (segment->branch < 0) continue;
        if (!strsame (control, segment->control)) continue;

        if (state == Normal) {
            LayoutSegmentsLive[i].needle =
                (segment->common == segment->next) ? segment->previous : segment->next;
        } else {
            LayoutSegmentsLive[i].needle = segment->branch;
        }
    }

    // If the switch is going to move, cancel any existing route.
    if (status->needle != oldposition) houserail_signal_protect (protected);

    houserail_field_switch_set (control, state);

    return 0;
}

int houserail_track_restricted (void) {
    return LayoutOptions->restrictedSpeed;
}

int houserail_track_poll (void) {
    if (!LayoutOptions) return 200; // Reasonable value until we get a config.
    return LayoutOptions->fieldPollPeriod;
}

const char *houserail_track_safe (const char *from,
                                  const char *to, const char **exit) {

    int before = houserail_topology_search_by_id (from);
    int entry = houserail_topology_search_by_id (to);
    if ((before < 0) || (entry < 0)) return "Invalid track";

    int protected = LayoutSegments[entry].protected; // Same for all switches
    int destination = entry;
    while (destination >= 0) {

        if ((LayoutSegments[destination].branch < 0)) {
            // Skip a single regular track between two switches in the
            // current group. (The track might be one half of a crossing.)
            if (destination == entry) break;
            int ahead;
            if (LayoutSegments[destination].next != before) {
                ahead = LayoutSegments[destination].next;
            } else {
                ahead = LayoutSegments[destination].previous;
            }
            if (ahead < 0) break;
            if (LayoutSegments[ahead].branch < 0) break;
            if (LayoutSegments[ahead].protected != protected) break;

            // Start over from the switch after that regular track.
            before = destination;
            destination = entry = ahead;
        }

        if (before == LayoutSegments[destination].common) {
            before = destination;
            destination = LayoutSegmentsLive[destination].needle;
            continue;
        }
        if (before == LayoutSegmentsLive[destination].needle) {
            before = destination;
            destination = LayoutSegments[destination].common;
            continue;
        }
        return "Cannot allow a movement to an unaligned switch";
    }
    if (destination >= 0) *exit = LayoutSegments[destination].id;
    else *exit = 0;
    return 0;
}

