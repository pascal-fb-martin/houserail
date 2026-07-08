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
#include "houserail_track.h"

const char *houserail_train_initialize (int argc, const char **argv);

void houserail_train_testmode (int enabled);

void houserail_train_tracking (const struct TrackRange *area,
                               int occupied,
                               long long timestamp);

const char *houserail_train_move (const char *id, const char *to, int slow);
const char *houserail_train_stop (const char *id, int emergency);

const char *houserail_train_enter (const char *id,
                                   const char *facing, int orientation);
const char *houserail_train_park (const char *id);
const char *houserail_train_consist (const char *id,
                                     const char *cars[], int count);
const char *houserail_train_delete (const char *id);

const char *houserail_train_reload (void);

const struct TrackLocation *houserail_train_head (const char *id);
const struct TrackLocation *houserail_train_tail (const char *id);

int houserail_train_export (char *buffer, int size, const char *separator);
int houserail_train_status (char *buffer, int size);
int houserail_train_locate (char *buffer, int size);

void houserail_train_background (time_t now);

