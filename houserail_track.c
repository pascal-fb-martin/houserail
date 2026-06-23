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
 *     as a train would.
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
 * const char *houserail_track_segment (const struct TrackLocation *point);
 *
 *     Return the name of the segment where the specified point is located.
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

#define DEBUG if (echttp_isdebug()) printf

static DetectionListener *TrackNextListener = 0;

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

    // Calculate the size needed for each array.

    int models = houseconfig_array (0, ".track.models");
    if (models < 0) return "No track models found";

    LayoutModelsCount = houseconfig_array_length (models);
    if (LayoutModelsCount <= 0) return "Empty track model list";

    int segments = houseconfig_array (0, ".track.segments");
    if (segments < 0) return "No track segments found";

    LayoutSegmentsCount = houseconfig_array_length (segments);
    if (LayoutSegmentsCount <= 0) return "Empty track segment list";

    int detectors = houseconfig_array (0, ".track.detectors");
    if (detectors < 0) return "No track detectors found";

    LayoutDetectorsCount = houseconfig_array_length (detectors);
    if (LayoutDetectorsCount <= 0) return "Empty track detectors list";

    int max = LayoutModelsCount;
    if (LayoutSegmentsCount > max) max = LayoutSegmentsCount;
    if (LayoutDetectorsCount > max) max = LayoutDetectorsCount;
    int *list = calloc (max, sizeof(int));

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
        segment->signature =
            echttp_hash_signature (segment->id);

        segment->line = houseconfig_string (element, ".line");
        const char *modelid = houseconfig_string (element, ".model");
        segment->model = houserail_track_search_model (modelid);

        segment->start = houseconfig_integer (element, ".start");
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
        segment->previous = houserail_track_search_by_id (temp[i].previous);
        segment->next = houserail_track_search_by_id (temp[i].next);
        segment->branch = houserail_track_search_by_id (temp[i].branch);
        if (segment->branch >= 0) {
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
        int startpost = 0;
        int isstart = (segment->previous < 0);
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
           segment->low = (segment->start > 0) ? segment->start : startpost;
           segment->high = segment->low + LayoutModels[segment->model].length;
           struct TrackSegment *cursor = segment;
           int next;
           for (next = segment->next; next >= 0; next = cursor->next) {
               LayoutSegments[next].low = cursor->high;
               cursor = LayoutSegments + next;
               cursor->high = cursor->low + LayoutModels[cursor->model].length;

               // Stop when the following segment was already processed, or
               // when reaching a different line (usually a switch).
               //
               if ((cursor->next >= 0) &&
                   (LayoutSegments[cursor->next].low >= 0)) break;
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
        if (segment->branch >= 0) {
            const char *line = LayoutSegments[segment->branch].line;
            houserail_scout_add (&LayoutSegmentsIndex,
                                 i, line, segment->low, segment->high);
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
        if (detector->segment < 0) continue;
        struct TrackSegment *segment = LayoutSegments + detector->segment;
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

    int cursor = snprintf (buffer, size, "%s\"track\":{", separator);
    if (cursor >= size) goto overflow;

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

    prefix = "\"segments\":[";
    start = cursor;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"model\":\"%s\","
                                "\"line\":\"%s\",\"start\":%d",
                            prefix,
                            segment->id, LayoutModels[segment->model].id,
                            segment->line, segment->start);
        if (cursor >= size) goto overflow;
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

    prefix = "\"detectors\":[";
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

static int houserail_track_status_detector (char *buffer, int size) {

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
    cursor += houserail_track_status_detector (buffer+cursor, size-cursor);
    return cursor;
}


void houserail_track_background (time_t now) {
    // TBD: background work needed?
}

const char *houserail_track_segment (const struct TrackLocation *point) {

    if (point->segment) return point->segment;
    int segment = houserail_track_search_by_location (point->line, point->post);
    if (segment < 0) return 0;
    return LayoutSegments[segment].id;
}

static int houserail_track_locate (const struct TrackLocation *point) {

    if (point->segment)
        return houserail_track_search_by_id (point->segment);

    return houserail_track_search_by_location (point->line, point->post);
}

int houserail_track_vicinity (struct TrackLocation *point,
                              const char *id, int direction) {

    int index = houserail_track_search_by_id (id);
    if (index > 0) {
        struct TrackSegment *segment = LayoutSegments + index;
        point->line = segment->line;
        point->segment = segment->id;
        point->post = (segment->high + segment->low) / 2;
        return 1;
    }
    struct TrackDetector *detector = houserail_track_search_detector (id);
    if (detector) {
        point->line = detector->area.line;
        point->segment = 0;
        int low = detector->area.low;
        int high = detector->area.high;
        if (low > high) {
           low = high;
           high = detector->area.low;
        }
        // Just take a point close by and outside the detection area.
        point->post = (direction >= 0)? low - 1 : high + 1;
        return 1;
    }
    return 0;
}

int houserail_track_civil (const struct TrackLocation *point, int direction) {

    int segment = houserail_track_locate (point);
    if (segment < 0) return 0; // Stop whenever there is any doubt.

    int speed1 = LayoutModels[LayoutSegments[segment].model].civil;
    if (!direction) return speed1;

    segment = (direction > 0) ? LayoutSegments[segment].next
                              : LayoutSegments[segment].previous;
    int speed2 = LayoutModels[LayoutSegments[segment].model].civil;
    if (speed1 < speed2) return speed1;
    return speed2;
}

static int houserail_track_step (struct TrackSegment *segment, int direction) {
    return (direction > 0) ? segment->next : segment->previous;
}

int houserail_track_walk (struct TrackRange *path, int size,
                          const struct TrackLocation *limit1,
                          const struct TrackLocation *limit2,
                          int direction, int max) {

    if ((!limit2) && (!max)) return 0; // Must have at least one end criteria.

    if (limit2 && strcasecmp (limit1->line, limit2->line) == 0) {
        // The two limits are on the same line: there is only one section
        // in the path.
        int end = limit2->post;
        if ((max > 0) && (abs(end - limit1->post) > max)) {
            end = (direction > 0) ? limit1->post + max : limit1->post - max;
        }
        path[0].line = limit2->line;
        path[0].segment = 0;
        path[0].low  = limit1->post;
        path[0].high = end;
        return 1;
    }

    // Walk the tracks from one limit until we meet the other limit, the
    // max distance or the end of the rails.
    //
    int index = houserail_track_locate (limit1);
    if (index < 0) return 0;
    struct TrackSegment *segment = LayoutSegments + index;

    int cursor = 0;
    const char *line = limit1->line;
    path[0].line = limit1->line;
    path[0].segment = segment->id;
    path[0].low = limit1->post;

    int distance = 0;

    int nextsegment = houserail_track_step (segment, direction);
    while (nextsegment >= 0) {
        struct TrackSegment *next = LayoutSegments + nextsegment;

        if ((next->branch >= 0) && (LayoutSegments+next->common == segment)) {

           // This is the entry to a switch: follow the needle
           if (next->needle == next->branch) {
               struct TrackModel *model = LayoutModels + next->model;
               struct TrackSegment *branch = LayoutSegments + next->branch;
               line = branch->line;

               if (max > 0) {
                   distance += model->reverse;
                   if (distance > max) goto toofar;
               }

               // The current section ends at the exit of the segment before
               // the switch.
               path[cursor++].high =
                   (direction > 0) ? segment->low : segment->high;
               if (cursor >= size) return 0; // Overflow.

               // The next section starts at the entry of the switch, but
               // refer to the branch line.
               path[cursor].line = line;
               path[cursor].segment = next->id;
               path[cursor].low = (direction > 0)?branch->low-model->reverse
                                                 :branch->high+model->reverse;

               next = LayoutSegments + next->branch; // Pass that switch.
           }
        }
        if (strcasecmp (line, next->line)) {

           // The name of the line changed: new section.
           line = next->line;
           int post = (direction > 0) ? segment->low : segment->high;

           if (max > 0) {
               distance += abs (post - path[cursor].low);
               if (distance > max) goto toofar;
           }

           path[cursor++].high = post;
           if (cursor >= size) return 0; // Overflow.

           path[cursor].line = line;
           path[cursor].segment = next->id;
           path[cursor].low = next->low;
        }
        if (strcasecmp (line, limit2->line) == 0) break; // End of path

        segment = next;
        nextsegment = houserail_track_step (segment, direction);
    }
    if (nextsegment < 0) return 0; // Could not find the other end.
    if (!limit2) return 0; // Not possible: while did it break the loop?

    path[cursor++].high = limit2->post;
    return cursor;

toofar:

    int left = distance - max;
    path[cursor].high =
      (direction > 0) ? path[cursor].low + left : path[cursor].low - left;
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
       int delta = path[i].high - path[i].low;
       if (delta >= 0) distance += delta;
       else            distance -= delta;
       if (distance > max) return -1;
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

