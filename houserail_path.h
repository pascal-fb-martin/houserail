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
 * houserail_path.h - A module to create, merge or adjust track paths.
 */

struct TrackPath {
   int count;
   int size;
   int direction;
   struct TrackRange *sections;
};

#define TRACKPATHNULL {0, 0, 0}

int houserail_path_covers (const struct TrackPath *path,
                           const struct TrackRange *area);

int houserail_path_span (struct TrackPath *path,
                         const struct TrackLocation *limit1,
                         int length, int direction);

int houserail_path_set (struct TrackPath *path,
                        const struct TrackLocation *limit1,
                        const struct TrackLocation *limit2, int direction);

int houserail_path_lengthen (struct TrackPath *path, int distance);

int houserail_path_extend (struct TrackPath *path,
                           const struct TrackLocation *point);

int houserail_path_rollup (struct TrackPath *path,
                           const struct TrackLocation *point);

int houserail_path_truncate (struct TrackPath *path,
                             const struct TrackLocation *point);

int houserail_path_move (const struct TrackPath *path,
                         struct TrackLocation *point,
                         int distance, int orientation);

void houserail_path_turn (struct TrackPath *path, int direction);

void houserail_path_erase (struct TrackPath *path);

