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
 * layoutdisplay --config=<file>
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

#include "houserail_scout.h"
#include "houserail_track.h"  // Only to get data structures.

static int TestMode = 0;

#define DEBUG if (TestMode) printf

static const char *LayoutName = 0;
static const char *LayoutDescription = 0;

#define PRECISION 1000  // Goal is millimeter precision on the display.

struct DisplayLocation {
    int x;
    int y;
    int z;
};

struct DisplayCurve {
    int arc;      // 0 means straight.
    int radius;
};

struct TrackModel {
    const char *id;
    unsigned int signature; // Seach accelerator.

    int length;  // Length on the normal side.
    int reverse; // Length on the reverse side, 0 if not a switch.
    int civil;   // Civil speed limit on that track.

    struct DisplayCurve curve;
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

    int angle;
    struct DisplayCurve curve;

    // The following attributes are calculated by following the topology from
    // the terminal point marked as the origin.
    int low;
    int high;

    int detector; // First detector on this segment;

    int done;
    struct DisplayLocation origin;
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

        int curve = houseconfig_array (element, ".curve");
        if (curve < 0) {
            model->curve.arc = model->curve.radius = 0;
        } else {
            int curvelist[2];
            houseconfig_enumerate (curve, curvelist, 2);
            model->curve.arc = houseconfig_integer (curvelist[0], "");
            model->curve.radius = houseconfig_integer (curvelist[1], "");
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

        struct TrackModel *model = LayoutModels + segment->model;
        segment->curve = model->curve;

        int curve = houseconfig_integer (element, ".curve");
        if (model->curve.arc > 0) {
            if (curve == 0) return "Curved segment is missing curve direction";
            segment->curve.arc = model->curve.arc * ((curve > 0)?1:-1);
        }
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
                if (strsame (temp[j].next, segment->id) &&
                    strsame (LayoutSegments[j].line, segment->line)) {
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

static int find_preferred_start (void) {

    int i;
    int ok = -1;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        struct TrackSegment *segment = LayoutSegments + i;
        if (segment->start < 0) continue;
        if (segment->previous < 0) return i; // Best choice possible.
        if (ok < 0) ok = i;
    }
    if (ok < 0) {
        fprintf (stderr, "Cannot find a start segment?\n");
        exit (1);
    }
    return ok;
}

static void move_straight (const struct DisplayLocation *origin,
                           struct DisplayLocation *end,
                           int angle, int length) {

    // Angle:                    0   15   30   45   60   75    90
    static const int base[] =   {0, 259, 500, 707, 866, 966, 1000};

    int sine = 1;
    int cosine = 1;

    if (angle < 0) angle += 360;
    else if (angle > 360) angle -= 360;

    if (angle > 180) {
        cosine = sine = -1;
        angle -= 180;
    }
    if (angle > 90) {
        angle = 180 - angle;
        cosine *= -1;
    }
    int index = angle / 15;
    sine *= base[index];
    cosine *= base[6-index];

    end->x = origin->x + ((length * cosine) / 1000);
    end->y = origin->y + ((length * sine) / 1000);
}

static void move_center (struct DisplayLocation *origin,
                         struct DisplayLocation *center,
                         int angle, int radius, int arc) {

    if (arc > 0)
        move_straight (origin, center, angle+90, radius);
    else
        move_straight (origin, center, angle-90, radius);
}

static void move_circle (struct DisplayLocation *origin,
                         struct DisplayLocation *end,
                         int angle, int radius, int arc) {

    struct DisplayLocation center;
    move_center (origin, &center, angle, radius, arc);
    move_straight (&center, end, angle+arc-90, radius);
}

static void calculate_endpoints (int start) {

    struct DisplayLocation origin = {0, 0, 0};

    int angle = 0;
    int i = start;
    struct TrackSegment *segment = LayoutSegments + i;
    struct TrackModel *model = LayoutModels + segment->model;
    int previous = segment->previous;
    for (;;) {

        if (segment->done) break;

        segment->angle = angle;
        segment->origin = origin;
        if (model->curve.arc == 0) {
            int length = model->length * PRECISION;
            if (model->curve.radius > 0) length = model->curve.radius;
            move_straight (&origin, &(segment->end), angle, length);
        } else {
            move_circle (&origin, &(segment->end), angle,
                         model->curve.radius, model->curve.arc);
            angle += model->curve.arc;
        }
fprintf (stderr, "Segment %s (forward) from (%d,%d) to (%d, %d)\n", segment->id, segment->origin.x, segment->origin.y, segment->end.x, segment->end.y);
        origin = segment->end;
        segment->done = 1;

        i = segment->next;
        if (i < 0) break;
        if (i == start) break;
        segment = LayoutSegments + i;
        model = LayoutModels + segment->model;
    }

    if (previous >= 0) {
        i = previous;
        segment = LayoutSegments + i;
        model = LayoutModels + segment->model;

        for (;;) {

            if (segment->done) break;

            segment->angle = angle;
            segment->origin = origin;
            if (model->curve.radius == 0) {
                move_straight (&origin, &(segment->end), angle,
                               model->length * PRECISION);
            } else {
                move_circle (&origin, &(segment->end), angle,
                             model->curve.radius, model->curve.arc);
                angle += model->curve.arc;
            }
fprintf (stderr, "Segment %s (backward) from (%d,%d) to (%d, %d)\n", segment->id, segment->origin.x, segment->origin.y, segment->end.x, segment->end.y);
            origin = segment->end;
            segment->done = 1;

            i = segment->previous;
            if (i < 0) break;
            if (i == start) break;
            segment = LayoutSegments + i;
            model = LayoutModels + segment->model;
        }
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

static void generate_track (const struct TrackSegment *segment, int width) {
   int gap = width / 7;
   struct DisplayLocation origin;
   struct DisplayLocation end;
   move_straight (&(segment->origin), &origin, segment->angle, gap);
   move_straight (&(segment->end), &end, segment->angle+segment->curve.arc+180, gap);
   if (segment->curve.arc == 0) {
       printf ("      <line id=\"%s\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"%d\"/>\n",
               segment->id, origin.x, origin.y, end.x, end.y, width);
   } else {
       printf ("      <path id=\"%s\" d=\"M %d %d A %d %d %d 0 %d %d %d\" fill=\"none\" stroke-width=\"%d\"/>\n",
               segment->id,
               origin.x, origin.y,
               segment->curve.radius, segment->curve.radius, segment->angle,
               (segment->curve.arc > 0)?1:0,
               end.x, end.y, width);
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

static const char *generate_display (void) {

    const char *error = load_config ();
    if (error) return error;

    // First step is to define where each segment fits and the viewbox
    struct DisplayLocation min;
    struct DisplayLocation max;
    calculate_endpoints (find_preferred_start());
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
    char option[120];
    const char *prefix = "./";
    if ((argv[1][0] == '/') || (argv[1][0] == '.')) prefix = "";
    snprintf (option, sizeof(option), "--config=%s%s", prefix, argv[1]);
    houseconfig_default (option);
    argv += 1;
    argc -= 1;

    const char *error =
        houseconfig_initialize ("layoutdisplay", generate_display, argc, argv);
    if (error) {
        printf ("Configuration error: %s\n", error);
        exit (1);
    }

    return 0;
}

