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
 * houserail_display.c - Build the track topology display.
 *
 * SYNOPSYS:
 *
 * layoutdisplay [options..] <file>
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

#include "houserail_catalog.h"
#include "houserail_scout.h"
#include "houserail_track.h"  // Only to get data structures.

static int TestMode = 0;

#define DEBUG if (TestMode) printf

static const char *LayoutName = 0;
static const char *LayoutDescription = 0;

#define PRECISION 1000  // Goal is millimeter precision on the display.

static int TrackPostDistance = PRECISION;

struct DisplayLocation {

    int x;
    int y;
    int angle;
};

struct DisplayShape {

    int arc;      // 0 means straight.
    int radius;
    int straight; // Physical length of the normal side. For switch only.
};

struct TrackModel {

    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;  // Length on the normal side.
    int reverse; // Length on the reverse side, 0 if not a switch.
    int civil;   // Civil speed limit on that track.

    struct DisplayShape shape;
};

struct TrackSegment {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

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

    struct DisplayShape shape;

    // The following attributes are calculated by following the topology from
    // the terminal point marked as the origin.
    int low;
    int high;

    int detector; // First detector on this segment;

    int done;
    struct DisplayLocation origin; // Can be explicit in the layout, too
    struct DisplayLocation end;
    struct DisplayLocation reverse; // If a switch.
};

struct TrackDetector {

    const char *id;
    unsigned int signature; // Seach accelerator.

    int segment;
    int next;     // Next detector on the same segment.
    struct TrackRange area; // RESTRICTION: a detector covers only one segment.
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
        if (strsame (LayoutModels[i].id, id)) return i;
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
        if (strsame (LayoutSegments[i].id, id)) return i;
    }
    return -1;
}

static int houserail_track_search_by_location (const char *line, int post) {

    return houserail_scout_inside (&LayoutSegmentsIndex, line, post);
}

/* Not used yet. Show location of detectors?
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
        if (strsame (detector->id, id)) return detector;
    }
    return 0;
}
*/

const char *load_config (void) {

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
    int scale = houseconfig_integer (0, ".rail.scale");

    // Calculate the size needed for each array.

    int track = houseconfig_object (0, ".rail.track");
    if (track < 0) return "No track topology found";

    int models = houseconfig_array (track, ".models");
    int configmodelcount = 0;
    if (models >= 0) configmodelcount = houseconfig_array_length (models);

    int catalogmodels = 0;
    int catalogmodelcount = 0;
    const char *catalog = houseconfig_string (track, ".catalog");
    if (catalog) {
        const char *error = houserail_catalog_load (catalog);
        if (error) return error;
        if (scale > 0) houserail_catalog_set_scale (scale);

        catalogmodels = houserail_catalog_array (0, ".track.models");
        if (catalogmodels < 0) return "Empty track in catalog";
        catalogmodelcount = houserail_catalog_array_length (catalogmodels);
        if (catalogmodelcount <= 0) return "Empty track.models in catalog";
    }
    LayoutModelsCount = configmodelcount + catalogmodelcount;
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

    TrackPostDistance = houseconfig_integer (track, ".distances.post");
    if (TrackPostDistance <= 0) TrackPostDistance = PRECISION;
    DEBUG (__FILE__ ": Post distance set to %d\n", TrackPostDistance);

    // Populate the models array.

    LayoutModels = calloc (LayoutModelsCount, sizeof(struct TrackModel));
    houseconfig_enumerate (models, list, max);

    int i;
    for (i = 0; i < configmodelcount; ++i) {
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

        model->shape.straight = model->shape.arc = model->shape.radius = 0;
        int shape = houseconfig_array (element, ".shape");
        if (shape >= 0) {
            int shapelist[3];
            int count = houseconfig_enumerate (shape, shapelist, 3);
            if (count == 1) { // Straight track.
                model->shape.straight = houseconfig_integer (shapelist[0], "");
            } else if (count > 1) { // Curved track.
                model->shape.arc = houseconfig_integer (shapelist[0], "");
                model->shape.radius = houseconfig_integer (shapelist[1], "");
                if (count >= 3) { // Switch: curved branch and straight main
                    model->shape.straight = houseconfig_integer (shapelist[2], "");
                }
            }
        }
    }

   // Add the models from the catalog, if any.

    if (catalog) {
        houserail_catalog_enumerate (catalogmodels, list, max);
        int c;
        for (c = 0; c < catalogmodelcount; ++i, ++c) {
            int element = list[c];
            struct TrackModel *model = LayoutModels + i;
            model->id = houserail_catalog_string (element, ".id");
            model->signature = echttp_hash_signature (model->id);

            model->length = houserail_catalog_integer (element, ".length");
            model->reverse = houserail_catalog_integer (element, ".reverse");
            model->civil = houserail_catalog_integer (element, ".civil");
            if (model->reverse > 0) {
                DEBUG (__FILE__ ": Model %s civil speed %d length %d (%d on reverse branch)\n",
                       model->id, model->civil, model->length, model->reverse);
            } else {
                DEBUG (__FILE__ ": Model %s civil speed %d length %d\n",
                       model->id, model->civil, model->length);
            }

            model->shape.straight = model->shape.arc = model->shape.radius = 0;
            int shape = houserail_catalog_array (element, ".shape");
            if (shape >= 0) {
                int shapelist[3];
                int count = houserail_catalog_enumerate (shape, shapelist, 3);
                if (count == 1) { // Straight track.
                    model->shape.straight =
                        houserail_catalog_integer_scaled (shapelist[0], "");
                } else if (count > 1) { // Curved track.
                    model->shape.arc =
                        houserail_catalog_integer (shapelist[0], "");
                    model->shape.radius =
                        houserail_catalog_integer_scaled (shapelist[1], "");
                    if (count >= 3) { // Curved branch and straight main
                        model->shape.straight =
                            houserail_catalog_integer_scaled (shapelist[2], "");
                    }
                }
            }
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
        segment->index = i;

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

        struct TrackModel *model = LayoutModels + segment->model;
        segment->shape = model->shape;

        int curve = houseconfig_integer (element, ".curve");
        if ((temp[i].branch == 0) && (model->shape.arc > 0)) {
            if (curve == 0) return "Curved segment is missing curve direction";
            segment->shape.arc = model->shape.arc * ((curve > 0)?1:-1);
        }

        segment->origin.x = segment->origin.y = segment->origin.angle = 0;
        int origin = houseconfig_array (element, ".origin");
        if (origin >= 0) {
            int coordinates[3];
            int count = houseconfig_enumerate (origin, coordinates, 3);
            if (count >= 1)
                segment->origin.x = houseconfig_integer (coordinates[0], "");
            if (count >= 2)
                segment->origin.y = houseconfig_integer (coordinates[1], "");
            if (count >= 3)
                segment->origin.angle = houseconfig_integer (coordinates[2], "");
            if (segment->origin.angle == 0)
                segment->origin.angle = 360; // Used as explicit origin flag.
printf ("--- Segment %s has origin (%d, %d, %d)\n", segment->id, segment->origin.x, segment->origin.y, segment->origin.angle);
        }
    }

    // Resolve the segment linkages

    int switchcount = 0;

    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;

        // Allow the 'previous' field to be optional.
        // TBD: use a faster method than linear search inside a loop..
        if (!temp[i].previous) {
            int j;
            // At first search a good candidate from all the next fields,
            // but skip those that point to the branch of a switch.
            for (j = 0; j < LayoutSegmentsCount; ++j) {
                if (j == i) continue;
                if (strsame (LayoutSegments[j].id, temp[i].branch)) continue;
                if (strsame (temp[j].next, segment->id)) {
                    temp[i].previous = LayoutSegments[j].id;
                    break;
                }
            }
            if (!temp[i].previous) {
                // If we could not retrieve the previous from the next fields,
                // Then the previous might be a branch. Avoid using a branch
                // that the next points to..
                for (j = 0; j < LayoutSegmentsCount; ++j) {
                    if (j == i) continue;
                    if (strsame (LayoutSegments[j].id, temp[i].next)) continue;
                    if (strsame (temp[j].branch, segment->id)) {
                        temp[i].previous = LayoutSegments[j].id;
                        break;
                    }
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

        segment->done = 0;
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
           segment->start = startpost; // Remember what was used as start.

           segment->low = (segment->start >= 0) ? segment->start : startpost;
           segment->high = segment->low + LayoutModels[segment->model].length;

           struct TrackSegment *cursor = segment;
           int next;
           int high = cursor->high;
           for (next = segment->next; next >= 0; ) {
               LayoutSegments[next].low = high;
               cursor = LayoutSegments + next;
               high = cursor->high =
                   cursor->low + LayoutModels[cursor->model].length;

               // Stop when the line ends, the following segment was already
               // processed or when reaching a different line (usually a
               // switch).
               if (cursor->next < 0) break;
               if (LayoutSegments[cursor->next].low >= 0) break;

               if (!strsame (LayoutSegments[cursor->next].line, cursor->line)) {

                   // Special case: the next is a switch, the line name is the
                   // same on the common and branch segments.
                   struct TrackSegment *successor = LayoutSegments + cursor->next;
                   if ((successor->branch == next) &&
                       strsame (LayoutSegments[successor->common].line, cursor->line)) {
                       // Skip the switch and keep going.
                       next = successor->common;
                       high += LayoutModels[successor->model].reverse;
                       continue;
                   }
                   if ((successor->common == next) &&
                       strsame (LayoutSegments[successor->branch].line, cursor->line)) {
                       // Skip the switch and keep going.
                       next = successor->branch;
                       high += LayoutModels[successor->model].reverse;
                       continue;
                   }
                   break;
               }
               next = cursor->next;
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

    // Populate the detectors array.

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

        int index = echttp_hash_insert (&LayoutDetectorsHash, detector->id);
        if ((index > 0) && (index <= LayoutDetectorsCount))
            LayoutDetectorsMap[index] = i;
    }
    return 0;
}

static int calculate_straight_length (struct TrackSegment *segment) {

    // Explicit length.
    if (segment->shape.straight > 0) return segment->shape.straight;

    // Legacy from an early version.
    if ((segment->shape.arc == 0) && (segment->shape.radius > 0))
            return segment->shape.radius;

    // Infer from the "post" length if there is nothing better.
    return LayoutModels[segment->model].length * TrackPostDistance;
}

static int rotate (int value, int increment) {
    value += increment;
    if (value > 180) value -= 360;
    else if (value <= -180) value += 360;
    return value;
}

static void move_straight (const struct DisplayLocation *origin,
                           struct DisplayLocation *end, int angle, int length) {

    // Angle:                    0   15   30   45   60   75    90
    static const int base[] =   {0, 259, 500, 707, 866, 966, 1000};

    int sine = 1;
    int cosine = 1;

    if (angle < 0) angle += 360;
    else if (angle >= 360) angle -= 360;

    // Fold the angle to the first quadrant (o to 90 degrees)
    if (angle >= 270) { // Fourth quadrant
        angle = 360 - angle;
        sine = -1;
    } else if (angle >= 180) {
        angle -= 180;
        cosine = sine = -1;
    } else if (angle > 90) {
        angle = 180 - angle;
        cosine *= -1;
    }
    int index = angle / 15;
    sine *= base[index];
    cosine *= base[6-index];

    end->x = origin->x + ((length * cosine) / 1000);
    end->y = origin->y + ((length * sine) / 1000);
    end->angle = origin->angle;
}

static void move_center (const struct DisplayLocation *origin,
                         struct DisplayLocation *center,
                         int angle, int radius, int arc) {

    if (arc > 0)
        move_straight (origin, center, rotate (angle, 90), radius);
    else
        move_straight (origin, center, rotate (angle, -90), radius);
}

static void move_circle (const struct DisplayLocation *origin,
                         struct DisplayLocation *end,
                         int angle, int radius, int arc) {

    struct DisplayLocation center;
    move_center (origin, &center, angle, radius, arc);
    if (arc > 0)
        move_straight (&center, end, rotate (angle, rotate (arc, -90)), radius);
    else
        move_straight (&center, end, rotate (angle, rotate (arc, 90)), radius);
    end->angle = rotate (angle, arc);
printf ("---      move_circle from (%d, %d) angle %d arc %d, center (%d, %d), to (%d, %d, %d)\n", origin->x, origin->y, angle, arc, center.x, center.y, end->x, end->y, end->angle);
}

static void move_to_branch (const struct TrackSegment *segment,
                            struct DisplayLocation *origin) {

    struct DisplayLocation reverse = segment->reverse;
    struct TrackSegment *upcoming = LayoutSegments + segment->branch;

printf ("---   Move to branch: start at reverse (%d, %d, %d)\n", reverse.x, reverse.y, reverse.angle);
    if (upcoming->next == segment->index) {
        // The reverse point is the end point of the upcoming segment:
        // retrieve its true origin.
printf ("---   Reverse is connected to the end, move to the origin.\n");
        if ((upcoming->shape.arc == 0) || (upcoming->branch >= 0)) {
printf ("---   (straight move)\n");
            int length = calculate_straight_length (upcoming);
            move_straight (&reverse, origin, reverse.angle, length);
            origin->angle = rotate (reverse.angle, 180);
        } else {
printf ("---   (circle move: radius %d, arc %d)\n", upcoming->shape.radius, 0-upcoming->shape.arc);
            move_circle (&reverse, origin, reverse.angle,
                         upcoming->shape.radius, 0-upcoming->shape.arc);
        }
        origin->angle = rotate (origin->angle, 180); // Turn around

    } else if (upcoming->branch == segment->index) {
        // Two switches are connected branch to branch.
printf ("---   Two switches connected branch to branch.\n");
printf ("---   (circle move)\n");
        move_circle (&reverse, origin, reverse.angle,
                     upcoming->shape.radius, 0-upcoming->shape.arc);
    } else {
        *origin = reverse;
    }
printf ("---   Move to branch: found origin of %s at (%d, %d, %d)\n", upcoming->id, origin->x, origin->y, origin->angle);
}

static void calculate_endpoints (int start,
                                 const struct DisplayLocation *origin) {

    int i = start;
    struct TrackSegment *segment = LayoutSegments + i;
    if (segment->done) return;
    int previous = segment->previous;
    struct DisplayLocation cursor = *origin;
    for (;;) {

        if (segment->done) break;

        segment->origin = cursor;
        segment->reverse.x = segment->reverse.y = segment->reverse.angle = 0;
        int length = calculate_straight_length (segment);

        if (segment->shape.arc == 0) {
            move_straight (&cursor, &(segment->end), cursor.angle, length);
        } else {
            int curveangle = cursor.angle;
            const struct DisplayLocation *curveorigin = &cursor;
            struct DisplayLocation *curveend = &(segment->end);
            if (segment->branch >= 0) { // This is a switch, straight main line
                move_straight (&cursor, &(segment->end), cursor.angle, length);
                if (segment->common == segment->next) {
                    // Move backward to the reverse point.
                    curveorigin = &(segment->end);
                    curveangle = rotate (curveangle, 180);
                }
                curveend = &(segment->reverse);
            }
            move_circle (curveorigin, curveend, curveangle,
                         segment->shape.radius, segment->shape.arc);
        }
        cursor = segment->end;
        segment->done = 1;
printf ("--- Done with segment %s (%d, %d, %d) to (%d, %d, %d), going to %s\n", segment->id, segment->origin.x, segment->origin.y, segment->origin.angle, segment->end.x, segment->end.y, segment->end.angle, (segment->next >= 0)?LayoutSegments[segment->next].id:"(null)");
if (segment->branch >= 0) printf ("---    Segment %s is a switch, reverse point at (%d, %d, %d)\n", segment->id, segment->reverse.x, segment->reverse.y, segment->reverse.angle);

        if (segment->branch >= 0) {
printf ("--- Taking a detour to %s\n", LayoutSegments[segment->branch].id);
            struct DisplayLocation subwalk;
            move_to_branch (segment, &subwalk);
            calculate_endpoints (segment->branch, &subwalk);
printf ("--- end of detour\n");
        }

        if (segment->next < 0) break;
        struct TrackSegment *upcoming = LayoutSegments + segment->next;
        if (upcoming->previous != i) {
printf ("--- Upcoming segment %s points back to %s, not %s\n", upcoming->id, LayoutSegments[upcoming->previous].id, segment->id);
            if (upcoming->branch == i) {
                // This entered a switch through a branch. Calculate
                // the location of the common endpoint and restart
                // a walk originating from there.
printf ("--- Upcoming segment %s is a switch: start a subwalk from there at (%d, %d, %d).\n", upcoming->id, segment->end.x, segment->end.y, segment->end.angle);
                struct DisplayLocation common;
                move_circle (&(segment->end), &common, cursor.angle,
                             upcoming->shape.radius, 0-upcoming->shape.arc);
printf ("--- ended on segment %s at (%d, %d, %d).\n", upcoming->id, common.x, common.y, common.angle);
                if (upcoming->common == upcoming->next) {
                    // Need to move back to that switch's origin.
                    struct DisplayLocation normal;
                    int l = calculate_straight_length (upcoming);
                    move_straight (&common, &normal, rotate (common.angle, 180), l);
printf ("--- moved to segment %s origin at (%d, %d, %d).\n", upcoming->id, normal.x, normal.y, normal.angle);
                    calculate_endpoints (segment->next, &normal);
                } else {
                    calculate_endpoints (segment->next, &common);
                }
printf ("--- end of subwalk\n");
            }
            break; // We are done with this branch.
        }

        i = segment->next;
        if (i == start) break;
        segment = LayoutSegments + i;
    }

    // Walkback from the original point. This is odd because walking back
    // means a 180 degree turn, so the angles (and their directions) change.

    if (previous < 0) return;
    i = previous;
    segment = LayoutSegments + i;
    cursor = *origin;

    for (;;) {

        if (segment->done) break;

        segment->end = cursor;
        int length = calculate_straight_length (segment);

        if (segment->shape.arc == 0) {
            move_straight (&cursor, &(segment->origin), rotate (cursor.angle, 180), length);
        } else {
            int curveangle = cursor.angle;
            const struct DisplayLocation *curveorigin = &(segment->end);
            if (segment->branch >= 0) { // This is a switch.
                move_straight (&cursor, &(segment->origin), rotate (cursor.angle, 180), length);
                if (segment->common == segment->previous) {
                    // For this curve, the move is forward.
                    curveorigin = &(segment->origin);
                    curveangle = rotate (curveangle, -180);
                }
                move_circle (curveorigin, &(segment->reverse),
                             rotate (curveangle, 180),
                             segment->shape.radius, segment->shape.arc);
            } else {
                move_circle (&(segment->end), &(segment->origin),
                             rotate (curveangle, 180),
                             segment->shape.radius, 0 - segment->shape.arc);
                segment->origin.angle = rotate (segment->origin.angle, 180);
            }
        }
        cursor = segment->origin;
        segment->done = 1;
printf ("--- Done backward with segment %s (%d, %d, %d) to (%d, %d, %d), going to %s\n", segment->id, segment->origin.x, segment->origin.y, segment->origin.angle, segment->end.x, segment->end.y, segment->end.angle, (segment->previous >= 0)?LayoutSegments[segment->previous].id:"(null)");
if (segment->branch >= 0) printf ("---    Segment %s is a switch, reverse point at (%d, %d, %d)\n", segment->id, segment->reverse.x, segment->reverse.y, segment->reverse.angle);

        if (segment->branch >= 0) {
            struct DisplayLocation subwalk;
            move_to_branch (segment, &subwalk);
            calculate_endpoints (segment->branch, &subwalk);
        }

        if (segment->previous < 0) break;
        struct TrackSegment *upcoming = LayoutSegments + segment->previous;
        if (upcoming->next != i) {
            if (upcoming->branch == i) {
                // This entered a switch through a branch. Calculate
                // the location of the switch origin and restart
                // a walk from there.
                struct DisplayLocation common;
                move_circle (&(segment->origin), &common, rotate (cursor.angle, 180),
                             upcoming->shape.radius, 0-upcoming->shape.arc);
                if (upcoming->common == upcoming->next) {
                    // Need to move back to that switch's origin.
                    struct DisplayLocation normal;
                    move_straight (&common, &normal, rotate (cursor.angle, 180), length);
                    calculate_endpoints (segment->previous, &normal);
                } else {
                    calculate_endpoints (segment->previous, &common);
                }
            }
            break;
        }

        i = segment->previous;
        if (i == start) break;
        segment = LayoutSegments + i;
    }
}

static void calculate_viewbox (struct DisplayLocation *min,
                               struct DisplayLocation *max) {

    min->x = min->y = 1000000000;
    max->x = max->y = 0;

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (!segment->done) {
            fprintf (stderr, "Skipping segment %s. Not connected?\n", segment->id);
            continue;
        }
        if (min->x > segment->origin.x) min->x = segment->origin.x;
        if (min->x > segment->end.x) min->x = segment->end.x;
        if (min->y > segment->origin.y) min->y = segment->origin.y;
        if (min->y > segment->end.y) min->y = segment->end.y;
        if (max->x < segment->origin.x) max->x = segment->origin.x;
        if (max->x < segment->end.x) max->x = segment->end.x;
        if (max->y < segment->origin.y) max->y = segment->origin.y;
        if (max->y < segment->end.y) max->y = segment->end.y;
        if (segment->branch >= 0) { // This is a switch.
            if (min->x > segment->reverse.x) min->x = segment->reverse.x;
            if (min->y > segment->reverse.y) min->y = segment->reverse.y;
            if (max->x < segment->reverse.x) max->x = segment->reverse.x;
            if (max->y < segment->reverse.y) max->y = segment->reverse.y;
        }
    }
}

static void generate_html_head (void) {
    printf ("<html>\n"
            "<body style=\"margin: 0;\">\n"
            "<div style=\"background-color: #355b1eff;\" width=\"100%%\">\n");
}

static void generate_svg_head (const struct DisplayLocation *min,
                               int width, int height) {

    printf ("<svg\n"
            "  width=\"100%%\"\n"
            "  height=\"100%%\"\n"
            "  viewBox=\"%d %d %d %d\"\n"
            "  version=\"1.1\"\n"
            "  id=\"svg1\"\n"
            "  xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
            "  xmlns=\"http://www.w3.org/2000/svg\"\n"
            "  xmlns:svg=\"http://www.w3.org/2000/svg\">\n",
           min->x, min->y, width, height);
}

static void draw_path (const char *id, const char *d, int width) {

    printf ("      <path id=\"%s\" d=\"%s\""
                       " fill=\"none\" stroke-width=\"%d\"/>\n",
           id, d, width);
}

static void draw_straight (const char *id,
                           const struct DisplayLocation *origin,
                           const struct DisplayLocation *end, int width) {

    char d[80];
    if (origin->y == end->y) {
        snprintf (d, sizeof(d), "M %d %d H %d", origin->x, origin->y, end->x);
    } else if (origin->x == end->x) {
        snprintf (d, sizeof(d), "M %d %d V %d", origin->x, origin->y, end->y);
    } else {
        snprintf (d, sizeof(d), "M %d %d L %d %d",
                  origin->x, origin->y, end->x, end->y);
    }
    draw_path (id, d, width);
}

static void draw_curve (const char *id,
                        const struct DisplayLocation *origin,
                        const struct DisplayLocation *end,
                        const struct DisplayShape *shape, int width) {

   char d[80];
   snprintf (d, sizeof(d), "M %d %d A %d %d %d 0 %d %d %d",
             origin->x, origin->y,
             shape->radius, shape->radius, origin->angle, (shape->arc > 0)?1:0,
             end->x, end->y);
   draw_path (id, d, width);
}

static void generate_track (const struct TrackSegment *segment, int width) {

   int gap = width / 7;
   struct DisplayLocation origin;
   struct DisplayLocation end;
   move_straight (&(segment->origin), &origin, segment->origin.angle, gap);

   if (segment->shape.arc == 0) {
       move_straight (&(segment->end), &end, rotate (segment->end.angle, 180), gap);
       draw_straight (segment->id, &origin, &end, width);
   } else {
       char id[32];
       if (segment->branch >= 0) {
           // This is a switch. There is always a straight portion.
           move_straight (&(segment->end), &end, rotate (segment->end.angle, 180), gap);
           draw_straight (segment->id, &origin, &end, width);

           snprintf (id, sizeof(id), "%s:reverse", segment->id);
           if (segment->common == segment->previous) {
               move_straight (&(segment->reverse), &end, rotate (segment->reverse.angle, 180), gap);
           } else {
               origin = end;
               move_straight (&(segment->reverse), &end, rotate (segment->reverse.angle, 180), gap);
           }
       } else {
           // This is a regular curve.
           snprintf (id, sizeof(id), "%s", segment->id);
           move_straight (&(segment->end), &end, rotate (segment->end.angle, 180), gap);
       }
       draw_curve (id, &origin, &end, &(segment->shape), width);
   }
}

static void generate_tracks (int width) {

    int i;
    printf ("    <g id=\"tracks\" stroke=\"#f0f0f0\">\n");
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (!segment->done) continue;
        generate_track (segment, width);
    }
    printf ("    </g>\n");
}

static void generate_svg_tail (void) {
    printf ("</svg>\n");
}

static void generate_html_tail (void) {
    printf ("</div>\n</body>\n>/html>\n");
}

static int find_preferred_origin (void) {

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if ((!segment->done) && (segment->origin.angle != 0)) return i;
    }
    return -1;
}

static void walk_the_layout (void) {

    int done = 0;
    for (;;) {
        int i = find_preferred_origin();
        if (i < 0) break;
        struct DisplayLocation origin = LayoutSegments[i].origin;
        calculate_endpoints (i, &origin);
        done += 1;
    }

    if (!done) {
        // There is no explicit origin. Just use the first segment.
        struct DisplayLocation origin = {0, 0, 0};
        calculate_endpoints (0, &origin);
    }

    // Find segments not calculated yet that are linked to calculated segments.
/*
    for (;;) {
        int i = find_uncalculated ();
        if (i < 0) break;
        // TBD
    }
*/
}

static const char *generate_display (void) {

    const char *error = load_config ();
    if (error) return error;

    // Find where each segment fits and what is the viewbox
    walk_the_layout ();
    struct DisplayLocation min;
    struct DisplayLocation max;
    calculate_viewbox (&min, &max);

    int margin = (max.x - min.x) / 30;
    min.x -= margin;
    min.y -= margin;
    max.x += margin;
    max.y += margin;
    int width = max.x - min.x;
    int height = max.y - min.y;

    int strokewidth = margin / 3;

    generate_html_head ();
    generate_svg_head (&min, width, height);
    generate_tracks (strokewidth);
    generate_svg_tail ();
    generate_html_tail ();

    return 0;
}

int main (int argc, const char **argv) {

    houserail_scout_initialize (&LayoutSegmentsIndex, 0);
    if (argc <= 1) {
        printf ("Missing rail config file\n");
        exit (1);
    }
    const char *arg = argv[argc-1];
    argc -= 1;

    char option[120];
    const char *prefix = "./";
    if ((arg[0] == '/') || (arg[0] == '.')) prefix = "";
    snprintf (option, sizeof(option), "--config=%s%s", prefix, arg);
    houseconfig_default (option);
    houserail_catalog_default ("--catalog=.");

    const char *error =
        houserail_catalog_initialize (argc, argv);
    if (!error)
        error = houseconfig_initialize
                    ("layoutdisplay", generate_display, argc, argv);
    if (error) {
        printf ("Configuration error: %s\n", error);
        exit (1);
    }

    return 0;
}

