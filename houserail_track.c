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
 * typedef void DetectionListener (const struct TrackRange *area,
 *                                 int occupied,
 *                                 long long timestamp);
 *
 * DetectionListener *houserail_track_subscribe (DetectionListener *listener);
 *
 *    Subscribe to track detection changes. This returns the previous listener
 *    as a way to chain listeners. It is up to the caller to maintain that
 *    chain. That previous listener might be null, i.e. no previous listener.
 *
 *    NOTE: the exact location is (line, lowpost, highpost). The segment
 *    parameter is a pre-calculated accelerator. The line parameter could
 *    be needed if the segment is part of an interlocking (more than one
 *    branch in that segment).
 *
 * void houserail_track_input (const char *name,
 *                             long long timestamp, const char *state);
 *
 *     Update track detection based on detector input. This is a listener
 *     to the field input changes. See the housecontrol.c module.
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
 *     Load a new configuration.
 *
 * int houserail_track_export (char *buffer, int size, const char *separator);
 *
 *     Export the current track configuration as JSON data.
 *
 * void houserail_track_background (time_t now);
 *
 *     Periodic update function.
 *
 * int houserail_track_civil (const struct TrackLocation *point, int direction);
 *
 *     Return the civil speed limit applicable to the specified location in
 *     the specified direction.
 *
 *     If the direction is 0 (i.e. none), the limit is the civil speed for
 *     the segment at that location. Otherwise, the limit is the smallest of
 *     the civil speed for the segment at this location and the civil speed
 *     on the approaching segment in that direction.
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
 *     Update a switch position. This is designed to be used as a listener
 *     or through a web request. Return 0 on success, an error message on
 *     failure.
 *
 * const char *houserail_track_signal (const char *name, const char *state);
 *
 *     Update a signal state. This is designed to be used as a listener
 *     or through a web request. Return 0 on success, an error message on
 *     failure.
 *
 * LIMITATIONS:
 *
 * This design is limited to 256 segments for now. To remove this restriction,
 * change echttp_hash.[hc] to allow the caller to set the size of the hash.
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_hash.h>

#include <houseconfig.h>

#include "houserail_scout.h"
#include "houserail_track.h"

static int TestMode = 0;
#define DEBUG if (TestMode || echttp_isdebug()) printf

static int SwitchReverseSpeed = 20; // TBD: make it configurable?

static DetectionListener *TrackNextListener = 0;

static const char *LayoutName = 0;
static const char *LayoutDescription = 0;

struct TrackModel {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;  // Length on the normal side.
    int reverse; // Length on the reverse side, 0 if not a switch.
    int civil;   // Civil speed limit on that track.
};

struct TrackSegment {
    const char *id;
    unsigned int signature; // Seach accelerator.

    const char *line; // The name of the line going through the normal points.
    int start;        // Starting milepost for this segment (optional).

    // The following attributes are calculated by following the linkages.

    int model;
    int next;     // Link from exit point to the next segment. -1 if none.
    int previous; // Link from entry point to the previous segment. -1 if none.

    // The following items are for switches only, valid if branch >= 0.
    int common;   // The adjacent segment connected to the common switch end
    int branch;   // The adjacent segment connected to the reverse point.
    int needle;   // The adjacent segment connected to the needle's position.

    // The following attributes are calculated by following the topology from
    // the terminal point marked as the origin.

    int low;
    int high;

    int detector; // First detector on this segment.
};

struct TrackDetector {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int segment;
    int next;     // Next detector on the same segment.
    struct TrackRange area; // RESTRICTION: a detector covers only one segment.

    // The following is the live status.
    struct {
        int occupied;
        long long timestamp;
    } live;
};

static struct TrackModel *LayoutModels = 0;
static int                LayoutModelsCount = 0;

static struct TrackSegment *LayoutSegments = 0;
static int                  LayoutSegmentsCount = 0;

static struct TrackDetector *LayoutDetectors = 0;
static int                   LayoutDetectorsCount = 0;

static echttp_hash       LayoutSegmentsHash;
static int              *LayoutSegmentsMap = 0;
static struct RangeIndex LayoutSegmentsIndex;

static echttp_hash LayoutDetectorsHash;
static int        *LayoutDetectorsMap = 0;


void houserail_track_testmode (int enabled) {
    TestMode = enabled;
}

static int houserail_track_search_model (const char *id) {

    if (!id) return -1;
    int signature = echttp_hash_signature (id);

    int i;
    for (i = 0; i < LayoutModelsCount; ++i) {
        if (LayoutModels[i].signature != signature) continue;
        if (!strcmp (LayoutModels[i].id, id)) return i;
    }
    return -1;
}

static int houserail_track_search_by_id (const char *id) {

    if (!id) return -1;

    int i = echttp_hash_find (&LayoutSegmentsHash, id);
    if ((i > 0) && (i <= LayoutSegmentsCount)) {
        return LayoutSegmentsMap[i];
    }

    // For now: if not in the hash, fallback to linear search.
    // FIXME: improve echttp_hash to support variable size.

    int signature = echttp_hash_signature (id);

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        if (LayoutSegments[i].signature != signature) continue;
        if (!strcmp (LayoutSegments[i].id, id)) return i;
    }
    return -1;
}

static int houserail_track_search_by_location (const char *line, int post) {

    return houserail_scout_inside (&LayoutSegmentsIndex, line, post);
}

static struct TrackDetector *houserail_track_search_detector (const char *id) {

    if (!id) return 0;

    int i = echttp_hash_find (&LayoutDetectorsHash, id);
    if ((i > 0) && (i <= LayoutDetectorsCount)) {
        return LayoutDetectors + LayoutDetectorsMap[i];
    }

    // For now: if not in the hash, fallback to linear search.
    // FIXME: improve echttp_hash to support variable size.

    int signature = echttp_hash_signature (id);

    for (i = 0; i < LayoutDetectorsCount; ++i) {
        struct TrackDetector *detector = LayoutDetectors + i;
        if (detector->signature != signature) continue;
        if (!detector->id) continue;
        if (!strcmp (detector->id, id)) return detector;
    }
    return 0;
}

const char *houserail_track_initialize (int argc, const char **argv) {

    houserail_scout_initialize (&LayoutSegmentsIndex, 0);
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

    printf (__FILE__ ": received input %s, state %s at %lld\n",
            name, state, timestamp);

    struct TrackDetector *detector = houserail_track_search_detector (name);
    if (!detector) return;
    if (detector->segment < 0) return;

    int occupied = strcasecmp (state, "on") ? 0 : 1;
    if (occupied == detector->live.occupied) return;
    detector->live.occupied = occupied;

    if (!TrackNextListener) return;
    TrackNextListener (&(detector->area), occupied, timestamp);
}

static int houserail_track_detector_compare (const void *a, const void *b) {

    const struct TrackDetector *detecta = (struct TrackDetector *)a;
    const struct TrackDetector *detectb = (struct TrackDetector *)b;

    int result = strcasecmp (detecta->area.line, detectb->area.line);
    if (result) return result;

    if (detecta->area.low < detectb->area.low) return -1;
    if (detecta->area.low > detectb->area.low) return 1;
    return 0;
}

const char *houserail_track_reload (void) {

    if (LayoutModels) {
       free (LayoutModels);
       LayoutModels = 0;
       LayoutModelsCount = 0;
    }
    if (LayoutSegments) {
       free (LayoutSegments);
       LayoutSegments = 0;
       LayoutSegmentsCount = 0;
    }
    if (LayoutSegmentsMap) {
       free (LayoutSegmentsMap);
       LayoutSegmentsMap = 0;
    }
    if (LayoutDetectors) {
        free (LayoutDetectors);
        LayoutDetectors= 0;
    }
    if (LayoutDetectorsMap) {
        free (LayoutDetectorsMap);
        LayoutDetectorsMap = 0;
    }
    houserail_scout_erase (&LayoutSegmentsIndex);

    LayoutName = houseconfig_string (0, ".rail.layout");
    if (!LayoutName) return "No track layout name";
    LayoutDescription = houseconfig_string (0, ".rail.description");

    // Calculate the size needed for each array.

    int track = houseconfig_object (0, ".rail.track");
    if (track < 0) return "No track topology found";

    int models = houseconfig_array (track, ".models");
    if (models < 0) return "No track models found";

    LayoutModelsCount = houseconfig_array_length (models);
    if (LayoutModelsCount <= 0) return "Empty track model list";

    int segments = houseconfig_array (track, ".segments");
    if (segments < 0) return "No track segments found";

    LayoutSegmentsCount = houseconfig_array_length (segments);
    if (LayoutSegmentsCount <= 0) return "Empty track segment list";

    int detectors = houseconfig_array (track, ".detectors");
    if (detectors < 0) return "No track detectors found";

    LayoutDetectorsCount = houseconfig_array_length (detectors);
    if (LayoutDetectorsCount <= 0) return "Empty track detectors list";

    int max = LayoutModelsCount;
    if (LayoutSegmentsCount > max) max = LayoutSegmentsCount;
    if (LayoutDetectorsCount > max) max = LayoutDetectorsCount;
    int *list = calloc (max, sizeof(int));

    DEBUG (__FILE__ ": %d models, %d segments, %d detectors\n",
           LayoutModelsCount, LayoutSegmentsCount, LayoutDetectorsCount);

    // Populate the models array.

    LayoutModels = calloc (LayoutModelsCount, sizeof(struct TrackModel));
    houseconfig_enumerate (models, list, LayoutModelsCount);

    int i;
    for (i = 0; i < LayoutModelsCount; ++i) {
        int element = list[i];
        struct TrackModel *model = LayoutModels + i;
        model->id = houseconfig_string (element, ".id");
        model->signature = echttp_hash_signature (model->id);

        model->length = houseconfig_integer (element, ".length");
        model->reverse = houseconfig_integer (element, ".reverse");
        model->civil = houseconfig_integer (element, ".civil");
        if (model->reverse > 0) {
            DEBUG (__FILE__ ": Model %s civil speed %d length %d (%d on reverse branch)\n",
                   model->id, model->civil, model->length, model->reverse);
        } else {
            DEBUG (__FILE__ ": Model %s civil speed %d length %d\n",
                   model->id, model->civil, model->length);
        }
    }

    // Populate the segments array.

    LayoutSegments = calloc (LayoutSegmentsCount, sizeof(struct TrackSegment));
    houseconfig_enumerate (segments, list, LayoutSegmentsCount);

    struct Linkage {
        const char *previous;
        const char *next;
        const char *common;
        const char *branch;
    } *temp = calloc (LayoutSegmentsCount, sizeof(struct Linkage));

    echttp_hash_reset (&LayoutSegmentsHash, 0);
    LayoutSegmentsMap = calloc (LayoutSegmentsCount+1, sizeof(int));

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        int element = list[i];
        struct TrackSegment *segment = LayoutSegments + i;
        segment->id = houseconfig_string (element, ".id");
        if (!segment->id) {
            DEBUG (__FILE__ ": Error on segment at index %d\n", i);
            return "invalid segment (no id)";
        }
        segment->signature =
            echttp_hash_signature (segment->id);

        segment->line = houseconfig_string (element, ".line");
        if (!segment->line) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "invalid segment (no line)";
        }
        const char *modelid = houseconfig_string (element, ".model");
        if (!modelid) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "invalid segment (no model)";
        }
        segment->model = houserail_track_search_model (modelid);

        if (houseconfig_present (element, ".start"))
            segment->start = houseconfig_integer (element, ".start");
        else
            segment->start = -1;
        segment->low = segment->high = -1; // To be calculated later.
        segment->detector = -1; // List will be built later.

        temp[i].previous = houseconfig_string (element, ".previous");
        temp[i].next = houseconfig_string (element, ".next");
        temp[i].common = houseconfig_string (element, ".common");
        temp[i].branch = houseconfig_string (element, ".branch");

        int index = echttp_hash_insert (&LayoutSegmentsHash, segment->id);
        if ((index > 0) && (index <= LayoutSegmentsCount))
            LayoutSegmentsMap[index] = i;
    }

    // Resolve the segment linkages

    int switchcount = 0;

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;

        // Allow the 'previous' field to be optional in the simple case:
        // same line.
        // TBD: use a faster method than linear search inside a loop..
        if (!temp[i].previous) {
            int j;
            for (j = 0; j < LayoutSegmentsCount; ++j) {
                if ((temp[j].next) &&
                    (!strcmp (temp[j].next, segment->id)) &&
                    (!strcmp (LayoutSegments[j].line, segment->line))) {
                    temp[i].previous = LayoutSegments[j].id;
                    break;
                }
            }
        }
        if ((!temp[i].previous) && (!temp[i].next)) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "isolated segment";
        }

        segment->previous = houserail_track_search_by_id (temp[i].previous);
        if ((segment->previous < 0) && temp[i].previous) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "invalid previous link";
        }
        segment->next = houserail_track_search_by_id (temp[i].next);
        if ((segment->next < 0) && temp[i].next) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "invalid previous link";
        }

        segment->branch = houserail_track_search_by_id (temp[i].branch);
        if (segment->branch >= 0) {
            // Default state of switch is 'normal'.
            segment->common = houserail_track_search_by_id (temp[i].common);
            segment->needle = (segment->common == segment->next)? segment->previous : segment->next;
            switchcount += 1;
        } else {
            segment->common = segment->needle = -1;
        }
    }
    free (temp);

    // Find the first track on each line, and follow the layout to calculate
    // the low and high post for each segment.
    // (This is not a very efficient loop. Make it better later, if needed.)
    //
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (segment->low >= 0) continue; // Already processed.
        int startpost = (segment->start > 0)?segment->start:0;
        int isstart = ((segment->start >= 0) || (segment->previous < 0));
        if (!isstart) {
           // A branch starts at the common point of a switch, if the current
           // segment starts at the switch (and not ends at the switch).
           struct TrackSegment *previous = LayoutSegments + segment->previous;
           if (previous->branch == i) {
              isstart = 1; // This starts from a switch reverse branch.
              startpost = LayoutModels[previous->model].reverse;
           }
        }
        if (isstart) {
           DEBUG (__FILE__ ": Segment %s is a starting point for line %s post %d\n",
                  segment->id, segment->line, startpost);
           segment->low = (segment->start >= 0) ? segment->start : startpost;
           segment->high = segment->low + LayoutModels[segment->model].length;

           struct TrackSegment *cursor = segment;
           int next;
           for (next = segment->next; next >= 0; next = cursor->next) {
               LayoutSegments[next].low = cursor->high;
               cursor = LayoutSegments + next;
               cursor->high = cursor->low + LayoutModels[cursor->model].length;

               // Stop when the line ends, the following segment was already
               // processed or when reaching a different line (usually a
               // switch).
               if (cursor->next < 0) break;
               if (LayoutSegments[cursor->next].low >= 0) break;
               if (strcasecmp (LayoutSegments[cursor->next].line, cursor->line))
                   break;
           }
        }
    }

    // Create the segment index to accelerate segment retrieval by location.
    //
    houserail_scout_initialize (&LayoutSegmentsIndex,
                                LayoutSegmentsCount + switchcount);
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        houserail_scout_add (&LayoutSegmentsIndex,
                             i, segment->line, segment->low, segment->high);
        DEBUG (__FILE__ ": Segment %s on %s %d to %d (between %s and %s)\n",
               segment->id, segment->line, segment->low, segment->high,
               (segment->previous >= 0)?LayoutSegments[segment->previous].id:"(none)",
               (segment->next >= 0)?LayoutSegments[segment->next].id:"(none)");
        if (segment->branch >= 0) {
            struct TrackSegment *branch = LayoutSegments + segment->branch;
            int low, high;
            int reverse = LayoutModels[segment->model].reverse;
            int branchprevious, branchnext;
            if (branch->previous == i) {
                // Increasing posts
                low = branch->low - reverse;
                high = branch->low;
                branchprevious = segment->previous;
                branchnext = segment->branch;
            } else {
                // Decreasing posts
                low = branch->high;
                high = low + reverse;
                branchprevious = segment->branch;
                branchnext = segment->next;
            }
            houserail_scout_add (&LayoutSegmentsIndex,
                                 i, branch->line, low, high);
            DEBUG (__FILE__ ": Segment %s is a switch, branch on %s %d to %d (between %s and %s)\n",
                   segment->id, branch->line, low, high,
                   LayoutSegments[branchprevious].id,
                   LayoutSegments[branchnext].id);
        }
    }
    houserail_scout_finalize (&LayoutSegmentsIndex);

    // Populate the detectors array and sort by line and (low) post.

    echttp_hash_reset (&LayoutDetectorsHash, 0);

    LayoutDetectorsMap = calloc (LayoutDetectorsCount+1, sizeof(int));
    LayoutDetectors = calloc (LayoutDetectorsCount, sizeof(struct TrackDetector));
    houseconfig_enumerate (detectors, list, LayoutDetectorsCount);

    for (i = 0; i < LayoutDetectorsCount; ++i) {
        int element = list[i];
        struct TrackDetector *detector = LayoutDetectors + i;
        detector->id = houseconfig_string (element, ".id");
        detector->signature = echttp_hash_signature (detector->id);

        detector->area.line = houseconfig_string (element, ".line");
        detector->area.segment = 0;
        detector->area.low = houseconfig_integer (element, ".low");
        detector->area.high = houseconfig_integer (element, ".high");

        detector->segment =
            houserail_track_search_by_location (detector->area.line, detector->area.low);
        if (detector->segment < 0) {
            DEBUG (__FILE__ ": Invalid location for detector %s\n", detector->id);
            continue;
        }
        struct TrackSegment *segment = LayoutSegments + detector->segment;
        DEBUG (__FILE__ ": Detector %s is on segment %s covers %s %d to %d\n",
               detector->id, segment->id,
               detector->area.line, detector->area.low, detector->area.high);
        detector->next = segment->detector;
        segment->detector = i;
        detector->area.segment = segment->id;

        detector->live.occupied = 0;
        detector->live.timestamp = 0;

        int index = echttp_hash_insert (&LayoutDetectorsHash, detector->id);
        if ((index > 0) && (index <= LayoutDetectorsCount))
            LayoutDetectorsMap[index] = i;
    }

    qsort (LayoutDetectors, LayoutDetectorsCount, sizeof(struct TrackDetector),
           houserail_track_detector_compare);

    return 0;
}

int houserail_track_export (char *buffer, int size, const char *separator) {

    if (!LayoutName) return 0; // No track layout was loaded.

    int cursor = snprintf (buffer, size,
                           "%s\"layout\":\"%s\"", separator, LayoutName);
    if (cursor >= size) goto overflow;
    if (LayoutDescription) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"description\":\"%s\"", LayoutDescription);
        if (cursor >= size) goto overflow;
    }
    cursor += snprintf (buffer+cursor, size-cursor, ",\"track\":{");
    if (cursor >= size) goto overflow;
    int preamble = cursor;

    // Populate the models array.

    const char *prefix = "\"models\":[";
    int start = cursor;
    int i;
    for (i = 0; i < LayoutModelsCount; ++i) {
        struct TrackModel *model = LayoutModels + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\""
                                ",\"length\":%d,\"reverse\":%d,\"civil\":%d}",
                            prefix, model->id, model->length,
                                    model->reverse, model->civil);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    if (cursor > start) {
        cursor += snprintf (buffer+cursor, size-cursor, "]");
        if (cursor >= size) goto overflow;
    }

    // Populate the segments array.

    prefix = (cursor > preamble)?",\"segments\":[":"\"segments\":[";
    start = cursor;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"model\":\"%s\","
                                "\"line\":\"%s\"",
                            prefix,
                            segment->id, LayoutModels[segment->model].id,
                            segment->line);
        if (cursor >= size) goto overflow;
        if (segment->start >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"start\":%d", segment->start);
            if (cursor >= size) goto overflow;
        }
        if (segment->previous >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"previous\":\"%s\"",
                                LayoutSegments[segment->previous].id);
            if (cursor >= size) goto overflow;
        }
        if (segment->next >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"next\":\"%s\"",
                                LayoutSegments[segment->next].id);
            if (cursor >= size) goto overflow;
        }
        if (segment->branch >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"branch\":\"%s\"",
                                LayoutSegments[segment->branch].id);
            if (cursor >= size) goto overflow;
        }
        if (segment->common >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"common\":\"%s\"",
                                LayoutSegments[segment->common].id);
            if (cursor >= size) goto overflow;
        }
        cursor += snprintf (buffer+cursor, size-cursor, "}");
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    if (cursor > start) {
        cursor += snprintf (buffer+cursor, size-cursor, "]");
        if (cursor >= size) goto overflow;
    }

    // Populate the detectors array.

    prefix = (cursor > preamble)?",\"detectors\":[":"\"detectors\":[";
    start = cursor;
    for (i = 0; i < LayoutDetectorsCount; ++i) {
        struct TrackDetector *detector = LayoutDetectors + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"line\":\"%s\","
                                "\"low\":%d,\"high\":%d}",
                            prefix,
                            detector->id, detector->area.line,
                            detector->area.low, detector->area.high);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    if (cursor > start) {
        cursor += snprintf (buffer+cursor, size-cursor, "]");
        if (cursor >= size) goto overflow;
    }
    cursor += snprintf (buffer+cursor, size-cursor, "}");

    return cursor;

overflow:
    return 0;
}

static int houserail_track_status_track (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"track\":[";

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        const char *state = "off";
        int j;
        for (j = segment->detector; j >= 0; j = LayoutDetectors[j].next) {
            if (LayoutDetectors[j].live.occupied) {
                state = "on";
                break;
            }
        }
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s[\"%s\",\"%s\"]", prefix, segment->id, state);
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
        struct TrackDetector *detector = LayoutDetectors + i;
        const char *state = detector->live.occupied?"on":"off";
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s[\"%s\",\"%s\"]", prefix, detector->id, state);
        prefix = ",";
    }
    if (cursor > 0) cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;
}

static int houserail_track_status_switch (char *buffer, int size) {

    int cursor = 0;
    const char *prefix = ",\"switch\":[";

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (segment->branch >= 0) {
            const char *state = "invalid";
            if (segment->needle == segment->branch)
                state = "reverse";
            if (segment->needle == segment->next)
                state = "normal";
            else if (segment->needle == segment->previous)
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

    int cursor = houserail_track_status_track (buffer, size);
    cursor += houserail_track_status_switch (buffer+cursor, size-cursor);
    cursor += houserail_track_detectors (buffer+cursor, size-cursor);
    return cursor;
}


void houserail_track_background (time_t now) {
    // TBD: background work needed?
}

const char *houserail_track_segment (const struct TrackLocation *point,
                                     int direction) {

    if (point->segment) return point->segment;
    int index = houserail_track_search_by_location (point->line, point->post);
    if (index < 0) return 0;

    // If the point is at the limit between two segments, each of these two
    // segments would be a valid response. The direction parameter is used
    // to indicate which of the two segments is preferred.

    if (direction) {
        struct TrackSegment *segment = LayoutSegments + index;
        if (direction > 0) {
            if ((segment->low == point->post) && (segment->previous >= 0)) {
                DEBUG (__FILE__ ": On the low edge of segment %s\n", segment->id);
                int alternative = segment->previous;
                segment = LayoutSegments + alternative;
                DEBUG (__FILE__ ": Trying segment %s\n", segment->id);
                if ((segment->high == point->post) &&
                    (!strcmp (segment->line, point->line))) {
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
                    (!strcmp (segment->line, point->line))) {
                    index = alternative;
                }
            }
        }
    }
    return LayoutSegments[index].id;
}

static int houserail_track_locate (const struct TrackLocation *point) {

    DEBUG (__FILE__ ": houserail_track_locate(): use location %s.%d\n", point->line, point->post);
    return houserail_track_search_by_location (point->line, point->post);
}

int houserail_track_vicinity (struct TrackLocation *point,
                              const char *id, int direction) {

    int low = -1;
    int high = -1;
    point->line = 0;

    int index = houserail_track_search_by_id (id);
    if (index > 0) {
        struct TrackSegment *segment = LayoutSegments + index;
        point->line = segment->line;
        point->segment = segment->id;
        low = segment->low;
        high = segment->high;
    } else {
        struct TrackDetector *detector = houserail_track_search_detector (id);
        if (detector) {
           point->line = detector->area.line;
           point->segment = LayoutSegments[detector->segment].id;
           low = detector->area.low;
           high = detector->area.high;
        }
    }
    if (!point->line) return 0; // Not found.

    if (! direction) {
       // Not moving: choose a point in the middle.
       point->post = (high + low) / 2;
       return 1;
    }

    // Choose the limit according to the direction of travel.
    if (low > high) {
        int temp = low;
        low = high;
        high = temp;
    }
    point->post = (direction > 0)? low : high;
    return 1;
}

int houserail_track_civil (const struct TrackLocation *point, int direction) {

    int index = houserail_track_locate (point);
    if (index < 0) {
        DEBUG (__FILE__ ": invalid location %s.%d\n", point->line, point->post);
        return 0; // Stop whenever there is any doubt.
    }

    struct TrackSegment *segment = LayoutSegments + index;
    int speed1 = LayoutModels[segment->model].civil;
    DEBUG (__FILE__ ": Consider civil speed %d for segment %s (model %s)\n",
           speed1, segment->id, LayoutModels[segment->model].id);
    if ((segment->branch >= 0) && (segment->needle == segment->branch)) {
       speed1 = SwitchReverseSpeed;
       DEBUG (__FILE__ ": use branch speed %d instead for switch %s in reverse state",
              speed1, segment->id);
    }
    if (!direction) return speed1;

    index = (direction > 0) ? segment->next : segment->previous;
    segment = LayoutSegments + index;
    int speed2 = LayoutModels[segment->model].civil;
    DEBUG (__FILE__ ": Consider civil speed %d from segment %s (model %s)\n",
           speed2, segment->id, LayoutModels[segment->model].id);
    if (speed1 < speed2) return speed1;
    return speed2;
}

// Retrieve the track range covered by the specified segment.
// This function handles switches.
//
void houserail_track_limits (const char *line, int direction,
                             struct TrackSegment *segment,
                             struct TrackRange *range) {

    // Consider the segment's 'normal' range as the default.
    range->line = segment->line;
    range->low = segment->low;
    range->high = segment->high;

    if (segment->branch < 0) return; // No ambiguity: straight segment.

    struct TrackSegment *branch = LayoutSegments + segment->branch;

    // What is the geometry of the switch: increasing or decreasing posts?
    int geometry = (segment->common == segment->previous)?1:-1;

    int onbranch = 0;
    if (direction == geometry) {
        // Follow the needle on a divergent switch.
        if (segment->needle == segment->branch) onbranch = 1;
    } else {
        // Does this come from the branch of a convergent switch?
        if (!strcmp (line, branch->line)) onbranch = 1;
    }
    if (onbranch) {
        range->line = branch->line;
        struct TrackModel *model = LayoutModels + segment->model;
        if (geometry > 0) {
            range->low = branch->low - model->reverse;
            range->high = branch->low;
        } else {
            range->low = branch->high;
            range->high = branch->high + model->reverse;
        }
    }
}

// Make one step to the next segment. This handles switches.
//
static int houserail_track_step (struct TrackSegment *segment, int direction) {

    // The default is to follow the 'normal' direction
    //
    int next = (direction > 0) ? segment->next : segment->previous;
    if (segment->branch < 0) return next; // No ambiguity.

    // The segment is a switch. What is it geometry compare to the direction?
    //
    if (next < 0) return next; // Switches to nowhere don't exist.
    int geometry = (segment->common == segment->previous)?1:-1;
    if (geometry == direction) { // Follow the needle on a divergent switch
        return segment->needle;
    }
    return next;
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
    struct TrackSegment *segment = LayoutSegments + index;

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
            (!strcmp (limit2->line, current.line))) {

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
        struct TrackSegment *next = LayoutSegments + index;

        struct TrackRange upcoming;
        houserail_track_limits (current.line, direction, next, &upcoming);
        DEBUG (__FILE__ ": Walking to segment %s (%s %d to %d)\n", next->id, upcoming.line, upcoming.low, upcoming.high);

        int join2 = (direction > 0)? upcoming.low : upcoming.high;
        DEBUG (__FILE__ ": Segments %s and %s join at %s.%d and %s.%d\n",
               segment->id, next->id, current.line, join1, upcoming.line, join2);

        if ((join1 != join2) || strcasecmp (line, upcoming.line)) {

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

    int index = houserail_track_search_by_id (name);
    if (index < 0) return "Invalid name";

    struct TrackSegment *segment = LayoutSegments + index;
    if (segment->branch < 0) return "Not a switch";

    if (!strcasecmp (state, "normal")) {
        segment->needle =
            (segment->common == segment->next) ? segment->previous : segment->next;
    } else if (!strcasecmp (state, "reverse")) {
        segment->needle = segment->branch;
    } else {
        segment->needle = -1;
    }
    return 0;
}

const char *houserail_track_signal (const char *name, const char *state) {

    return 0; // TBD: add signal to the topology database, stop trains on red.
}

