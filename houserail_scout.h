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
 * houserail_scout.h - Search a location on a track layout.
 */

struct RangeElement {
   const char *line;
   int low;
   int high;
   int value;
};

struct RangeIndex {
   int size;
   int count;
   int *ordered;
   struct RangeElement *elements;
};

void houserail_scout_initialize (struct RangeIndex *index, int size);
void houserail_scout_add (struct RangeIndex *index,
                          int value, const char *line, int low, int high);
void houserail_scout_finalize (struct RangeIndex *index);

void houserail_scout_erase (struct RangeIndex *index);

int houserail_scout_inside (const struct RangeIndex *index,
                            const char *line, int post);

