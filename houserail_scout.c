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
 * houserail_scout.c - Search a location on a track layout.
 *
 * SYNOPSYS:
 *
 * This module handles searches based on lines and posts. The operations are:
 *
 * - searching for a specific segment matching a given location, or
 *
 * - searching for the closest segment or detector to a given location
 *   in a given direction of travel.
 *
 * The implementation uses dichotomy through a list sorted by line and post.
 *
 * void houserail_scout_initialize (struct RangeIndex *index, int size);
 * void houserail_scout_add (struct RangeIndex *index,
 *                           int value, const char *line, int low, int high);
 * void houserail_scout_finalize (struct RangeIndex *index);
 *
 *    Build a search index for the set of provided track ranges.
 *
 * void houserail_scout_erase (struct RangeIndex *index);
 *
 *    Clear up the whole index and free all its resources.
 *
 * int houserail_scout_nearest (const struct RangeIndex *index,
 *                              const char *line, int post, int direction);
 *
 *    Search the limit that is the nearest to the given location in the given
 *    direction. Direction is either increasing (1) or decreasing (-1).
 *
 *    This function returns the index that was provided when the element
 *    was added (see houserail_scout_add() above).
 *
 * int houserail_scout_inside (const struct RangeIndex *index,
 *                             const char *line, int post);
 *
 *    Search the element that covers the specified location.
 *
 *    This function returns the index that was provided when the element
 *    was added (see houserail_scout_add() above).
 *
 * LIMITATIONS:
 *
 *    This implementation only supports up to 32767 items referenced.
 *    It was estimated that no layout would have that many segments.
 *
 *    This implementation does not support overlapping ranges (no strict
 *    order is possible).
 */

#include <unistd.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"

#include "houserail_track.h"
#include "houserail_scout.h"

#define DEBUG if (echttp_isdebug()) printf

static int houserail_scout_compare (const void *p1, const void *p2) {

    const struct RangeElement *e1 = (struct RangeElement *)p1;
    const struct RangeElement *e2 = (struct RangeElement *)p2;

    // The line has precedence.
    int result = strcmp (e1->line, e2->line);
    if (result != 0) return result;

    return e1->high - e2->high;
}

void houserail_scout_initialize (struct RangeIndex *index, int size) {

    index->count = 0;
    index->size = size;
    if (size > 0) {
        index->elements = calloc (size, sizeof(struct RangeElement));
    } else {
        index->elements = 0;
    }
}

void houserail_scout_add (struct RangeIndex *index,
                          int value, const char *line, int low, int high) {

    if (index->count >= index->size) return;

    struct RangeElement *element = index->elements + index->count;
    element->value = value;
    element->line = line;
    if (high >= low) {
        element->high = high;
        element->low = low;
    } else {
        element->high = low;
        element->low = high;
    }
    index->count += 1;
}

void houserail_scout_finalize (struct RangeIndex *index) {
    if (index->count > 1)
       qsort (index->elements, index->count, sizeof(struct RangeElement),
              houserail_scout_compare);
}

void houserail_scout_erase (struct RangeIndex *index) {

    if (index->elements) {
        free (index->elements);
        index->elements = 0;
    }
    index->count = 0;
    index->size = 0;
}

static int houserail_search_dichotomy_up (const struct RangeIndex *index,
                                          const char *line, int post) {

    int bottom = 0;
    int top = index->count - 1;

    // Eliminate indirections
    const struct RangeElement *elements = index->elements;
    const struct RangeElement *element;

    while (bottom < top - 1) {
        int mid = bottom + (top - bottom) / 2;
        element = elements + mid;

        int lineorder = strcmp (element->line, line);
        int order = lineorder ? lineorder : (element->low - post);

        if (order == 0) return element->value; // Exact location was found.

        if (order > 0) top = mid;
        else           bottom = mid;
    }
    element = elements + top;
    int lineorder = strcmp (element->line, line);
    if ((!lineorder) && (post < element->low)) return element->value;

    return -1; // Could not find something that matches.
}

static int houserail_search_dichotomy_down (const struct RangeIndex *index,
                                            const char *line, int post) {

    int bottom = 0;
    int top = index->count - 1;

    // Eliminate indirections
    const struct RangeElement *elements = index->elements;
    const struct RangeElement *element;

    while (bottom < top - 1) {
        int mid = bottom + (top - bottom) / 2;
        element = elements + mid;

        int lineorder = strcmp (element->line, line);
        int order = lineorder ? lineorder : (element->high - post);

        if (order == 0) return element->value; // Exact location was found.

        if (order > 0) top = mid;    // The mid point is higher.
        else           bottom = mid; // The mid point is lower.
    }
    element = elements + bottom;
    int lineorder = strcmp (element->line, line);
    if ((!lineorder) && (post > element->high)) return element->value;

    return -1; // Could not find something that matches.
}
                                
int houserail_scout_nearest (const struct RangeIndex *index,
                             const char *line, int post, int direction) {

    if (direction >= 0)
        return houserail_search_dichotomy_up (index, line, post);
    return houserail_search_dichotomy_down (index, line, post);
}

int houserail_scout_inside (const struct RangeIndex *index,
                            const char *line, int post) {

    if (!line) return -1;

    int bottom = 0;
    int top = index->count - 1;

    // Eliminate indirections
    const struct RangeElement *elements = index->elements;

    while (bottom < top - 1) { // As long as we have a segment in between
        int mid = bottom + (top - bottom) / 2;
        const struct RangeElement *element = elements + mid;

        // Is the location within that segment?
        int order = strcmp (element->line, line);
        if (order == 0) {
            if (post > element->high) order = -1; // Mid is lower.
            else if (post < element->low) order = 1; // Mid is higher.
            else return element->value; // The location is within this segment.
        }
        if (order > 0) top = mid;    // Mid is the new high bound
        else           bottom = mid; // Mid is the new low bound
    }

    const struct RangeElement *element = elements + top;
    if (!strcmp (element->line, line)) {
        if ((post > element->low) && (post < element->high))
            return element->value; // Within the top segment.
    }
    if (bottom == top) return -1;

    element = elements + bottom;
    if (!strcmp (element->line, line)) {
        if ((post > element->low) && (post < element->high))
            return element->value; // Within the bottom segment.
    }
    return -1; // Could not find something that matches.
}

