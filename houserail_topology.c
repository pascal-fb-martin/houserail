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
 * void houserail_topology_billmode (int enabled);
 *
 *    Enable traces, for tools and unit tests only.
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
 * int houserail_topology_export
 *         (char *buffer, int size, const char *separator);
 *
 *     Export the last loaded track topology configuration in JSON format.
 *     Can be used to feed other service???
 *
 * int houserail_topology_export_segments
 *         (char *buffer, int size, const char *separator);
 *
 *     Export the list of segments (ID, line, low and high posts) in JSON
 *     format. The segments are ordered by line and high post.
 *
 *     This is not just a subset of the houserail_topology_export() data:
 *     this provides post limits, which are calculated. Because switches
 *     have two tracks on different lines, they appear here as two segments:
 *     once for the normal track and once for the reverse track.
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
 * int houserail_topology_signal_count (void);
 * const struct TrackSignal *houserail_topology_signals (void);
 *
 *     Give read only access to the database of signals.
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
 * int houserail_topology_search_signal (const char *id);
 *
 *     Search for the specified signal. Returns an index to
 *     the TrackSignal table on success, -1 otherwise.
 *
 * LIMITATIONS:
 *
 * This design is optimized for up to 256 segments for now. To remove this
 * restriction, change echttp_hash.[hc] to allow the caller to set the size
 * of the hash.
 *
 * Another limit to the number of segment is the signature that identifies
 * the feature protected by a signal: that signature assumes less than 16384
 * segments at this time. See constant TRACK_SEGMENTS_MAX and TrackSignal's
 * field "protected".
 *
 * The implementation also assumes maximums of 16384 detectors total and
 * 16 detectors per segment.
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <alloca.h>

#include <echttp.h>
#include <echttp_libc.h>
#include <echttp_hash.h>

#include <houselog.h>
#include <houseconfig.h>

#include "houserail_scout.h"
#include "houserail_catalog.h"
#include "houserail_topology.h"

#define PRECISION 1000   // Goal is millimeter precision.

#define TRACK_SEGMENTS_MAX 0x4000

static int BillMode = 0;
static int TestMode = 0;
#define DEBUG if (TestMode || echttp_isdebug()) printf

static struct TrackOptions TopologyOptions;

static struct TrackModel *TopologyModels = 0;
static int                TopologyModelsCount = 0;

static struct TrackSegment *TopologySegments = 0;
static int                  TopologySegmentsCount = 0;

static struct TrackDetector *TopologyDetectors = 0;
static int                   TopologyDetectorsCount = 0;

static struct TrackSignal *TopologySignals = 0;
static int                 TopologySignalsCount = 0;

static echttp_hash       TopologySegmentsHash = {0};
static int              *TopologySegmentsMap = 0;
static struct RangeIndex TopologySegmentsIndex = {0};

static echttp_hash TopologyDetectorsHash = {0};
static int        *TopologyDetectorsMap = 0;

static echttp_hash TopologySignalsHash = {0};
static int        *TopologySignalsMap = 0;

struct TrackLinkage {
    const char *previous;
    const char *next;
    const char *common;
    const char *branch;
    const char *defend;
};
static struct TrackLinkage *Symbols = 0;

void houserail_topology_testmode (int enabled) {
    TestMode = enabled;
}

void houserail_topology_billmode (int enabled) {
    BillMode = enabled;
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
    return -1;
}

int houserail_topology_search_signal (const char *id) {

    if (!id) return -1;
    if (TopologySignalsCount <= 0) return -1;

    int i = echttp_hash_find (&TopologySignalsHash, id);
    if ((i > 0) && (i <= TopologySignalsCount)) {
        return TopologySignalsMap[i];
    }
    return -1;
}

const char *houserail_topology_initialize (int argc, const char **argv) {

    houserail_scout_initialize (&TopologySegmentsIndex, 0);
    return 0;
}

static void houserail_topology_set_origin (const struct TrackSegment *segment,
                                           struct TrackLocation *origin) {

    if ((!origin->segment) ||
        (segment->low < origin->post) ||
        ((segment->low == origin->post) &&
            (strcasecmp (segment->line, origin->line) < 0))) {

        origin->segment = segment->id; // New origin switch for the group.
        origin->line = segment->line;
        origin->post = segment->low;
    }
}

static int houserail_topology_search_origin (struct TrackSegment *segment,
                                             struct TrackLocation *origin,
                                             int from, int count) {

    if (segment->branch < 0) return count; // Stop walking when not a switch.

    count += 1;
    houserail_topology_set_origin (segment, origin);
    if (from != segment->branch) // Avoid going back to the switch it came from
        count = houserail_topology_search_origin
                    (TopologySegments + segment->branch, origin, segment->index, count);

    // Walk the tracks across adjacent switches until we do not encounter
    // any more switches. This loops to walk adjacent switches on the same
    // line and use recursion to walk each branch.

    int next = segment->next;
    while (next >= 0) {
        struct TrackSegment *cursor = TopologySegments + next;
        if (cursor->branch < 0) break;
        if (++count > TopologySegmentsCount) break; // Hoops, infinite loop.
        houserail_topology_set_origin (cursor, origin);
        count = houserail_topology_search_origin
                    (TopologySegments + cursor->branch, origin, next, count);
        next = cursor->next;
    }

    int prev = segment->previous;
    while (prev >= 0) {
        struct TrackSegment *cursor = TopologySegments + prev;
        if (cursor->branch < 0) break;
        if (++count > TopologySegmentsCount) break; // Hoops, infinite loop.
        houserail_topology_set_origin (cursor, origin);
        count = houserail_topology_search_origin
                    (TopologySegments + cursor->branch, origin, prev, count);
        prev = cursor->previous;
    }
    return count;
}

static void houserail_topology_erase (echttp_hash *h, int **map) {
    if (*map) {
        echttp_hash_reset (h, 0);
        echttp_hash_release (h);
        free (*map);
        *map = 0;
    }
}

const char *houserail_topology_reload (void) {

    if (Symbols) {
       free (Symbols);
       Symbols = 0;
    }
    if (TopologyModels) {
       free (TopologyModels);
       TopologyModels = 0;
       TopologyModelsCount = 0;
    }
    if (TopologySegments) {
       free (TopologySegments);
       TopologySegments = 0;
       TopologySegmentsCount = 0;
       houserail_topology_erase (&TopologySegmentsHash, &TopologySegmentsMap);
       houserail_scout_erase (&TopologySegmentsIndex);
    }

    if (TopologyDetectors) {
        free (TopologyDetectors);
        TopologyDetectors= 0;
        houserail_topology_erase (&TopologyDetectorsHash, &TopologyDetectorsMap);
    }

    if (TopologySignals) {
        free (TopologySignals);
        TopologySignals= 0;
        houserail_topology_erase (&TopologySignalsHash, &TopologySignalsMap);
    }

    TopologyOptions.name = houseconfig_string (0, ".rail.layout");
    if (!TopologyOptions.name) return "No track layout name";
    TopologyOptions.description = houseconfig_string (0, ".rail.description");

    int scale = houseconfig_positive (0, ".rail.scale");

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

    // Even if no catalog was loaded, it is still useful for providing
    // the default scale.
    TopologyOptions.scale = (scale > 0)? scale : houserail_catalog_get_scale ();

    TopologyModelsCount = configmodelcount + catalogmodelcount;
    if (TopologyModelsCount <= 0) return "Empty track model list";

    int segments = houseconfig_array (track, ".segments");
    if (segments < 0) return "No track segments found";

    TopologySegmentsCount = houseconfig_array_length (segments);
    if (TopologySegmentsCount <= 0)
        return "Empty track segment list";
    if (TopologySegmentsCount > TRACK_SEGMENTS_MAX)
        return "Track segment list too long";

    int detectors = houseconfig_array (track, ".detectors");
    if (detectors < 0) return "No track detectors found";

    TopologyDetectorsCount = houseconfig_array_length (detectors);
    if (TopologyDetectorsCount <= 0) return "Empty track detectors list";

    // Signals are optional.
    int signals = houseconfig_array (track, ".signals");
    TopologySignalsCount = (signals>=0)?houseconfig_array_length (signals):0;

    int max = 2; // Minimum.
    if (TopologyModelsCount > max) max = TopologyModelsCount;
    if (TopologySegmentsCount > max) max = TopologySegmentsCount;
    if (TopologyDetectorsCount > max) max = TopologyDetectorsCount;
    if (TopologySignalsCount > max) max = TopologySignalsCount;
    int *list = alloca (max * sizeof(int));

    DEBUG (__FILE__ ": %d models, %d segments, %d detectors, %d signals\n",
           TopologyModelsCount,
           TopologySegmentsCount,
           TopologyDetectorsCount,
           TopologySignalsCount);

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
            DEBUG (__FILE__ ": model %s civil speed %d length %d (%d on reverse branch)\n",
                   model->id, model->civil, model->length, model->reverse);
        } else {
            DEBUG (__FILE__ ": model %s civil speed %d length %d\n",
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
                if (houseconfig_isreal (shapelist[0], "")) {
                    double arc = houseconfig_real (shapelist[0], "");
                    model->shape.arc = lround (arc * 100);
                } else {
                    model->shape.arc =
                        100 * houseconfig_integer (shapelist[0], "");
                }
                model->shape.radius = houseconfig_integer (shapelist[1], "");
                if (count >= 3) { // Switch: curved branch and straight main
                    model->shape.straight = houseconfig_integer (shapelist[2], "");
                }
            }
        }
        model->feature = houseconfig_string (element, ".feature");
        model->usage = 0;
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
                DEBUG (__FILE__ ": model %s civil speed %d length %d (%d on reverse branch)\n",
                       model->id, model->civil, model->length, model->reverse);
            } else {
                DEBUG (__FILE__ ": model %s civil speed %d length %d\n",
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
                    if (houserail_catalog_isreal (shapelist[0], "")) {
                        double arc = houserail_catalog_real (shapelist[0], "");
                        model->shape.arc = lround (arc * 100);
                    } else {
                        model->shape.arc =
                            100 * houserail_catalog_integer (shapelist[0], "");
                    }
                    model->shape.radius =
                        houserail_catalog_integer_scaled (shapelist[1], "");
                    if (count >= 3) { // Curved branch and straight main
                        model->shape.straight =
                            houserail_catalog_integer_scaled (shapelist[2], "");
                    }
                }
            }
            model->feature = houserail_catalog_string (element, ".feature");
            model->usage = 0;
        }
    }

    // Populate the segments array.

    Symbols = calloc (TopologySegmentsCount, sizeof(struct TrackLinkage));
    TopologySegments = calloc (TopologySegmentsCount, sizeof(struct TrackSegment));

    echttp_hash_create (&TopologySegmentsHash, TopologySegmentsCount+1);
    TopologySegmentsMap = calloc (TopologySegmentsCount+1, sizeof(int));

    houseconfig_enumerate (segments, list, TopologySegmentsCount);

    for (i = 0; i < TopologySegmentsCount; ++i) {
        int element = list[i];
        struct TrackSegment *segment = TopologySegments + i;
        segment->id = houseconfig_string (element, ".id");
        if (!segment->id) {
            DEBUG (__FILE__ ": error on segment at index %d\n", i);
            return "invalid segment (no id)";
        }
        segment->signature =
            echttp_hash_signature (segment->id);
        segment->index = i;

        segment->line = houseconfig_string (element, ".line");
        if (!segment->line) {
            DEBUG (__FILE__ ": error on segment at index %d: %s\n", i, segment->id);
            return "invalid segment (no line)";
        }
        const char *modelid = houseconfig_string (element, ".model");
        if (!modelid) {
            DEBUG (__FILE__ ": error on segment at index %d: %s\n", i, segment->id);
            return "invalid segment (no model)";
        }
        segment->model = houserail_topology_search_model (modelid);
        if (segment->model < 0) {
            DEBUG (__FILE__ ": unknown model %s referenced by segment %s\n", modelid, segment->id);
            return "invalid segment (unknown model)";
        }
        TopologyModels[segment->model].usage += 1;

        segment->control = houseconfig_string (element, ".control");
        if (!segment->control) segment->control = segment->id;

        if (houseconfig_present (element, ".start"))
            segment->start = houseconfig_integer (element, ".start");
        else
            segment->start = -1;
        segment->low = segment->high = -1; // To be calculated later.
        segment->detector = -1; // List will be built later.
        segment->detectorcount = 0; // List will be built later.

        Symbols[i].previous = houseconfig_string (element, ".previous");
        Symbols[i].next = houseconfig_string (element, ".next");
        Symbols[i].common = houseconfig_string (element, ".common");
        Symbols[i].branch = houseconfig_string (element, ".branch");
        Symbols[i].defend = houseconfig_string (element, ".defend");

        // All these links will be resolved later.
        segment->next = segment->previous = segment->common = segment->branch = segment->protected = -1;
        segment->signals = -1;

        int index = echttp_hash_insert (&TopologySegmentsHash, segment->id);
        if ((index > 0) && (index <= TopologySegmentsCount))
            TopologySegmentsMap[index] = i;

        segment->ending = 0;
        segment->stop.line = segment->slow.line = 0;

        const struct TrackModel *model = TopologyModels + segment->model;
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
        if ((Symbols[i].branch == 0) && (model->shape.arc > 0)) {
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
            if (count >= 3) {
                if (houseconfig_isreal (coordinates[2], "")) {
                    double angle = houseconfig_real (coordinates[2], "");
                    segment->display.angle = lround (angle * 100);
                } else {
                    segment->display.angle =
                        100 * houseconfig_integer (coordinates[2], "");
                }
            }
            if (segment->display.angle == 0)
                segment->display.angle = 36000; // Used as explicit origin flag.
        }
        segment->feature = houseconfig_string (element, ".feature");
    }

    // Infer missing "next" links: could it be the subsequent segment?

    for (i = TopologySegmentsCount-2; i >= 0; --i) {

        if (!Symbols[i].next) {
            struct TrackSegment *segment = TopologySegments + i;
            struct TrackSegment *subsequent = segment + 1;

            if (!strsame (segment->line, subsequent->line)) continue;
            if (Symbols[i+1].previous &&
                (!strsame (Symbols[i+1].previous, segment->id))) continue;
            Symbols[i].next = subsequent->id;
        }
    }

    // Resolve the "next" links and infers missing obvious "previous" links.

    for (i = 0; i < TopologySegmentsCount; ++i) {

        const char *next = Symbols[i].next;
        if (!next) continue;

        struct TrackSegment *segment = TopologySegments + i;
        int nextindex = houserail_topology_search_by_id (next);
        if (nextindex < 0) {
            DEBUG (__FILE__ ": error on segment %s at index %d: invalid next %s\n", segment->id, segment->index, next);
            return "invalid next link";
        }
        segment->next = nextindex;

        // Allow the 'previous' field to be optional.
        // Since there is now a valid "next" link, this can be used to infer
        // (and resolve) a "previous" field for the target, if:
        // - the target's "previous" field is missing (duh..), and
        // - the current segment is not the target's next or branch.
        // (That method is cheaper than scanning the whole table later.)

        if ((!Symbols[nextindex].previous) &&
            (!strsame (Symbols[nextindex].next, segment->id)) &&
            (!strsame (Symbols[nextindex].branch, segment->id))) {
            Symbols[nextindex].previous = segment->id;
            TopologySegments[nextindex].previous = i;
        }
    }

    // Resolve the other segment linkages. Infer "previous" links that are
    // still missing from "branch" links, when applicable.

    int switchcount = 0;

    for (i = 0; i < TopologySegmentsCount; ++i) {

        struct TrackSegment *segment = TopologySegments + i;

        if (segment->previous < 0) {
            segment->previous = houserail_topology_search_by_id (Symbols[i].previous);
            if ((segment->previous < 0) && Symbols[i].previous) {
                DEBUG (__FILE__ ": error on segment %s at index %d: invalid previous %s\n", segment->id, i, Symbols[i].previous);
                return "invalid previous link";
            }
        }

        if (Symbols[i].branch) {

            segment->branch = houserail_topology_search_by_id (Symbols[i].branch);
            if (segment->branch < 0) {
                DEBUG (__FILE__ ": error on segment %s at index %d: invalid branch %s\n", segment->id, i, Symbols[i].branch);
                return "invalid branch link";
            }
            segment->common = houserail_topology_search_by_id (Symbols[i].common);
            if (segment->common < 0) {
                DEBUG (__FILE__ ": error on segment %s at index %d: invalid common %s\n", segment->id, i, Symbols[i].common?Symbols[i].common:"(missing)");
                return "invalid common link";
            }

            // Allow the 'previous' field to be optional.
            // See if a missing "previous" link could be inferred from this
            // branch link. (All the "next" inferences have alreay been done.)
            // Using a branch link is fine now, as long as the branch target
            // does not already points to the current segment by another mean.

            struct TrackSegment *branch = TopologySegments + segment->branch;
            if ((!Symbols[segment->branch].previous) &&
                (branch->next != i) &&
                (!strsame (Symbols[segment->branch].branch, segment->id))) {
                Symbols[segment->branch].previous = segment->id;
                branch->previous = i;
            }
            switchcount += 1;
        }
    }

    // Final checks: reject isolated tracks or unbalanced links.
    // This probably does not catch all possible mistakes, but troubleshooting
    // incorrect links is tedious, so any error detection helps.

    for (i = 0; i < TopologySegmentsCount; ++i) {

        struct TrackSegment *segment = TopologySegments + i;

        if ((segment->next < 0) && (segment->previous < 0)) {
            DEBUG (__FILE__ ": isolated segment %s at index %d\n", segment->id, i);
            return "isolated track segment";
        }

        // Reject loopbacks, except if this is a 2 parts 360 degrees loop, or
        // if this is a loopback on a switch.

        if (segment->next == segment->previous) {
            struct TrackSegment *next = TopologySegments + segment->next;
            if ((next->branch < 0) &&
                ((next->next != i) || (next->previous != i))) {
               DEBUG (__FILE__ ": segment %s loops on segment %s\n", segment->id, next->id);
               return "invalid loopback";
            }
        }

        if (segment->next >= 0) {
            // The target must reference this segment one way or another
            if ((TopologySegments[segment->next].next != i) &&
                (TopologySegments[segment->next].previous != i) &&
                (TopologySegments[segment->next].branch != i)) {
                DEBUG (__FILE__ ": error on segment %s at index %d: unbalanced next link\n", segment->id, i);
                return "unbalanced next link";
            }
        }
        if (segment->previous >= 0) {
            // The target must reference this segment one way or another
            if ((TopologySegments[segment->previous].next != i) &&
                (TopologySegments[segment->previous].previous != i) &&
                (TopologySegments[segment->previous].branch != i)) {
                DEBUG (__FILE__ ": error on segment %s at index %d: unbalanced previous link\n", segment->id, i);
                return "unbalanced previous link";
            }
        }

        if (segment->branch > 0) {
            struct TrackSegment *branch = TopologySegments + segment->branch;

            // The common target must match the switch's next or previous
            if ((segment->common != segment->next) &&
                (segment->common != segment->previous)) {
                DEBUG (__FILE__ ": error on segment %s at index %d: common does not match next or previous\n", segment->id, i);
                return "common does not match next or previous";
            }

            // The branch target must not be on the switch's main line.
            if (strsame (branch->line, segment->line)) {
                DEBUG (__FILE__ ": error on segment %s at index %d: branch refers to the main line\n", segment->id, i);
                return "branch refers to the main line";
            }

            // The branch target must reference this segment one way or another
            if ((branch->next != i) &&
                (branch->previous != i) &&
                (branch->branch != i)) {
                DEBUG (__FILE__ ": error on segment %s at index %d: unbalanced branch link\n", segment->id, i);
                return "unbalanced branch link";
            }
        }
    }

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
           DEBUG (__FILE__ ": segment %s is a starting point for line %s post %d\n",
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
        const struct TrackSegment *segment = TopologySegments + i;
        houserail_scout_add (&TopologySegmentsIndex,
                             i, segment->line, segment->low, segment->high);
        DEBUG (__FILE__ ": segment %s on %s %d to %d (between %s and %s)\n",
               segment->id, segment->line, segment->low, segment->high,
               (segment->previous >= 0)?TopologySegments[segment->previous].id:"(none)",
               (segment->next >= 0)?TopologySegments[segment->next].id:"(none)");
        if (segment->branch >= 0) {
            const struct TrackSegment *branch = TopologySegments + segment->branch;
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
            DEBUG (__FILE__ ": segment %s is a switch, branch on %s %d to %d (between %s and %s)\n",
                   segment->id, branch->line, low, high,
                   TopologySegments[branchprevious].id,
                   TopologySegments[branchnext].id);
        }
    }
    houserail_scout_finalize (&TopologySegmentsIndex);

    // Create the switch group linkage used by the signal logic.

    for (i = 0; i < TopologySegmentsCount; ++i) {

        struct TrackSegment *segment = TopologySegments + i;
        if (segment->branch < 0) continue;

        segment->protected = segment->index; // Default: protect thyself.
        if (Symbols[i].defend) {
            segment->protected = houserail_topology_search_by_id (Symbols[i].defend);
            if (segment->protected < 0) {
                DEBUG (__FILE__ ": invalid defend link %s for segment %s\n", Symbols[i].defend, segment->id);
                return "invalid defend link";
            }
        } else {
            int count = 0;
            struct TrackLocation origin = {0};
            houserail_topology_search_origin (segment, &origin, -1, count);
            segment->protected =
                houserail_topology_search_by_id (origin.segment);
        }
        DEBUG (__FILE__ ": switch %s is part of group protected by %s\n", TopologySegments[i].id, TopologySegments[segment->protected].id);
    }

    // Populate the detectors array.

    TopologyDetectors = calloc (TopologyDetectorsCount, sizeof(struct TrackDetector));

    echttp_hash_create (&TopologyDetectorsHash, TopologyDetectorsCount+1);
    TopologyDetectorsMap = calloc (TopologyDetectorsCount+1, sizeof(int));

    houseconfig_enumerate (detectors, list, TopologyDetectorsCount);

    for (i = 0; i < TopologyDetectorsCount; ++i) {
        int element = list[i];
        struct TrackDetector *detector = TopologyDetectors + i;
        detector->id = houseconfig_string (element, ".id");
        detector->signature = echttp_hash_signature (detector->id);
        detector->index = i;

        detector->segment = -1;
        detector->area.segment = 0;
        detector->area.low = houseconfig_integer (element, ".low");
        detector->area.high = houseconfig_integer (element, ".high");

        detector->area.line = houseconfig_string (element, ".line");
        if (detector->area.line) {
            detector->segment =
                houserail_topology_search_by_location (detector->area.line,
                                                       detector->area.low);
        } else {
            const char *id = houseconfig_string (element, ".segment");
            if (id) {
                detector->segment = houserail_topology_search_by_id (id);
                if (detector->segment < 0) {
                    DEBUG (__FILE__ ": invalid segment %s for detector %s\n", id, detector->id);
                    continue;
                }
                detector->area.low += TopologySegments[detector->segment].low;
                detector->area.high += TopologySegments[detector->segment].low;
                detector->area.line = TopologySegments[detector->segment].line;
            }
        }
        if (detector->segment < 0) {
            DEBUG (__FILE__ ": invalid location for detector %s\n", detector->id);
            continue;
        }
        struct TrackSegment *segment = TopologySegments + detector->segment;
        DEBUG (__FILE__ ": detector %s is on segment %s covers %s %d to %d\n",
               detector->id, segment->id,
               detector->area.line, detector->area.low, detector->area.high);
        detector->next = segment->detector;
        segment->detector = i;
        detector->map = (segment->detectorcount)++;
        detector->area.segment = segment->id;

        int index = echttp_hash_insert (&TopologyDetectorsHash, detector->id);
        if ((index > 0) && (index <= TopologyDetectorsCount))
            TopologyDetectorsMap[index] = i;
    }

    // Populate the signal array (optional: a layout might not have any).

    if (TopologySignalsCount > 0) {

        TopologySignals = calloc (TopologySignalsCount, sizeof(struct TrackSignal));

        echttp_hash_create (&TopologySignalsHash, TopologySignalsCount+1);
        TopologySignalsMap = calloc (TopologySignalsCount+1, sizeof(int));

        houseconfig_enumerate (signals, list, TopologySignalsCount);

        for (i = 0; i < TopologySignalsCount; ++i) {
            int element = list[i];
            int segmentindex;
            struct TrackSignal *signal = TopologySignals + i;
            signal->id = houseconfig_string (element, ".id");
            signal->signature = echttp_hash_signature (signal->id);
            signal->index = i;

            signal->direction = 0; // Unknown.
            const char *direction = houseconfig_string (element, ".dir");
            if (!direction) signal->direction = 1; // default is "up".
            else if (strsame (direction, "up")) signal->direction = 1;
            else if (strsame (direction, "down")) signal->direction = -1;
            else DEBUG (__FILE__ ": invalid direction %s for signal %s\n", direction, signal->id);

            signal->location.post = houseconfig_integer (element, ".post");
            signal->location.line = houseconfig_string (element, ".line");
            if (signal->location.line) {
                segmentindex =
                    houserail_topology_search_by_location (signal->location.line, signal->location.post);
            } else {
                const char *id = houseconfig_string (element, ".segment");
                if (!id) {
                    DEBUG (__FILE__ ": no location for signal %s\n", signal->id);
                    continue;
                }
                segmentindex = houserail_topology_search_by_id (id);
                if (segmentindex >= 0) {
                    signal->location.post += TopologySegments[segmentindex].low;
                    signal->location.line = TopologySegments[segmentindex].line;
                }
            }

            if (segmentindex < 0) {
                DEBUG (__FILE__ ": invalid location for signal %s\n", signal->id);
                continue;
            }
            struct TrackSegment *segment = TopologySegments + segmentindex;
            signal->location.segment = segment->id;

            signal->nextonsegment = segment->signals;
            segment->signals = i;

            int index = echttp_hash_insert (&TopologySignalsHash, signal->id);
            if ((index > 0) && (index <= TopologySignalsCount))
                TopologySignalsMap[index] = i;

            // What is that signal protecting?
            signal->entry = -1;
            const char *entry = houseconfig_string (element, ".entry");
            if (entry) {
                signal->entry = houserail_topology_search_by_id (entry);
                if (signal->entry < 0) {
                    DEBUG (__FILE__ ": invalid entry %s for signal %s\n", entry, signal->id);
                    continue;
                }
            } else if (signal->direction > 0) {
                signal->entry = TopologySegments[segmentindex].next;
            } else if (signal->direction < 0) {
                signal->entry = TopologySegments[segmentindex].previous;
            }

            signal->protected = -1;
            const char *defend = houseconfig_string (element, ".defend");
            if (defend) {
                signal->protected = houserail_topology_search_by_id (defend);
                if (signal->protected < 0) {
                    DEBUG (__FILE__ ": invalid link segment %s for signal %s\n", defend, signal->id);
                    continue;
                }
                const struct TrackSegment *segment = TopologySegments + signal->protected;
                if ((segment->branch < 0) &&
                    (!segment->feature) &&
                    (!TopologyModels[segment->model].feature)) {

                    // If not in front of a switch or a feature, a signal
                    // protects the area between two segments. Refer to
                    // the lower index segment.
                    if (signal->protected > segmentindex)
                        signal->protected = segmentindex;
                }
            } else if (signal->entry >= 0) {
                int protected = signal->entry;
                const struct TrackSegment *segment = TopologySegments + protected;
                const struct TrackModel *model = TopologyModels + segment->model;

                if (segment->branch >= 0) {

                    // In front of a switch, the signal is part of a group
                    // that protects a route.
                    // When there is a group of adjacent switches, the signal
                    // links to the most upstream switch. That was calculated
                    // before, when loading segments.
                    signal->protected = segment->protected;

                } else if (segment->feature || model->feature) {

                    // Protect a sequence of same feature segments
                    // (typically bridges.)
                    const char *feature = model->feature;
                    if (!feature) feature = segment->feature;

                    int upstream = TopologySegments[protected].previous;
                    while (upstream >= 0) {
                        segment = TopologySegments + upstream;
                        model = TopologyModels + segment->model;
                        if ((!strsame (segment->feature, feature)) &&
                            (!strsame (model->feature, feature))) break;
                        protected = upstream;
                        upstream = segment->previous;
                    }
                    signal->protected = protected; // Protects the feature.

                } else {

                    // If not in front of a switch or a bridge, a signal
                    // protects the area between two segments. Refer to
                    // the lower index segment.
                    if (protected > segmentindex)
                        signal->protected = segmentindex;
                    else
                        signal->protected = protected;
                }

                DEBUG (__FILE__ ": signal %s is on segment %s at %s %d protecting entry %s of group %s direction %s\n",
                       signal->id, signal->location.segment,
                       signal->location.line, signal->location.post,
                       (signal->entry >= 0)?TopologySegments[signal->entry].id:"(nothing)",
                       (signal->protected >= 0)?TopologySegments[signal->protected].id:"(nothing)",
                       (signal->direction>0)?"up":((signal->direction<0)?"down":"unknown"));
            }
        }
    }

    // When everything loaded well, set the global options for this layout.

    int value = houseconfig_integer (track, ".speeds.restricted");
    if (value <= 0) return "No Restricted speed found";
    TopologyOptions.restrictedSpeed = value;
    DEBUG (__FILE__ ": restricted speed set to %d\n", TopologyOptions.restrictedSpeed);

    value = houseconfig_integer (track, ".speeds.reverse");
    if (value <= 0) return "No switch reverse speed found";
    TopologyOptions.switchReverseSpeed = value;
    DEBUG (__FILE__ ": switch reverse speed set to %d\n", TopologyOptions.switchReverseSpeed);

    value = houseconfig_integer (track, ".periods.poll");
    if (value > 0) {
        if ((value < 10) || (value >= 1000)) return "Invalid poll period";
        TopologyOptions.fieldPollPeriod = value;
        DEBUG (__FILE__ ": field poll period set to %d\n", TopologyOptions.fieldPollPeriod);
    }

    TopologyOptions.postDistance =
        houseconfig_integer (track, ".distances.post");
    if (TopologyOptions.postDistance <= 0)
        TopologyOptions.postDistance = PRECISION;
    DEBUG (__FILE__ ": post distance set to %d\n", TopologyOptions.postDistance);

    value = houseconfig_integer (track, ".distances.stop");
    if (value <= 0) return "No stop distance found";
    TopologyOptions.stopDistance = value;
    DEBUG (__FILE__ ": stop distance set to %d\n", TopologyOptions.stopDistance);

    value = houseconfig_integer (track, ".distances.slow");
    if (value <= 0) return "No slow distance found";
    TopologyOptions.slowDistance = value;
    DEBUG (__FILE__ ": slow distance set to %d\n", TopologyOptions.slowDistance);

    const char *option = houseconfig_string (track, ".display.signal.foot");
    TopologyOptions.showSignalFoot = strsame (option, "hide")?0:1;
    DEBUG (__FILE__ ": %sshow signal foot\n",
           TopologyOptions.showSignalFoot?"":"do not ");

    option = houseconfig_string (track, ".display.signal.light");
    if (strsame (option, "arrow")) {
        TopologyOptions.showSignalLightStyle = SIGNAL_ARROW;
        DEBUG (__FILE__ ": show signal light with arrow\n");
    } else {
        TopologyOptions.showSignalLightStyle = SIGNAL_CLASSIC;
        TopologyOptions.showSignalLightFirst = strsame (option, "first");
        DEBUG (__FILE__ ": show signal light %s\n",
               TopologyOptions.showSignalLightFirst?"first":"last ");
    }

    TopologyOptions.backgroundColor =
        houseconfig_string (track, ".display.colors.background");
    TopologyOptions.foregroundColor =
        houseconfig_string (track, ".display.colors.foreground");
    TopologyOptions.auxiliaryColor =
        houseconfig_string (track, ".display.colors.auxiliary");
    DEBUG (__FILE__ ": display background %s, foreground %s auxiliary %s\n",
           TopologyOptions.backgroundColor?TopologyOptions.backgroundColor:"default",
           TopologyOptions.foregroundColor?TopologyOptions.foregroundColor:"default",
           TopologyOptions.auxiliaryColor?TopologyOptions.auxiliaryColor:"default");

    // Preprocessing for end of track.
    // The goal here is to automatically slow trains when they approach,
    // and stop trains when they arrive at, a line's end.
    // For each end of track this retrieves what segments are within
    // the stop and slow areas.

    for (i = 0; i < TopologySegmentsCount; ++i) {

        struct TrackRange slow;
        struct TrackRange stop;
        slow.line = stop.line = 0;
        struct TrackSegment *segment = TopologySegments + i;

        if (segment->next < 0) {

           // The end of line is met while going in the up direction
           // This code backtrack in the down direction to find where
           // the stop and slow areas start.

           stop.line = slow.line = segment->line;
           stop.high = segment->high; // That's the end point.
           stop.low = segment->high - TopologyOptions.stopDistance;
           slow.high = stop.low;
           slow.low = segment->high - TopologyOptions.slowDistance;
           DEBUG (__FILE__ ": track %s ends up at post %d, slow %d to %d, stop %d to %d\n", stop.line, segment->high, slow.low, slow.high, stop.low, stop.high);

           struct TrackSegment *cursor = segment;

           while (stop.low < cursor->high) {
              stop.segment = cursor->id;
              cursor->ending = 1;
              cursor->stop = stop;
              if (cursor->high < cursor->stop.high)
                  cursor->stop.high = cursor->high;
              if (stop.low > cursor->low) break; // The stop area ends here
              cursor->stop.low = cursor->low;
              DEBUG (__FILE__ ": stop zone covers segment %s from %d to %d\n",
                     cursor->id, cursor->stop.low, cursor->stop.high);

              if (cursor->previous < 0) goto nextend;
              cursor = TopologySegments + cursor->previous;
              if (!strsame (cursor->line, stop.line)) goto nextend;
           }
           DEBUG (__FILE__ ": stop zone covers segment %s from %d to %d\n",
                  cursor->id, cursor->stop.low, cursor->stop.high);

           while (slow.low < cursor->high) {

              slow.segment = cursor->id;
              cursor->ending = 1;
              cursor->slow = slow;
              if (cursor->high < cursor->slow.high)
                  cursor->slow.high = cursor->high;
              if (slow.low > cursor->low) break; // The slow area ends here
              cursor->slow.low = cursor->low;
              DEBUG (__FILE__ ": slow zone covers segment %s from %d to %d\n",
                     cursor->id, cursor->slow.low, cursor->slow.high);
              if (cursor->previous < 0) goto nextend;
              cursor = TopologySegments + cursor->previous;
              if (!strsame (cursor->line, stop.line)) goto nextend;
           }
           DEBUG (__FILE__ ": slow zone covers segment %s from %d to %d\n",
                  cursor->id, cursor->slow.low, cursor->slow.high);

        } else if (segment->previous < 0) {

           // The end of line is met while going in the down direction
           // This code backtrack in the up direction to find where
           // the stop and slow areas start.

           stop.line = slow.line = segment->line;
           stop.low = segment->low; // That's the end point.
           stop.high = segment->low + TopologyOptions.stopDistance;
           slow.low = stop.high;
           slow.high = segment->low + TopologyOptions.slowDistance;
           DEBUG (__FILE__ ": track %s ends down at post %d, slow %d to %d, stop %d to %d\n", stop.line, segment->low, slow.low, slow.high, stop.low, stop.high);

           struct TrackSegment *cursor = segment;

           while (stop.high > cursor->low) {

              stop.segment = cursor->id;
              cursor->ending = -1;
              cursor->stop = stop;

              if (cursor->low > cursor->stop.low)
                  cursor->stop.low = cursor->low;
              if (stop.high < cursor->high) break; // The stop area ends here
              cursor->stop.high = cursor->high;
              DEBUG (__FILE__ ": stop zone covers segment %s from %d to %d\n",
                     cursor->id, cursor->stop.low, cursor->stop.high);

              if (cursor->next < 0) goto nextend;
              cursor = TopologySegments + cursor->next;
              if (!strsame (cursor->line, stop.line)) goto nextend;
           }
           DEBUG (__FILE__ ": stop zone covers segment %s from %d to %d\n",
                  cursor->id, cursor->stop.low, cursor->stop.high);

           while (slow.high > cursor->low) {
              slow.segment = cursor->id;
              cursor->ending = -1;
              cursor->slow = slow;

              if (cursor->low > cursor->slow.low)
                  cursor->slow.low = cursor->low;
              if (slow.high < cursor->high) break; // The slow area ends here
              cursor->slow.high = cursor->high;
              DEBUG (__FILE__ ": slow zone covers segment %s from %d to %d\n",
                     cursor->id, cursor->slow.low, cursor->slow.high);

              if (cursor->next < 0) goto nextend;
              cursor = TopologySegments + cursor->next;
              if (!strsame (cursor->line, stop.line)) goto nextend;
           }
           DEBUG (__FILE__ ": slow zone covers segment %s from %d to %d\n",
                  cursor->id, cursor->slow.low, cursor->slow.high);

        }
        nextend: // Used as a multi-level continue.
    }

    if (BillMode) {
        printf ("Bill of Material\n\n");
        printf ("----- Track Model --- Count -\n");
        for (i = 0; i < TopologyModelsCount; ++i) {
            const struct TrackModel *model = TopologyModels + i;
            if (model->usage > 0) {
                printf (" %16s  %8d\n", model->id, model->usage);
            }
        }
        printf (  "-----------------------------\n");
    }

    houselog_event ("LAYOUT", "TOPOLOGY", "LOADED",
                    "%d models %d tracks %d detectors %d signals",
                    TopologyModelsCount,
                    TopologySegmentsCount,
                    TopologyDetectorsCount,
                    TopologySignalsCount);
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

    cursor += snprintf (buffer+cursor, size-cursor,
                        ",\"display\":{\"signal\":{\"foot\":\"%s\",\"light\":\"%s\"}}",
                        TopologyOptions.showSignalFoot?"show":"hide",
                        TopologyOptions.showSignalLightFirst?"first":"last");
    if (cursor >= size) goto overflow;

    // Populate the models array.

    const char *prefix = "\"models\":[";
    int start = cursor;
    int i;
    for (i = 0; i < TopologyModelsCount; ++i) {
        const struct TrackModel *model = TopologyModels + i;
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
        const struct TrackSegment *segment = TopologySegments + i;
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
        const struct TrackDetector *detector = TopologyDetectors + i;
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

    // Populate the signals array.

    prefix = (cursor > preamble)?",\"signals\":[":"\"signals\":[";
    start = cursor;
    for (i = 0; i < TopologySignalsCount; ++i) {
        const struct TrackSignal *signal = TopologySignals + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"line\":\"%s\","
                                "\"post\":%d,\"dir\":\"%s\"}",
                            prefix,
                            signal->id,
                            signal->location.line, signal->location.post,
                            (signal->direction > 0)?"up":"down");
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

int houserail_topology_export_segments
        (char *buffer, int size, const char *separator) {

    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    if (TopologySegmentsIndex.count <= 0) return 0; // Is it even possible?

    int cursor = snprintf (buffer, size, "%s\"segment\":[", separator);
    if (cursor >= size) goto overflow;

    // Populate the segments array.

    const char *prefix = "";
    int i;
    for (i = 0; i < TopologySegmentsIndex.count; ++i) {
        const struct RangeElement *range = TopologySegmentsIndex.elements + i;
        int index = TopologySegmentsIndex.elements[i].value;
        const struct TrackSegment *segment = TopologySegments + index;

        const char *suffix = "";
        if (segment->branch >= 0) {
            // A switch is listed twice in the index, because it belongs to
            // different lines on the normal and reverse sides.
            if (strsame (segment->line, TopologySegmentsIndex.elements[i].line))
                suffix = "~normal";
            else
                suffix = "~reverse";
        }
        cursor += snprintf (buffer+cursor, size-cursor,
                        "%s[\"%s%s\",\"%s\",%d,%d]",
                        prefix, segment->id, suffix,
                        range->line, range->low, range->high);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    return 0;
}

int houserail_topology_model_count (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologyModelsCount;
}

const struct TrackModel *houserail_topology_models (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologyModels;
}

int houserail_topology_segment_count (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologySegmentsCount;
}

const struct TrackSegment *houserail_topology_segments (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologySegments;
}

int houserail_topology_detector_count (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologyDetectorsCount;
}

const struct TrackDetector *houserail_topology_detectors (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologyDetectors;
}

int houserail_topology_signal_count (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologySignalsCount;
}

const struct TrackSignal *houserail_topology_signals (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return TopologySignals;
}

const struct TrackOptions *houserail_topology_options (void) {
    if (!TopologyOptions.name) return 0; // No track layout was loaded.
    return &TopologyOptions;
}

