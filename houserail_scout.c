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
 * This module handles searches a segment based on line name and post.
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
 *    This implementation does not support overlapping ranges (no strict
 *    order is possible).
 *
 * BUGS:
 *
 *    All segments overlap at their limits. The overlap length is 0,
 *    but querying an exact limit will return one segment or the other,
 *    depending on the search iterations.
 */

#include <unistd.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <echttp.h>
#include <echttp_libc.h>

#include "houserail_track.h"
#include "houserail_scout.h"

#define DEBUG if (echttp_isdebug()) printf

static int houserail_scout_compare (const void *p1, const void *p2) {

    const struct RangeElement *e1 = (struct RangeElement *)p1;
    const struct RangeElement *e2 = (struct RangeElement *)p2;

    // The line has precedence.
    int result = strcasecmp (e1->line, e2->line);
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
        int order = strcasecmp (element->line, line);
        if (order == 0) {
            if (post > element->high) order = -1; // Mid is lower.
            else if (post < element->low) order = 1; // Mid is higher.
            else return element->value; // The location is within this segment.
        }
        if (order > 0) top = mid;    // Mid is the new high bound
        else           bottom = mid; // Mid is the new low bound
    }

    const struct RangeElement *element = elements + top;
    if (strsame (element->line, line)) {
        if ((post >= element->low) && (post <= element->high))
            return element->value; // Within the top segment.
    }
    if (bottom == top) return -1;

    element = elements + bottom;
    if (strsame (element->line, line)) {
        if ((post >= element->low) && (post <= element->high))
            return element->value; // Within the bottom segment.
    }
    return -1; // Could not find something that matches.
}

