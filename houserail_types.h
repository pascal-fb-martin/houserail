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
 * houserail_types.h - Basic types used throughout HouseRail
 */

#ifndef HOUSERAIL_TYPES__H_DEFINED
#define HOUSERAIL_TYPES__H_DEFINED

// Use these two data structures to represent the track network
// in the railroad world. These are the basic blocks for identifying
// the location of trains and features on the tracks.

struct TrackRange {
    const char *segment;
    const char *line;
    int low;
    int high;
};

struct TrackLocation {
    const char *segment;
    const char *line;
    int post;
};

// Use these data structures to represent the track network in the graphic
// world. These are the basic blocks for identifying the location of track
// segments and features on a display pane.

struct TrackShape {
    int arc;      // 0 means straight. In 1/100 of degrees!
    int radius;
    int straight; // Physical length of the normal side. For switch only.
};

struct TrackVertex {
    int x;
    int y;
    int angle; // Warning: in 1/100 of degrees!
};
#endif

