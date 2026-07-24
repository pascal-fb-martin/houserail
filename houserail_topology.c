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
 * houserail_topology.c - Load and store track and signaling data.
 *
 * This loads and decodes a model railroad layout topology.
 *
 * This module is a mostly passive database load and store module. Apart
 * from the database schema, it only implement data schema rules, not
 * business logic. It make the data accessible to other modules that
 * implement business logic.
 *
 * The provided data is made available read only (fixed configuration).
 * Live data must be managed by each module separately. This read only
 * feature is the reason why the table pointers are not made public:
 * the tables are read/write internally, exported read only as "const"
 * pointers. A similar reasoning applies to the counts. In other words,
 * the "getter" functions are only an artifact of C own's limitations.
 *
 * This module does implement search functions, because these features
 * are common to all other module and only depend on data schema rules.
 *
 * This module is not intended to be accessed by most HouseRail module,
 * as it exposes mostly raw data. It should only be accessed by a few
 * modules, that then expose data according to business rules.
 *
 * void houserail_topology_testmode (int enabled);
 *
 *    Enable debug traces, for unit tests only.
 *
 * const char *houserail_topology_initialize (int argc, const char **argv);
 *
 *    Initialize the options used by this module from command line arguments.
 *
 * const char *houserail_topology_reload (void);
 *
 *     Load a new topology configuration. The original data  used to build
 *     the topology in memory is accessed through the houseconfig.c module,
 *     typically loaded from a depot service, or from a file. This module
 *     does not decide where the data comes from, only about decoding it
 *     and converting it into internal data structures usable by other
 *     HouseRail modules.
 *
 *     This module implement access to "catalog" data, i.e. model databases
 *     shared by multiple layout databases.
 *
 * int houserail_topology_export (char *buffer, int size, const char *sep);
 *
 *     Export the track topology configuration in JSON format. Can be used
 *     to feed other service???
 *
 * int houserail_topology_model_count (void);
 * const struct TrackModel *houserail_topology_models (void);
 *
 *     Give read only access to the database of track segment models.
 *
 * int houserail_topology_segment_count (void);
 * const struct TrackSegment *houserail_topology_segments (void);
 *
 *     Give read only access to the database of actual track segments
 *
 * int houserail_topology_detector_count (void);
 * const struct TrackDetector *houserail_topology_detectors (void);
 *
 *     Give read only access to the database of track detectors.
 *
 * const struct TrackOptions *houserail_topology_options (void);
 *
 *     Give read only access to the global options.
 *
 *     Some layout data is global in nature: value of restricted speed,
 *     default distance between posts, etc. This returns a pointer to
 *     a single data structure that contains all such global data.
 *
 * int houserail_topology_search_model (const char *id);
 *
 *     Search for the specified segment model. Returns an index to
 *     the TrackModel table on success, -1 otherwise.
 *
 * int houserail_topology_search_by_id (const char *id);
 *
 *     Search for the specified track segment. Returns an index to
 *     the TrackSegment table on success, -1 otherwise.
 *
 * int houserail_topology_search_by_location (const char *line, int post);
 *
 *     Search for a track segment based on a track location. Returns
 *     an index to the TrackSegment table on success, -1 otherwise.
 *
 * int houserail_topology_search_detector (const char *id);
 *
 *     Search for the specified track detector. Returns an index to
 *     the TrackDetector table on success, -1 otherwise.
 *
 * LIMITATIONS:
 *
 * This design is optimized for up to 256 segments for now. To remove this
 * restriction, change echttp_hash.[hc] to allow the caller to set the size
 * of the hash.
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
#include "houserail_catalog.h"
#include "houserail_topology.h"

#define PRECISION 1000   // Goal is millimeter precision.

static int TestMode = 0;
#define DEBUG if (TestMode || echttp_isdebug()) printf

static struct TrackOptions TopologyOptions;

static struct TrackModel *TopologyModels = 0;
static int                TopologyModelsCount = 0;

static struct TrackSegment *TopologySegments = 0;
static int                  TopologySegmentsCount = 0;

static struct TrackDetector *TopologyDetectors = 0;
static int                   TopologyDetectorsCount = 0;

static echttp_hash       TopologySegmentsHash;
static int              *TopologySegmentsMap = 0;
static struct RangeIndex TopologySegmentsIndex;

static echttp_hash TopologyDetectorsHash;
static int        *TopologyDetectorsMap = 0;


void houserail_topology_testmode (int enabled) {
    TestMode = enabled;
}

int houserail_topology_search_model (const char *id) {

    if (!id) return -1;
    int signature = echttp_hash_signature (id);

    int i;
    for (i = 0; i < TopologyModelsCount; ++i) {
        if (TopologyModels[i].signature != signature) continue;
        if (strsame (TopologyModels[i].id, id)) return i;
    }
    return -1;
}

int houserail_topology_search_by_id (const char *id) {

    if (!id) return -1;

    int i = echttp_hash_find (&TopologySegmentsHash, id);
    if ((i > 0) && (i <= TopologySegmentsCount)) {
        return TopologySegmentsMap[i];
    }

    // For now: if not in the hash, fallback to linear search.
    // FIXME: improve echttp_hash to support variable size.

    int signature = echttp_hash_signature (id);

    for (i = 0; i < TopologySegmentsCount; ++i) {
        if (TopologySegments[i].signature != signature) continue;
        if (strsame (TopologySegments[i].id, id)) return i;
    }
    return -1;
}

int houserail_topology_search_by_location (const char *line, int post) {

    return houserail_scout_inside (&TopologySegmentsIndex, line, post);
}

int houserail_topology_search_detector (const char *id) {

    if (!id) return -1;

    int i = echttp_hash_find (&TopologyDetectorsHash, id);
    if ((i > 0) && (i <= TopologyDetectorsCount)) {
        return TopologyDetectorsMap[i];
    }

    // For now: if not in the hash, fallback to linear search.
    // FIXME: improve echttp_hash to support variable size.

    int signature = echttp_hash_signature (id);

    for (i = 0; i < TopologyDetectorsCount; ++i) {
        struct TrackDetector *detector = TopologyDetectors + i;
        if (detector->signature != signature) continue;
        if (!detector->id) continue;
        if (strsame (detector->id, id)) return i;
    }
    return -1;
}

const char *houserail_topology_initialize (int argc, const char **argv) {

    houserail_scout_initialize (&TopologySegmentsIndex, 0);
    return 0;
}

const char *houserail_topology_reload (void) {

    if (TopologyModels) {
       free (TopologyModels);
       TopologyModels = 0;
       TopologyModelsCount = 0;
    }
    if (TopologySegments) {
       free (TopologySegments);
       TopologySegments = 0;
       TopologySegmentsCount = 0;
    }
    if (TopologySegmentsMap) {
       free (TopologySegmentsMap);
       TopologySegmentsMap = 0;
    }
    if (TopologyDetectors) {
        free (TopologyDetectors);
        TopologyDetectors= 0;
    }
    if (TopologyDetectorsMap) {
        free (TopologyDetectorsMap);
        TopologyDetectorsMap = 0;
    }
    houserail_scout_erase (&TopologySegmentsIndex);

    TopologyOptions.name = houseconfig_string (0, ".rail.layout");
    if (!TopologyOptions.name) return "No track layout name";
    TopologyOptions.description = houseconfig_string (0, ".rail.description");

    int scale = houseconfig_integer (0, ".rail.scale");

    // Calculate the size needed for each array.

    int track = houseconfig_object (0, ".rail.track");
    if (track < 0) return "No track topology found";

    TopologyOptions.postDistance =
        houseconfig_integer (track, ".distances.post");
    if (TopologyOptions.postDistance <= 0)
        TopologyOptions.postDistance = PRECISION;
    DEBUG (__FILE__ ": Post distance set to %d\n", TopologyOptions.postDistance);

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

    // Even if no catalog was loaded, it is still useful for providing
    // the default scale.
    TopologyOptions.scale = (scale > 0)? scale : houserail_catalog_get_scale ();

    TopologyModelsCount = configmodelcount + catalogmodelcount;
    if (TopologyModelsCount <= 0) return "Empty track model list";

    int segments = houseconfig_array (track, ".segments");
    if (segments < 0) return "No track segments found";

    TopologySegmentsCount = houseconfig_array_length (segments);
    if (TopologySegmentsCount <= 0) return "Empty track segment list";

    int detectors = houseconfig_array (track, ".detectors");
    if (detectors < 0) return "No track detectors found";

    TopologyDetectorsCount = houseconfig_array_length (detectors);
    if (TopologyDetectorsCount <= 0) return "Empty track detectors list";

    int max = TopologyModelsCount;
    if (TopologySegmentsCount > max) max = TopologySegmentsCount;
    if (TopologyDetectorsCount > max) max = TopologyDetectorsCount;
    int *list = calloc (max, sizeof(int));

    DEBUG (__FILE__ ": %d models, %d segments, %d detectors\n",
           TopologyModelsCount, TopologySegmentsCount, TopologyDetectorsCount);

    // Populate the models array.

    TopologyModels = calloc (TopologyModelsCount, sizeof(struct TrackModel));
    houseconfig_enumerate (models, list, max);

    int i;
    for (i = 0; i < configmodelcount; ++i) {
        int element = list[i];
        struct TrackModel *model = TopologyModels + i;
        model->id = houseconfig_string (element, ".id");
        model->signature = echttp_hash_signature (model->id);
        model->index = i;

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
            struct TrackModel *model = TopologyModels + i;
            model->id = houserail_catalog_string (element, ".id");
            model->signature = echttp_hash_signature (model->id);
            model->index = i;

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

    TopologySegments = calloc (TopologySegmentsCount, sizeof(struct TrackSegment));
    houseconfig_enumerate (segments, list, TopologySegmentsCount);

    struct Linkage {
        const char *previous;
        const char *next;
        const char *common;
        const char *branch;
    } *temp = calloc (TopologySegmentsCount, sizeof(struct Linkage));

    echttp_hash_reset (&TopologySegmentsHash, 0);
    TopologySegmentsMap = calloc (TopologySegmentsCount+1, sizeof(int));

    for (i = 0; i < TopologySegmentsCount; ++i) {
        int element = list[i];
        struct TrackSegment *segment = TopologySegments + i;
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
        segment->model = houserail_topology_search_model (modelid);

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

        int index = echttp_hash_insert (&TopologySegmentsHash, segment->id);
        if ((index > 0) && (index <= TopologySegmentsCount))
            TopologySegmentsMap[index] = i;

        struct TrackModel *model = TopologyModels + segment->model;
        segment->shape = model->shape;

        segment->curve = 0;
        const char *curve = houseconfig_string (element, ".curve");
        if (curve) {
            if (strsame (curve, "left")) segment->curve = -1;
            else if (strsame (curve, "right")) segment->curve = 1;
            else return "invalid curve value";
        } else {
            // Legacy from early schema.
            segment->curve = houseconfig_integer (element, ".curve");
        }
        if ((temp[i].branch == 0) && (model->shape.arc > 0)) {
            // This is a simple curved track. WHich direction does it go?
            if (segment->curve == 0)
                return "Curved segment is missing curve direction";
            segment->shape.arc = model->shape.arc * ((segment->curve > 0)?1:-1);
        }

        segment->display.x = segment->display.y = segment->display.angle = 0;
        int origin = houseconfig_array (element, ".display");
        if (origin < 0) origin = houseconfig_array (element, ".origin");
        if (origin >= 0) {
            int coordinates[3];
            int count = houseconfig_enumerate (origin, coordinates, 3);
            if (count >= 1)
                segment->display.x = houseconfig_integer (coordinates[0], "");
            if (count >= 2)
                segment->display.y = houseconfig_integer (coordinates[1], "");
            if (count >= 3)
                segment->display.angle = houseconfig_integer (coordinates[2], "");
            if (segment->display.angle == 0)
                segment->display.angle = 360; // Used as explicit origin flag.
        }
    }

    // Resolve the segment linkages

    int switchcount = 0;

    for (i = 0; i < TopologySegmentsCount; ++i) {
        struct TrackSegment *segment = TopologySegments + i;

        // Allow the 'previous' field to be optional.
        // Obviously, inferring it from a next link is preferred but,
        // if there is none, a branch link is fine too.
        // TBD: use a faster method than linear search inside a loop..
        if (!temp[i].previous) {
            int j;
            // At first search a good candidate from all the next fields,
            // except one that points to the branch of a switch.
            for (j = 0; j < TopologySegmentsCount; ++j) {
                if (j == i) continue; // No self-reference allowed.
                if (strsame (TopologySegments[j].id, temp[i].branch)) continue;
                if (strsame (temp[j].next, segment->id)) {
                    temp[i].previous = TopologySegments[j].id;
                    break;
                }
            }
            if (!temp[i].previous) {
                // If we could not retrieve the previous from the next fields,
                // Then the previous might be a branch. Avoid using a branch
                // that the next points to..
                for (j = 0; j < TopologySegmentsCount; ++j) {
                    if (j == i) continue; // No self-reference allowed.
                    if (strsame (TopologySegments[j].id, temp[i].next)) continue;
                    if (strsame (temp[j].branch, segment->id)) {
                        temp[i].previous = TopologySegments[j].id;
                        break;
                    }
                }
            }
        }
        if ((!temp[i].previous) && (!temp[i].next)) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "isolated segment";
        }

        segment->previous = houserail_topology_search_by_id (temp[i].previous);
        if ((segment->previous < 0) && temp[i].previous) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "invalid previous link";
        }
        segment->next = houserail_topology_search_by_id (temp[i].next);
        if ((segment->next < 0) && temp[i].next) {
            DEBUG (__FILE__ ": Error on segment at index %d: %s\n", i, segment->id);
            return "invalid previous link";
        }

        segment->branch = houserail_topology_search_by_id (temp[i].branch);
        if (segment->branch >= 0) {
            // Default state of switch is 'normal'.
            segment->common = houserail_topology_search_by_id (temp[i].common);
            switchcount += 1;
        } else {
            segment->common = -1;
        }
    }
    free (temp);

    // Find the first track on each line, and follow the layout to calculate
    // the low and high post for each segment.
    // (This is not a very efficient loop. Make it better later, if needed.)
    //
    for (i = 0; i < TopologySegmentsCount; ++i) {
        struct TrackSegment *segment = TopologySegments + i;
        if (segment->low >= 0) continue; // Already processed.
        int startpost = (segment->start > 0)?segment->start:0;
        int isstart = ((segment->start >= 0) || (segment->previous < 0));
        if (!isstart) {
           // A branch starts at the common point of a switch, if the current
           // segment starts at the switch (and not ends at the switch).
           struct TrackSegment *previous = TopologySegments + segment->previous;
           if (previous->branch == i) {
              isstart = 1; // This starts from a switch reverse branch.
              startpost = TopologyModels[previous->model].reverse;
           }
        }
        if (isstart) {
           DEBUG (__FILE__ ": Segment %s is a starting point for line %s post %d\n",
                  segment->id, segment->line, startpost);
           segment->low = (segment->start >= 0) ? segment->start : startpost;
           segment->high = segment->low + TopologyModels[segment->model].length;

           struct TrackSegment *cursor = segment;
           int next;
           int high = cursor->high;
           for (next = segment->next; next >= 0; ) {
               TopologySegments[next].low = high;
               cursor = TopologySegments + next;
               high = cursor->high =
                   cursor->low + TopologyModels[cursor->model].length;

               // Stop when the line ends, the following segment was already
               // processed or when reaching a different line (usually a
               // switch).
               if (cursor->next < 0) break;
               if (TopologySegments[cursor->next].low >= 0) break;

               if (!strsame (TopologySegments[cursor->next].line, cursor->line)) {

                   // Special case: the next is a switch, the line name is the
                   // same on the common and branch segments.
                   struct TrackSegment *successor = TopologySegments + cursor->next;
                   if ((successor->branch == next) &&
                       strsame (TopologySegments[successor->common].line, cursor->line)) {
                       // Skip the switch and keep going.
                       next = successor->common;
                       high += TopologyModels[successor->model].reverse;
                       continue;
                   }
                   if ((successor->common == next) &&
                       strsame (TopologySegments[successor->branch].line, cursor->line)) {
                       // Skip the switch and keep going.
                       next = successor->branch;
                       high += TopologyModels[successor->model].reverse;
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
    houserail_scout_initialize (&TopologySegmentsIndex,
                                TopologySegmentsCount + switchcount);
    for (i = 0; i < TopologySegmentsCount; ++i) {
        struct TrackSegment *segment = TopologySegments + i;
        houserail_scout_add (&TopologySegmentsIndex,
                             i, segment->line, segment->low, segment->high);
        DEBUG (__FILE__ ": Segment %s on %s %d to %d (between %s and %s)\n",
               segment->id, segment->line, segment->low, segment->high,
               (segment->previous >= 0)?TopologySegments[segment->previous].id:"(none)",
               (segment->next >= 0)?TopologySegments[segment->next].id:"(none)");
        if (segment->branch >= 0) {
            struct TrackSegment *branch = TopologySegments + segment->branch;
            int low, high;
            int reverse = TopologyModels[segment->model].reverse;
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
            houserail_scout_add (&TopologySegmentsIndex,
                                 i, branch->line, low, high);
            DEBUG (__FILE__ ": Segment %s is a switch, branch on %s %d to %d (between %s and %s)\n",
                   segment->id, branch->line, low, high,
                   TopologySegments[branchprevious].id,
                   TopologySegments[branchnext].id);
        }
    }
    houserail_scout_finalize (&TopologySegmentsIndex);

    // Populate the detectors array.

    echttp_hash_reset (&TopologyDetectorsHash, 0);

    TopologyDetectorsMap = calloc (TopologyDetectorsCount+1, sizeof(int));
    TopologyDetectors = calloc (TopologyDetectorsCount, sizeof(struct TrackDetector));
    houseconfig_enumerate (detectors, list, TopologyDetectorsCount);

    for (i = 0; i < TopologyDetectorsCount; ++i) {
        int element = list[i];
        struct TrackDetector *detector = TopologyDetectors + i;
        detector->id = houseconfig_string (element, ".id");
        detector->signature = echttp_hash_signature (detector->id);
        detector->index = i;

        detector->area.line = houseconfig_string (element, ".line");
        detector->area.segment = 0;
        detector->area.low = houseconfig_integer (element, ".low");
        detector->area.high = houseconfig_integer (element, ".high");

        detector->segment =
            houserail_topology_search_by_location (detector->area.line, detector->area.low);
        if (detector->segment < 0) {
            DEBUG (__FILE__ ": Invalid location for detector %s\n", detector->id);
            continue;
        }
        struct TrackSegment *segment = TopologySegments + detector->segment;
        DEBUG (__FILE__ ": Detector %s is on segment %s covers %s %d to %d\n",
               detector->id, segment->id,
               detector->area.line, detector->area.low, detector->area.high);
        detector->next = segment->detector;
        segment->detector = i;
        detector->area.segment = segment->id;

        int index = echttp_hash_insert (&TopologyDetectorsHash, detector->id);
        if ((index > 0) && (index <= TopologyDetectorsCount))
            TopologyDetectorsMap[index] = i;
    }

    // When everything went well, set the global options for this layout.

    int value = houseconfig_integer (track, ".speeds.restricted");
    if (value <= 0) return "No Restricted speed found";
    TopologyOptions.restrictedSpeed = value;
    DEBUG (__FILE__ ": Restricted speed set to %d\n", TopologyOptions.restrictedSpeed);

    value = houseconfig_integer (track, ".speeds.reverse");
    if (value <= 0) return "No switch reverse speed found";
    TopologyOptions.switchReverseSpeed = value;
    DEBUG (__FILE__ ": Switch reverse speed set to %d\n", TopologyOptions.switchReverseSpeed);

    value = houseconfig_integer (track, ".periods.poll");
    if (value > 0) {
        if ((value < 10) || (value >= 1000)) return "Invalid poll period";
        TopologyOptions.fieldPollPeriod = value;
        DEBUG (__FILE__ ": field poll period set to %d\n", TopologyOptions.fieldPollPeriod);
    }

    value = houseconfig_integer (track, ".distances.stop");
    if (value <= 0) return "No stop distance found";
    TopologyOptions.stopDistance = value;
    DEBUG (__FILE__ ": Stop distance set to %d\n", TopologyOptions.stopDistance);

    value = houseconfig_integer (track, ".distances.slow");
    if (value <= 0) return "No slow distance found";
    TopologyOptions.slowDistance = value;
    DEBUG (__FILE__ ": Slow distance set to %d\n", TopologyOptions.slowDistance);

    houselog_event ("TOPOLOGY", "CONFIG", "LOADED",
                    "%d models %d tracks %d detectors",
                    TopologyModelsCount,
                    TopologySegmentsCount, TopologyDetectorsCount);
    return 0;
}

int houserail_topology_export (char *buffer, int size, const char *separator) {

    if (!TopologyOptions.name) return 0; // No track layout was loaded.

    int cursor = snprintf (buffer, size,
                           "%s\"layout\":\"%s\"",
                           separator, TopologyOptions.name);
    if (cursor >= size) goto overflow;
    if (TopologyOptions.description) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"description\":\"%s\"",
                            TopologyOptions.description);
        if (cursor >= size) goto overflow;
    }
    cursor += snprintf (buffer+cursor, size-cursor, ",\"track\":{");
    if (cursor >= size) goto overflow;
    int preamble = cursor;

    // Populate the global parameters

    cursor += snprintf (buffer+cursor, size-cursor,
                        ",\"speeds\":{\"restricted\":%d,\"reverse\":%d}",
                        TopologyOptions.restrictedSpeed,
                        TopologyOptions.switchReverseSpeed);
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, 
                        ",\"periods\":{\"poll\":%d}",
                        TopologyOptions.fieldPollPeriod);
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor,
                        ",\"distances\":{\"stop\":%d,\"slow\":%d}",
                        TopologyOptions.stopDistance,
                        TopologyOptions.slowDistance);
    if (cursor >= size) goto overflow;

    // Populate the models array.

    const char *prefix = "\"models\":[";
    int start = cursor;
    int i;
    for (i = 0; i < TopologyModelsCount; ++i) {
        struct TrackModel *model = TopologyModels + i;
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
    for (i = 0; i < TopologySegmentsCount; ++i) {
        struct TrackSegment *segment = TopologySegments + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"model\":\"%s\","
                                "\"line\":\"%s\"",
                            prefix,
                            segment->id, TopologyModels[segment->model].id,
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
                                TopologySegments[segment->previous].id);
            if (cursor >= size) goto overflow;
        }
        if (segment->next >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"next\":\"%s\"",
                                TopologySegments[segment->next].id);
            if (cursor >= size) goto overflow;
        }
        if (segment->branch >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"branch\":\"%s\"",
                                TopologySegments[segment->branch].id);
            if (cursor >= size) goto overflow;
        }
        if (segment->common >= 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                ",\"common\":\"%s\"",
                                TopologySegments[segment->common].id);
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
    for (i = 0; i < TopologyDetectorsCount; ++i) {
        struct TrackDetector *detector = TopologyDetectors + i;
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

int houserail_topology_model_count (void) {
    return TopologyModelsCount;
}

const struct TrackModel *houserail_topology_models (void) {
    return TopologyModels;
}

int houserail_topology_segment_count (void) {
    return TopologySegmentsCount;
}

const struct TrackSegment *houserail_topology_segments (void) {
    return TopologySegments;
}

int houserail_topology_detector_count (void) {
    return TopologyDetectorsCount;
}

const struct TrackDetector *houserail_topology_detectors (void) {
    return TopologyDetectors;
}

const struct TrackOptions *houserail_topology_options (void) {
    return &TopologyOptions;
}

