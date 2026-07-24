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
 * houserail_topology.h - Load and store track and signaling data.
 */

#include "houserail_types.h"

struct TrackOptions {

    const char *name;
    const char *description;

    int fieldPollPeriod;
    int restrictedSpeed;
    int switchReverseSpeed;
    int stopDistance;
    int slowDistance;

    int scale;
    int postDistance;
};

struct TrackDisplayShape {

    int arc;      // 0 means straight.
    int radius;
    int straight; // Physical length of the normal side. For switch only.
};

struct TrackDisplayLocation {
    int x;
    int y;
    int angle; // This is a point with a direction of "travel".
};

struct TrackModel {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    int length;  // Length on the normal side.
    int reverse; // Length on the reverse side, 0 if not a switch.
    int civil;   // Civil speed limit on that track.

    struct TrackDisplayShape shape;
};

struct TrackSegment {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    const char *line; // The name of the line going through the normal points.
    int start;        // Starting milepost for this segment (optional).

    // The following attributes are calculated by following the linkages.

    int model;    // Reference index to the track model table.
    int next;     // Link from exit point to the next segment. -1 if none.
    int previous; // Link from entry point to the previous segment. -1 if none.

    // The following items are for switches only, valid if branch >= 0.
    int common;   // The adjacent segment connected to the common switch end
    int branch;   // The adjacent segment connected to the reverse point.

    // The following items are to handle end of track.
    int ending;   // 1: ending up, -1: ending down, 0: no end near.
    struct TrackRange stop;
    struct TrackRange slow;

    // The following attributes are calculated by following the topology from
    // the terminal point marked as the origin.
    int low;
    int high;

    int detector; // First detector on this segment.

    int curve;
    struct TrackDisplayLocation display; // Optional, {0,0,0} if not present.
    struct TrackDisplayShape shape;      // A copy from the model.
};

struct TrackDetector {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    int segment;
    int next;     // Next detector on the same segment.
    struct TrackRange area; // RESTRICTION: a detector covers only one segment.
};

void houserail_topology_testmode (int enabled);

const char *houserail_topology_initialize (int argc, const char **argv);
const char *houserail_topology_reload (void);
int houserail_topology_export (char *buffer, int size, const char *sep);

int houserail_topology_model_count (void);
const struct TrackModel *houserail_topology_models (void);

int houserail_topology_segment_count (void);
const struct TrackSegment *houserail_topology_segments (void);

int houserail_topology_detector_count (void);
const struct TrackDetector *houserail_topology_detectors (void);

const struct TrackOptions *houserail_topology_options (void);

int houserail_topology_search_model (const char *id);
int houserail_topology_search_by_id (const char *id);
int houserail_topology_search_by_location (const char *line, int post);
int houserail_topology_search_detector (const char *id);

