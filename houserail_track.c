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
 * The functions below are used to move a train along the track. The path
 * followed depend on the position of the switches.
 *
 * int houserail_track_covered (const struct TrackRange *area,
 *                              const struct TrackLocation *limit1,
 *                              const struct TrackLocation *limit2,
 *                              int direction);
 *
 *     Return 1 if the location is within the two limits provided.
 *     This is used to check if a train is present over a specific area.
 *
 * int houserail_track_distance (const struct TrackLocation *point1,
 *                               const struct TrackLocation *point2,
 *                               int direction, int max);
 *
 *     Return the distance a train would have to move by between the two
 *     track points provided.
 *
 * int houserail_track_move (struct TrackLocation *location,
 *                           int distance, int direction);
 *
 *     Move a train location by the specified distance along the tracks.
 *     This is dependant on the state of the switches.
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
        const char *common;
        const char *branch;
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

        segment->start = houseconfig_integer (element, ".start");
        segment->low = segment->high = -1; // To be calculated later.

        temp[i].previous = houseconfig_string (element, ".previous");
        temp[i].next = houseconfig_string (element, ".next");
        temp[i].common = houseconfig_string (element, ".common");
        temp[i].branch = houseconfig_string (element, ".branch");
    }

    // Resolve the segment linkages

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        segment->previous = houserail_track_search_by_id (temp[i].previous);
        segment->next = houserail_track_search_by_id (temp[i].next);
        segment->branch = houserail_track_search_by_id (temp[i].branch);
        if (segment->branch >= 0) {
            segment->common = houserail_track_search_by_id (temp[i].common);
            segment->needle = (segment->common == segment->next)? segment->previous : segment->next;
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

    // Populate the detectors array and sort by line and (low) post.

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
        detector->area.segment = LayoutSegments[detector->segment].id;

        detector->live.occupied = 0;
        detector->live.timestamp = 0;
    }

    qsort (LayoutDetectors, LayoutDetectorsCount, sizeof(struct TrackDetector),
           houserail_track_detector_compare);

    return 0;
}

int houserail_track_export (char *buffer, int size, const char *separator) {
    return 0; // TBD
}

int houserail_track_status (char *buffer, int size) {
    return 0; // TBD
}


void houserail_track_background (time_t now) {
    // TBD
}

static int houserail_track_locate (const struct TrackLocation *point) {

    if (point->segment)
        return houserail_track_search_by_id (point->segment);

    return houserail_track_search_by_location (point->line, point->post);
}

static int houserail_track_walk (struct TrackSegment *segment, int direction) {
    return (direction > 0) ? segment->next : segment->previous;
}

static int houserail_track_path (struct TrackRange *path, int size,
                                 const struct TrackLocation *limit1,
                                 const struct TrackLocation *limit2,
                                 int direction) {

    if (strcasecmp (limit1->line, limit2->line) == 0) {
        // The two limits are on the same line: there is only one entry
        // in the path. The direction does not matter in this case.
        path[0].line = limit2->line;
        path[0].segment = 0;
        path[0].low  = limit1->post;
        path[0].high = limit2->post;
        return 1;
    }

    // The two limits are not on the same line: walk the tracks from one
    // limit until we meet the other limit.
    //
    int index = houserail_track_locate (limit1);
    if (index < 0) return 0;
    struct TrackSegment *segment = LayoutSegments + index;

    int cursor = 0;
    const char *line = limit1->line;
    path[0].line = limit1->line;
    path[0].segment = segment->id;
    path[0].low = limit1->post;

    int nextsegment = houserail_track_walk (segment, direction);
    while (nextsegment >= 0) {
        struct TrackSegment *next = LayoutSegments + nextsegment;

        if ((next->branch >= 0) && (LayoutSegments+next->common == segment)) {

           // This is the entry to a switch: follow the needle
           if (next->needle == next->branch) {
               line = LayoutSegments[next->branch].line;
               path[cursor++].high = (direction > 0) ? segment->low : segment->high;
               if (cursor >= size) return 0; // Overflow.

               path[cursor].line = line;
               path[cursor].segment = next->id;
               path[cursor].low = (direction > 0) ? next->low : next->high;

               next = LayoutSegments + next->branch; // Pass that switch.
           }
        }
        if (strcasecmp (line, next->line)) {

           // The name of the line changed, without taking a branch.
           line = next->line;
           path[cursor++].high = (direction > 0) ? segment->low : segment->high;
           if (cursor >= size) return 0; // Overflow.

           path[cursor].line = line;
           path[cursor].segment = next->id;
           path[cursor].low = next->low;
        }
        if (strcasecmp (line, limit2->line) == 0) break; // End of path

        segment = next;
        nextsegment = houserail_track_walk (segment, direction);
    }
    if (nextsegment < 0) return 0; // Could not find the other end.

    path[cursor++].high = limit2->post;
    return cursor;
}

static int houserail_track_overlap (const struct TrackRange *area,
                                    const struct TrackRange *limits) {

    if (strcasecmp (area->line, limits->line)) return 0;

    // Do not assume that the range's posts are properly sorted.
    int high = (limits->high >= limits->low)? limits->high : limits->low;
    if (area->low >= high) return 0;
    int low = (limits->high >= limits->low)? limits->low : limits->high;
    if (area->high <= low) return 0;

    return 1;
}

int houserail_track_covered (const struct TrackRange *area,
                             const struct TrackLocation *limit1,
                             const struct TrackLocation *limit2,
                             int direction) {

   // Retrieve all the line portions between the two limits. Then check
   // if there is an overlap with any of these portions.
   //
   struct TrackRange path[16];
   int count = houserail_track_path (path, 16, limit1, limit2, direction);
   int i;
   for (i = 0; i < count; ++i) {
       if (houserail_track_overlap (area, path+i)) return 1;
   }
   return 0;
}

int houserail_track_distance (const struct TrackLocation *point1,
                              const struct TrackLocation *point2,
                              int direction, int max) {

   struct TrackRange path[16];
   int count = houserail_track_path (path, 16, point1, point2, direction);
   if (count <= 0) return -1;

   int distance = 0;
   int i;
   for (i = 0; i < count; ++i) {
       int delta = path[i].high - path[i].low;
       if (delta >= 0) distance += delta;
       else            distance -= delta;
   }
   return distance;
}

int houserail_track_move (struct TrackLocation *location,
                          int distance, int direction) {
   return 1; // TBD
}

