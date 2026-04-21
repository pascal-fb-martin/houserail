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
 * houserail_fleet.c - The client stub to access the active vehicles.
 */
const char *houserail_fleet_initialize (const char *group,
                                        int argc, const char **argv);

typedef void FleetListener (const char *id, int index);

FleetListener *houserail_fleet_subscribe (FleetListener *listener);
int            houserail_fleet_iterate   (FleetListener *listener);

int         houserail_fleet_search (const char *id);
const char *houserail_fleet_model (int index);
int         houserail_fleet_speed (int index);

const char *houserail_fleet_move (const char *id, int speed);
const char *houserail_fleet_stop (const char *id, int emergency);

void houserail_fleet_background (time_t now);

