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
 * houserail_train.c - Train management (attributes, tracking, etc.)
 */
const char *houserail_train_initialize (int argc, const char **argv);

const char *houserail_train_move (const char *id, const char *to, int slow);
const char *houserail_train_stop (const char *id, int emergency);

void houserail_train_track (const char *name,
                            long long timestamp, const char *state);

int houserail_train_status (char *buffer, int size);

void houserail_train_background (time_t now);

