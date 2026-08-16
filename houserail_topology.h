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

// List of supported signal styles
#define SIGNAL_CLASSIC 0
#define SIGNAL_ARROW   1

struct TrackOptions {

    const char *name;
    const char *description;

    short fieldPollPeriod;
    short restrictedSpeed;
    short switchReverseSpeed;
    short stopDistance;
    short slowDistance;

    short scale;
    short postDistance;

    char showSignalFoot;
    char showSignalLightFirst;
    char showSignalLightStyle;

    const char *backgroundColor;
    const char *foregroundColor;
};

struct TrackModel {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    int length;  // Length on the normal side.
    int reverse; // Length on the reverse side, 0 if not a switch.
    int civil;   // Civil speed limit on that track.

    struct TrackShape shape;

    const char *feature;

    int usage; // Count how many segment reference this model.
};

struct TrackSegment {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    const char *line; // The name of the line going through the normal points.
    int start;        // Starting milepost for this segment (optional).

    // The following attributes are calculated by following the linkages.

    short model;    // Reference index to the track model table.
    short next;     // Link from exit point to the next segment. -1 if none.
    short previous; // Link from entry point to the previous segment. -1 if none.

    short signals;  // Reference to a list of signals located on this segment.

    // The following items are for switches only, valid if branch >= 0.
    short common;   // The adjacent segment connected to the common switch end
    short branch;   // The adjacent segment connected to the reverse point.

    short detector; // First detector on this segment.

    // The following attributes are calculated by following the topology from
    // the terminal point marked as the origin.
    int low;
    int high;

    // The following attributes drive the end-of-line protection mechanism.
    // These are precalculated during loading. They can also be used to
    // show the end-of-line zones on a track display.
    int ending;   // 1: ending up, -1: ending down, 0: no end near.
    struct TrackRange stop;
    struct TrackRange slow;

    int curve;
    struct TrackShape shape;    // The model's shape, adjusted.
    struct TrackVertex display; // Optional, {0,0,0} if not present.

    const char *feature;
};

struct TrackDetector {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    short segment;
    short next;     // Next detector on the same segment.
    struct TrackRange area; // RESTRICTION: a detector covers only one segment.
};

struct TrackSignal {

    const char *id;
    unsigned int signature; // Seach accelerator.
    int index;              // Self reference.

    int direction;          // The protected direction, 1: up, -1: down
    int protected;          // A unique signature of what is protected.

    struct TrackLocation location;

    short nextonsegment;
};

void houserail_topology_testmode (int enabled);
void houserail_topology_billmode (int enabled);

const char *houserail_topology_initialize (int argc, const char **argv);
const char *houserail_topology_reload (void);

int houserail_topology_export (char *buffer, int size, const char *separator);
int houserail_topology_export_segments
        (char *buffer, int size, const char *separator);

int houserail_topology_model_count (void);
const struct TrackModel *houserail_topology_models (void);

int houserail_topology_segment_count (void);
const struct TrackSegment *houserail_topology_segments (void);

int houserail_topology_detector_count (void);
const struct TrackDetector *houserail_topology_detectors (void);

int houserail_topology_signal_count (void);
const struct TrackSignal *houserail_topology_signals (void);

const struct TrackOptions *houserail_topology_options (void);

int houserail_topology_search_model (const char *id);
int houserail_topology_search_by_id (const char *id);
int houserail_topology_search_by_location (const char *line, int post);
int houserail_topology_search_detector (const char *id);
int houserail_topology_search_signal (const char *id);

