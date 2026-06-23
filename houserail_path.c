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
 *                          int direction, int length);
 *
 *    Set a new path that spans the specified length from the specified
 *    limit. Any existing section in the path is erased.
 *
 * int houserail_path_set (struct TrackPath *path,
 *                         const struct TrackLocation *limit1,
 *                         const struct TrackLocation *limit2,
 *                         int direction);
 *
 *    Set a path between two locations. Return 1 on success, 0 on failure.
 *    If there is any existing path sections, the function tries to rollup,
 *    extend and/or truncate what exists if applicable. Otherwise the path
 *    is wiped out and a new path is built.
 *
 * int houserail_path_lengthen (struct TrackPath *path,
 *                              int distance, int direction);
 *
 * int houserail_path_extend (struct TrackPath *path,
 *                            const struct TrackLocation *point, int direction);
 *
 *    Extend the end of existing path in the provided direction.
 *    The caller may either provide the end point (houserail_path_extend())
 *    or a distance if the exact end point is not known yet.
 *    Both functions return 1 on success, 0 on failure.
 *
 * int houserail_path_rollup (struct TrackPath *path,
 *                            const struct TrackLocation *point, int direction);
 *
 *    Shorten an existing path so that it starts at the provided point.
 *
 * int houserail_path_distance (const struct TrackPath *path,
 *                              const struct TrackLocation *point1,
 *                              const struct TrackLocation *point2,
 *                              int direction);
 *
 *    Calculate the distance between two points within a given path.
 *    Return -1 if any of the points is not within the path.
 *
 * int houserail_path_move (const struct TrackPath *path,
 *                          struct TrackLocation *point,
 *                          int distance, int direction);
 *
 *    Move a point along the specified path.
 *
 * void houserail_path_reverse (struct TrackPath *path);
 *
 *    Reverse the order of sections in a path.
 *
 * void houserail_path_erase (struct TrackPath *path);
 *
 *    Empty the specified path.
 */

#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"

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

static int houserail_path_front (const struct TrackRange *area, int direction) {

    if (area->high > area->low) {
        return (direction >= 0) ? area->high : area->low;
    }
    return (direction >= 0) ? area->low : area->high;
}

static int houserail_path_back (const struct TrackRange *area, int direction) {

    if (area->high > area->low) {
        return (direction >= 0) ? area->low : area->high;
    }
    return (direction >= 0) ? area->high : area->low;
}

static void houserail_path_adjust (struct TrackRange *area,
                                   const struct TrackLocation *point,
                                   int direction) {

    if (area->high > area->low) {
        if (direction > 0) area->low = point->post;
        else area->high = point->post;
    } else {
        if (direction > 0) area->high = point->post;
        else area->low = point->post;
    }
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
                         int direction, int length) {

    if (path->count > 0) houserail_path_erase (path);

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

    // Reuse whatever portion of the existing path is still relevant.
    if (path->count > 0) {
        if (houserail_path_rollup (path, limit1, direction)) {
            if (houserail_path_truncate (path, limit2, direction)) return 1;
            return houserail_path_extend (path, limit2, direction);
        }
        houserail_path_erase (path);
    }

    // Recalculate a path from scratch.
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

int houserail_path_lengthen (struct TrackPath *path,
                             int distance, int direction) {

    if (path->count <= 0) return 0;

    struct TrackRange *last = path->sections + path->count - 1;
    struct TrackLocation start;
    start.line = last->line;
    start.segment = last->segment;
    start.post = houserail_path_front (last, direction);

    int count = houserail_track_walk (last, path->size - path->count,
                                       &start, 0, distance, direction);
    if (count > 0) {
        path->count += count;
        return 1;
    }
    return 0;
}

int houserail_path_extend (struct TrackPath *path,
                           const struct TrackLocation *point, int direction) {

    if (path->count <= 0) return 0;

    struct TrackRange *last = path->sections + (path->count - 1);

    // If the new endpoint is on the same line as the last section,
    // just extend the last section.
    if (!strcmp (last->line, point->line)) {
        if (last->high > last->low) {
            if (direction > 0) last->high = point->post;
            else               last->low = point->post;
        } else {
            if (direction > 0) last->low = point->post;
            else               last->high = point->post;
        }
        return 1;
    }

    struct TrackLocation start;
    start.line = last->line;
    start.segment = last->segment;
    start.post = houserail_path_front (last, direction);

    int count = houserail_track_walk (last+1, path->size - path->count,
                                      &start, point, direction, 0);
    if (count > 0) {
        path->count += count;
        return 1;
    }
    return 0;
}

int houserail_path_distance (const struct TrackPath *path,
                             const struct TrackLocation *point1,
                             const struct TrackLocation *point2,
                             int direction) {

    int distance = 0;
    int i;
    for (i = 0; i < path->count; ++i) {
        const struct TrackRange *cursor = path->sections + i;
        if (!houserail_path_within (cursor, point1)) continue;

        int limit = houserail_path_front (cursor, direction);
        distance = abs (limit - point1->post);
    }
    if (i >= path->count) return -1; // Not within the path.

    for (; i < path->count; ++i) {
        const struct TrackRange *cursor = path->sections + i;
        if (!houserail_path_within (cursor, point2)) continue;

        int limit = houserail_path_back (cursor, direction);
        int delta = limit - point2->post;
        if (delta > 0) distance += delta;
        else           distance -= delta;
        return distance;
    }
    return -1; // Not within the path.
}

int houserail_path_rollup (struct TrackPath *path,
                           const struct TrackLocation *point, int direction) {

    struct TrackRange *sections = path->sections;
    int i;
    for (i = 0; i < path->count; ++i) {
        if (!houserail_path_within (sections+i, point)) continue;

        houserail_path_adjust (sections+i, point, direction);
        houserail_path_scrub (path, i);
        return 1;
    }
    return 0; // No changes made.
}

int houserail_path_truncate (struct TrackPath *path,
                             const struct TrackLocation *point, int direction) {

    struct TrackRange *sections = path->sections;
    int i;
    for (i = 0; i < path->count; ++i) {
        if (!houserail_path_within (sections+i, point)) continue;

        houserail_path_adjust (sections+i, point, (direction > 0)?-1:1);
        path->count = i + 1; // Truncate what is left over.
        return 1;
    }
    return 0; // No changes made.
}

int houserail_path_move (const struct TrackPath *path,
                         struct TrackLocation *point,
                         int distance, int direction) {

    struct TrackRange *sections = path->sections;
    struct TrackLocation original = *point;

    int i;
    for (i = 0; i < path->count; ++i) {
        if (houserail_path_within (sections+i, point)) break;
    }
    if (i >= path->count) return 0; // The point is not within the path?

    // Move the point forward. If the point exits the current section, iterate
    // to the subsequent sections until the point is within a section.
    int post = point->post;
    for (;;) {
        point->post = (direction >= 0) ? post + distance : post - distance;
        if (houserail_path_within (sections+i, point)) return 1;

        distance -= abs (houserail_path_front (sections+i, direction) - post);

        if (++i >= path->count) { // Reached the end of the path: backtrack.
            *point = original;
            return 0;
        }
        point->line = sections[i].line;
        post = houserail_path_back (sections+i, direction);
    }
    return 0; // Make gcc happy.
}

void houserail_path_reverse (struct TrackPath *path) {

    struct TrackRange temp;
    struct TrackRange *sections = path->sections;
    int i;
    int loop = path->count / 2;
    int end = path->count - 1;
    for (i = 0; i < loop; ++i) {
        temp = sections[i];
        sections[i] = sections[end-i];
        sections[end-i] = temp;
    }
}

void houserail_path_erase (struct TrackPath *path) {
    path->count = 0;
}

