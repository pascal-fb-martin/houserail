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
 * houserail_track.h - Track topology and detectection.
 */

#ifndef HOUSERAIL_TRACK__H_DEFINED
#define HOUSERAIL_TRACK__H_DEFINED
const char *houserail_track_initialize (int argc, const char **argv);

struct TrackRange {
    const char *segment;
    const char *line;
    int low;
    int high;
};

void houserail_track_testmode (int enabled);

typedef void DetectionListener (const struct TrackRange *area,
                                int occupied,
                                long long timestamp);

DetectionListener *houserail_track_subscribe (DetectionListener *listener);

void houserail_track_input (const char *name,
                            long long timestamp, const char *state);
void houserail_track_flush (void);

const char *houserail_track_reload (void);
int houserail_track_export (char *buffer, int size, const char *separator);
int houserail_track_status (char *buffer, int size);
int houserail_track_detectors (char *buffer, int size);

void houserail_track_background (time_t now);

// Navigate the track topology:

struct TrackLocation {
    const char *segment;
    const char *line;
    int post;
};

int houserail_track_vicinity (struct TrackLocation *point,
                              const char *id, int direction);

int houserail_track_restricted (void);
int houserail_track_civil (const struct TrackLocation *point,
                           int direction, const char **cause);

int houserail_track_covered (const struct TrackRange *area,
                             const struct TrackLocation *limit1, 
                             const struct TrackLocation *limit2,
                             int direction);

int houserail_track_walk (struct TrackRange *path, int size,
                          const struct TrackLocation *limit1,
                          const struct TrackLocation *limit2,
                          int direction, int max);

int houserail_track_distance (const struct TrackLocation *point1,
                              const struct TrackLocation *point2,
                              int direction, int max);

const char *houserail_track_segment (const struct TrackLocation *point,
                                     int direction);

const char *houserail_track_switch (const char *name, const char *state);
const char *houserail_track_signal (const char *name, const char *state);

int houserail_track_poll (void);
#endif

