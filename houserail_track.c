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
 * houserail_track.c - Track topology and detectection.
 *
 * SYNOPSYS:
 *
 * const char *houserail_track_initialize (int argc, const char **argv);
 *
 * typedef void DetectionListener (const char *name, long long timestamp,
 *                                 int occupied, int lowpost, int highpost);
 *
 * DetectionListener *houserail_track_subscribe (DetectionListener *listener);
 *
 *    Subscribe to track detection changes. This returns the previous listener
 *    as a way to chain listeners. It is up to the caller to maintain that
 *    chain. That previous listener might be null, i.e. no previous listener.
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
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_hash.h>

#include <houseconfig.h>

#include "houserail_track.h"

#define DEBUG if (echttp_isdebug()) printf

static DetectionListener *TrackNextListener = 0;

struct TrackModel {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;  // Length on the normal side.
    int reverse; // Length on the reverse side, 0 if not a switch.
};

struct TrackSegment {
    const char *id;
    unsigned int signature; // Seach accelerator.

    const char *line; // The name of the line going through the normal points.

    // The following attributes are calculated by following the linkages.

    int model;
    int next;     // Link from exit point to the next segment. -1 if none.
    int previous; // Link from entry point to the previous segment. -1 if none.
    int branch;   // Link from reverse point to the next segment (switch only).

    // The following attributes are calculated by following the topology from
    // the terminal point marked as the origin.

    int entry;   // Post value at the entry end (common end for a switch).
    int exit;    // Post value at the exit end (normal exit for a switch).
    int reverse; // Post value at the reverse exit, or -1 is not a switch.

    int low;
    int high;

    int detector; // First detector on this segment.
};

struct TrackDetector {
    const char *id;
    unsigned int signature; // Seach accelerator.

    const char *line;
    int low;     // Low post limit of the detection zone.
    int high;    // High post limit of the detection zone.

    // The following attributes are calculated.
    int segment; // RESTRICTION: a detector can cover at most one segment.

    // The following is the live status.
    int occupied;
    long long timestamp;
};

static struct TrackModel *LayoutModels = 0;
static int                LayoutModelsCount = 0;

static struct TrackSegment *LayoutSegments = 0;
static int                  LayoutSegmentsCount = 0;

static struct TrackDetector *LayoutDetectors = 0;
static int                   LayoutDetectorsCount = 0;

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
    int signature = echttp_hash_signature (id);

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        if (LayoutSegments[i].signature != signature) continue;
        if (!strcmp (LayoutSegments[i].id, id)) return i;
    }
    return -1;
}

static int houserail_track_search_by_location (const char *line, int post) {

    if (!line) return -1;

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (post < segment->low) continue;
        if (post >= segment->high) continue;
        if (strcasecmp (line, segment->line)) continue;
        return i;
    }
    return -1;
}

static struct TrackDetector *houserail_track_search_detector (const char *id) {

    if (!id) return 0;
    int signature = echttp_hash_signature (id);

    int i;
    for (i = 0; i < LayoutDetectorsCount; ++i) {
        struct TrackDetector *detector = LayoutDetectors + i;
        if (detector->signature != signature) continue;
        if (!detector->id) continue;
        if (!strcmp (detector->id, id)) return detector;
    }
    return 0;
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

    printf (__FILE__ ": received input %s, state %s at %lld\n",
            name, state, timestamp);
    // TBD
    struct TrackDetector *detector = houserail_track_search_detector (name);
    if (!detector) return;

    int occupied = strcasecmp (state, "on") ? 0 : 1;
    if (!TrackNextListener) return;

    TrackNextListener (detector->line, timestamp,
                       occupied, detector->low, detector->high);
}

static int houserail_track_detector_compare (const void *a, const void *b) {

    const struct TrackDetector *detecta = (struct TrackDetector *)a;
    const struct TrackDetector *detectb = (struct TrackDetector *)b;

    int result = strcasecmp (detecta->line, detectb->line);
    if (result) return result;

    if (detecta->low < detectb->low) return -1;
    if (detecta->low > detectb->low) return 1;
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
    }

    // Populate the segments array.

    LayoutSegments = calloc (LayoutSegmentsCount, sizeof(struct TrackSegment));
    houseconfig_enumerate (segments, list, LayoutSegmentsCount);

    struct Linkage {
        const char *previous;
        const char *next;
        const char *reverse;
    } *temp = calloc (LayoutSegmentsCount, sizeof(struct Linkage));

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        int element = list[i];
        struct TrackSegment *segment = LayoutSegments + i;
        segment->id = houseconfig_string (element, ".id");
        segment->signature =
            echttp_hash_signature (segment->id);

        segment->line = houseconfig_string (element, ".line");
        const char *modelid = houseconfig_string (element, ".model");
        segment->model = houserail_track_search_model (modelid);

        segment->low = segment->high = -1; // To be calculated later.

        temp[i].previous = houseconfig_string (element, ".previous");
        temp[i].next = houseconfig_string (element, ".next");
        temp[i].reverse = houseconfig_string (element, ".reverse");
    }

    // Resolve the segment linkages

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        segment->previous = houserail_track_search_by_id (temp[i].previous);
        segment->next = houserail_track_search_by_id (temp[i].next);
        segment->reverse = houserail_track_search_by_id (temp[i].reverse);
    }
    free (temp);

    // Find the first track on each line, and follow the layout to calculate
    // the low and high post for each segment.
    // (This is not a very efficient loop. Make it better later, if needed.)
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (segment->low >= 0) continue; // Already processed.
        int isstart = (segment->previous < 0);
        if (!isstart) {
           if (LayoutSegments[segment->previous].reverse == i)
              isstart = 1; // This is the exit of a switch reverse branch.
        }
        if (isstart) {
           segment->low = 0;
           segment->high = LayoutModels[segment->model].length;
           struct TrackSegment *cursor = segment;
           int next;
           for (next = segment->next; next >= 0; next = cursor->next) {
               LayoutSegments[next].low = cursor->high;
               cursor = LayoutSegments + next;
               cursor->high = cursor->low + LayoutModels[cursor->model].length;

               // Stop when the following segment was already processed, or
               // when reaching a different line (typically a switch).
               //
               if (LayoutSegments[cursor->next].low >= 0) break;
               if (strcasecmp (LayoutSegments[cursor->next].line, cursor->line))
                   break;
           }
        }
    }

    // Populate the detectors array and sort by line and (low) post.

    LayoutDetectors = calloc (LayoutDetectorsCount, sizeof(struct TrackDetector));
    houseconfig_enumerate (detectors, list, LayoutDetectorsCount);

    for (i = 0; i < LayoutDetectorsCount; ++i) {
        int element = list[i];
        struct TrackDetector *detector = LayoutDetectors + i;
        detector->id = houseconfig_string (element, ".id");
        detector->signature = echttp_hash_signature (detector->id);

        detector->line = houseconfig_string (element, ".line");
        detector->low = houseconfig_integer (element, ".low");
        detector->high = houseconfig_integer (element, ".high");

        detector->segment =
            houserail_track_search_by_location (detector->line, detector->low);

        detector->occupied = 0;
        detector->timestamp = 0;
    }

    qsort (LayoutDetectors, LayoutDetectorsCount, sizeof(struct TrackDetector),
           houserail_track_detector_compare);

    return 0;
}

int houserail_track_export (char *buffer, int size, const char *separator) {
    return 0; // TBD
}

int houserail_track_status (char *buffer, int size) {
    return 0;
}


void houserail_track_background (time_t now) {
}

