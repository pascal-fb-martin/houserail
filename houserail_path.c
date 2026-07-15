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
 * houserail_path.c - A module to create, merge or adjust track paths.
 *
 * SYNOPSYS:
 *
 * This module handles basic operations on paths. The main objective is to
 * avoid recalculating full paths everytime. Instead the system can take
 * advantage of an existing path and only recalculate an extension once.
 *
 * You can see this module as providing a way to cache existing paths so
 * that the application does not walk the tracks repeatedly.
 *
 * A path is oriented according to the train's direction of travel. The path
 * for a stopped train must be in the tail-to-head direction.
 *
 * The namig  convention in this module is that the name "direction"
 * denotes the train's (and path's) direction, while the name "orientation"
 * denotes an orientation relative to the path's direction.
 *
 * A positive train direction will create a path with increasing posts sections.
 * A negative train direction will create a path with decreasing posts sections.
 *
 * A positive orientation follows the path's direction, while a negative
 * orientation follows the reverse of the path direction.
 *
 * A path provided to any of these functions must always have been initialized.
 * This is typically done by setting every field to 0 (see TRACKPATHNULL).
 *
 * int houserail_path_covers (const struct TrackPath *path,
 *                            const struct TrackRange *area);
 *
 *    Return 1 if the area intersects with the specified path.
 *
 * int houserail_path_span (struct TrackPath *path,
 *                          const struct TrackLocation *limit1,
 *                          int length, int direction);
 *
 *    Set a new path that spans the specified length starting at the specified
 *    limit in the specified direction. Any existing section in the path is
 *    erased.
 *
 * int houserail_path_set (struct TrackPath *path,
 *                         const struct TrackLocation *limit1,
 *                         const struct TrackLocation *limit2,
 *                         int direction);
 *
 *    Set a path between two locations, in the specified direction.
 *    Return 1 on success, 0 on failure.
 *
 *    If there is any existing path sections, and the direction is the same
 *    as the existing direction, the function tries to rollup, extend and/or
 *    truncate what exists if applicable. Otherwise the path is wiped out
 *    and a new path is created.
 *
 * int houserail_path_lengthen (struct TrackPath *path, int distance);
 *
 * int houserail_path_extend (struct TrackPath *path,
 *                            const struct TrackLocation *point);
 *
 *    Extend the end of an existing path in the direction of the path.
 *    Function houserail_path_lengthen() extends the path by a defined length.
 *    Function houserail_path_extend() extends the path to a specified point.
 *    Both functions return 1 on success, 0 on failure.
 *
 * int houserail_path_rollup (struct TrackPath *path,
 *                            const struct TrackLocation *point);
 *
 *    Shorten an existing path so that it starts at the provided point.
 *
 * int houserail_path_truncate (struct TrackPath *path,
                                const struct TrackLocation *point);
 *
 *    Shorten an existing path so that it ends at the provided point.
 *
 * int houserail_path_move (const struct TrackPath *path,
 *                          struct TrackLocation *point,
 *                          int distance, int orientation);
 *
 *    Move a point along the specified path.
 *
 * void houserail_path_reverse (struct TrackPath *path);
 *
 *    Reverse the order of sections in a path.
 *
 * void houserail_path_turn (struct TrackPath *path, int direction);
 *
 *    Change the direction of the path to the one specified.
 *
 * void houserail_path_erase (struct TrackPath *path);
 *
 *    Empty the specified path.
 *
 * void houserail_path_release (struct TrackPath *path);
 *
 *    Deallocate every resources for this path.
 */

#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <echttp.h>
#include <echttp_libc.h>

#include "houserail_track.h"
#include "houserail_path.h"

#define DEBUG if (echttp_isdebug()) printf

static int houserail_path_within (const struct TrackRange *area,
                                  const struct TrackLocation *point) {

    if (strcasecmp (area->line, point->line)) return 0;

    // Do not assume that the range's posts are properly sorted.
    //
    int high = (area->high >= area->low)? area->high : area->low;
    if (point->post > high) return 0;
    int low = (area->high >= area->low)? area->low : area->high;
    if (point->post < low) return 0;

    return 1;

}

static void houserail_path_scrub (struct TrackPath *path, int index) {

    if (index > 0) {
        struct TrackRange *sections = path->sections;
        int i;
        for (i = index; i < path->count; ++i) {
            sections[i-index] = sections[i];
        }
        path->count -= index;
    }
}

int houserail_path_covers (const struct TrackPath *path,
                           const struct TrackRange *area) {

    struct TrackLocation point1, point2;
    point1.line = point2.line = area->line;
    point1.segment = point2.segment = 0;
    point1.post = area->low;
    point2.post = area->high;

    struct TrackRange *sections = path->sections;
    int i;
    for (i = 0; i < path->count; ++i) {
        if (houserail_path_within (sections+i, &point1)) return 1;
        if (houserail_path_within (sections+i, &point2)) return 1;
    }
    return 0;
}

int houserail_path_span (struct TrackPath *path,
                         const struct TrackLocation *limit1,
                         int length, int direction) {

    if (direction == 0) return 0;

    if (path->count > 0) houserail_path_erase (path);

    path->direction = direction;
    if (path->size <= 0) {
        path->size = 16; // FIXME: arbitrary.
        path->sections = calloc (path->size, sizeof(struct TrackRange));
    }
    int count = houserail_track_walk (path->sections, path->size,
                                      limit1, 0, direction, length);
    if (count > 0) {
        path->count = count;
        return 1;
    }
    return 0;
}

int houserail_path_set (struct TrackPath *path,
                        const struct TrackLocation *limit1,
                        const struct TrackLocation *limit2, int direction) {

    if (direction == 0) return 0;

    // Reuse whatever portion of the existing path is still relevant.
    if (path->count > 0) {
        if ((path->direction == direction) &&
            (houserail_path_rollup (path, limit1))) {
            if (houserail_path_truncate (path, limit2)) return 1;
            return houserail_path_extend (path, limit2);
        }
        houserail_path_erase (path);
    }

    // Recalculate a path from scratch.
    path->direction = direction;
    if (path->size <= 0) {
        path->size = 16; // FIXME: arbitrary.
        path->sections = calloc (path->size, sizeof(struct TrackRange));
    }
    int count = houserail_track_walk (path->sections, path->size,
                                      limit1, limit2, direction, 0);
    if (count > 0) {
        path->count = count;
        return 1;
    }
    return 0;
}

static void houserail_path_merge (struct TrackPath *path, int added) {

    struct TrackRange *last = path->sections + path->count - 1;

    if (strsame (last->line, last[1].line) && (last->high == last[1].low)) {
        // Merge these two segments that are in continuity.
        last->high = last[1].high;
        added -= 1;
        int i;
        for (i = 1 ; i <= added; ++i) last[i] = last[i+1];
    }
    path->count += added;
}

int houserail_path_lengthen (struct TrackPath *path, int distance) {

    if (path->count <= 0) return 0;

    int direction = path->direction;
    struct TrackRange *last = path->sections + path->count - 1;
    struct TrackLocation start;
    start.line = last->line;
    start.segment = last->segment;
    start.post = last->high;

    int count = houserail_track_walk (last+1, path->size - path->count,
                                       &start, 0, direction, distance);
    if (count > 0) {
        houserail_path_merge (path, count);
        return 1;
    }
    return 0;
}

int houserail_path_extend (struct TrackPath *path,
                           const struct TrackLocation *point) {

    if (path->count <= 0) return 0;

    int direction = path->direction;
    struct TrackRange *last = path->sections + (path->count - 1);

    // If the new endpoint is on the same line as the last section,
    // just extend the last section.
    if (strsame (last->line, point->line)) {
        last->high = point->post;
        return 1;
    }

    struct TrackLocation start;
    start.line = last->line;
    start.segment = 0;
    start.post = last->high;

    int count = houserail_track_walk (last+1, path->size - path->count,
                                      &start, point, direction, 0);
    if (count > 0) {
        houserail_path_merge (path, count);
        return 1;
    }
    return 0;
}

int houserail_path_rollup (struct TrackPath *path,
                           const struct TrackLocation *point) {

    struct TrackRange *sections = path->sections;
    int i;
    for (i = 0; i < path->count; ++i) {
        if (!houserail_path_within (sections+i, point)) continue;

        sections[i].low = point->post;
        houserail_path_scrub (path, i);
        return 1;
    }
    return 0; // No changes made.
}

int houserail_path_truncate (struct TrackPath *path,
                             const struct TrackLocation *point) {

    struct TrackRange *sections = path->sections;
    int i;
    for (i = 0; i < path->count; ++i) {
        if (!houserail_path_within (sections+i, point)) continue;

        sections[i].high = point->post;
        path->count = i + 1; // Truncate what is left over.
        return 1;
    }
    return 0; // No changes made.
}

int houserail_path_move (const struct TrackPath *path,
                         struct TrackLocation *point,
                         int distance, int orientation) {

    struct TrackRange *sections = path->sections;
    struct TrackLocation original = *point;

    int i;
    for (i = 0; i < path->count; ++i) {
        if (houserail_path_within (sections+i, point)) break;
    }
    if (i >= path->count) return 0; // The point is not within the path?

    // Move the point forward or backward relative to the path.
    // If the point exits the current section, iterate to the subsequent
    // sections until the point is within a section, or the end of the path.
    int post = point->post;
    int direction = path->direction * orientation;
    point->segment = 0; // This point may be moving out of the known segment.
    for (;;) {
        struct TrackRange *section = sections + i;
        point->line = section->line;
        point->post = (direction >= 0) ? post + distance : post - distance;
        if (houserail_path_within (section, point)) return 1;

        // Remove the distance to the end of the section.
        int end = (orientation > 0) ? section->high : section->low;
        distance -= abs(end - post);

        if (orientation >= 0) {
            if (++i >= path->count) { // Reached the end of the path: backtrack.
                *point = original;
                return 0;
            }
        } else {
            if (--i < 0) { // Reached the end of the path: backtrack.
                *point = original;
                return 0;
            }
        }
        // Step to the beginning of this new section.
        post = (orientation > 0) ? sections[i].low : sections[i].high;
    }
    return 0; // Make gcc happy.
}

static void houserail_path_reverse (struct TrackPath *path) {

    struct TrackRange temp;
    struct TrackRange *sections = path->sections;
    int i;
    int loop = path->count / 2;
    int end = path->count - 1;
    for (i = 0; i < loop; ++i) {
        temp = sections[i];
        sections[i].line = sections[end-i].line;
        sections[i].low = sections[end-i].high;
        sections[i].high = sections[end-i].low;
        sections[end-i].line = temp.line;
        sections[end-i].low = temp.high;
        sections[end-i].high = temp.low;
    }
    if (path->count & 1) {
        int temp = sections[loop].low;
        sections[loop].low = sections[loop].high;
        sections[loop].high = temp;
    }
    path->direction = 0 - path->direction;
}

void houserail_path_turn (struct TrackPath *path, int direction) {

    if (path->direction != direction) {
        houserail_path_reverse (path);
        path->direction = direction;
    }
}

void houserail_path_erase (struct TrackPath *path) {
    path->count = 0;
    path->direction = 0;
}

void houserail_path_release (struct TrackPath *path) {
    if (path->size > 0) {
       free (path->sections);
    }
    *path = TrackPathNew;
}

