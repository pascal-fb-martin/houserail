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
 * houserail_catalog.c - Load a model catalog.
 */
void houserail_catalog_default (const char *arg);
const char *houserail_catalog_initialize (int argc, const char **argv);

const char *houserail_catalog_load (const char *name);
void houserail_catalog_set_scale (int scale);
int  houserail_catalog_get_scale (void);
void houserail_catalog_clear (void);

int         houserail_catalog_present (int parent, const char *path);
const char *houserail_catalog_string  (int parent, const char *path);
int         houserail_catalog_integer (int parent, const char *path);
int         houserail_catalog_positive (int parent, const char *path);
int         houserail_catalog_integer_scaled (int parent, const char *path);
int         houserail_catalog_positive_scaled (int parent, const char *path);
int         houserail_catalog_boolean (int parent, const char *path);

int houserail_catalog_array (int parent, const char *path);
int houserail_catalog_array_length (int array);
int houserail_catalog_enumerate (int parent, int *index, int size);

int houserail_catalog_object (int parent, const char *path);

