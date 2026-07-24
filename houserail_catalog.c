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
 *
 * SYNOPSYS:
 *
 * void houserail_catalog_default (const char *arg);
 *
 *    Set a hardcoded default for a command line option.
 *
 * const char *houserail_catalog_initialize (int argc, const char **argv);
 *
 *    Initialize the context. Supported options are:
 *
 *    -catalog=STRING        Set a user path for catalog files.
 *
 * const char *houserail_catalog_load (const char *name);
 *
 *    Loads one more catalog. Only the latest catalog can be accessed.
 *    It is possible to load multiple catalogs: previous catalogs are
 *    still in memory. This works well if the catalog data was fully
 *    analyzed before loading the next one.
 *
 *    No catalog is automatically unloaded. This is not a bug or a memory
 *    leak: each catalog may still be referenced.
 *
 *    This returns 0 on success, an error message otherwise.
 *
 * void houserail_catalog_set_scale (int scale);
 *
 *    Set the scale factor for this layout. If not used, this module will
 *    use the scale factor from the catalog itself. This must be called
 *    after the catalog has been loaded.
 *
 * int houserail_catalog_get_scale (void);
 *
 *    This returns the effective scale, either as specified above, or
 *    the catalog's default.
 *
 * void houserail_catalog_clear (void);
 *
 *    Clear all catalogs from memory. This is typically done before loading
 *    a new configuration.
 *
 * int         houserail_catalog_present (int parent, const char *path);
 * const char *houserail_catalog_string  (int parent, const char *path);
 * int         houserail_catalog_integer (int parent, const char *path);
 * int         houserail_catalog_positive (int parent, const char *path);
 * int         houserail_catalog_integer_scaled (int parent, const char *path);
 * int         houserail_catalog_positive_scaled (int parent, const char *path);
 * int         houserail_catalog_boolean (int parent, const char *path);
 *
 *    Access individual items starting from the specified parent
 *    (the config root is index 0). If the path is an empty string,
 *    the entry being accessed is the parent itself.
 *
 * int houserail_catalog_array (int parent, const char *path);
 * int houserail_catalog_array_length (int array);
 *
 *    Retrieve an array.
 * 
 * int houserail_catalog_enumerate (int parent, int *index, int size);
 *
 *    Retrieve all the elements of an array or object. The index array
 *    must be large enough.
 *
 *    This function returns the actual length of the array, or -1 on error.
 *
 * int houserail_catalog_object (int parent, const char *path);
 *
 *    Retrieve an object. This returns an index that can be used to retrieve
 *    the object's individual items.
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housedepositor.h"
#include "houseconfig.h"

#include "houserail_catalog.h"

// This is the latest loaded catalog
static ParserToken *CatalogParsed = 0;
static int   CatalogTokenAllocated = 0;
static int   CatalogTokenCount = 0;
static char *CatalogText = 0;
static int   CatalogScale = 0;

// Keep trace of all memory allocated for catalogs. This is used when
// clearing up catalogs.
static char *CatalogArchive[256];
static int   CatalogArchiveCount = 0;

#define BUILTINCATALOGPATH "/usr/local/share/house/rail/catalogs"
#define LOCALCATALOGPATH "/var/house/rail/catalogs"

static char *CatalogUserDirectory = 0;

static void houserail_catalog_archive (char *space) {

    if (CatalogArchiveCount >= 256) return; // Hoops..
    CatalogArchive[CatalogArchiveCount++] = space;
}

void houserail_catalog_clear (void) {

    if (CatalogText) {
        free (CatalogText);
        CatalogText = 0;
    }
    if (CatalogParsed) {
        free (CatalogParsed);
        CatalogParsed = 0;
    }
    CatalogTokenAllocated = CatalogTokenCount = 0;

    int i;
    for (i = 0; i < CatalogArchiveCount; ++i) free (CatalogArchive[i]);
    CatalogArchiveCount = 0;
}

static const char *houserail_catalog_parse (const char *name) {

    if (!CatalogText) {
        CatalogTokenCount = 0;
        return "no configuration";
    }
    int count = echttp_json_estimate(CatalogText);
    if (count > CatalogTokenAllocated) {
        if (CatalogParsed) free (CatalogParsed);
        CatalogTokenAllocated = count + 32;
        CatalogParsed = calloc (CatalogTokenAllocated, sizeof(ParserToken));
    }
    CatalogTokenCount = CatalogTokenAllocated;

    const char *error =
        echttp_json_parse (CatalogText, CatalogParsed, &CatalogTokenCount);
    if (error) {
        CatalogTokenCount = 0;
        houselog_event ("CATALOG", name, "ERROR", "%s", error);
        return error;
    }
    return 0;
}

const char *houserail_catalog_load (const char *name) {

    char path[512];
    char *text = 0;

    if (CatalogUserDirectory) {
        snprintf (path, sizeof(path), "%s/%s.json", CatalogUserDirectory, name);
        text = echttp_parser_load (path);
    }
    if (!text) {
        snprintf (path, sizeof(path), LOCALCATALOGPATH "/%s.json", name);
        text = echttp_parser_load (path);
        if (!text) {
           snprintf (path, sizeof(path), BUILTINCATALOGPATH "/%s.json", name);
           text = echttp_parser_load (path);
        }
    }
    if (!text) {
        houselog_event ("CATALOG", name, "ERROR", "FILE NOT FOUND");
        return "file not found";
    }

    // Do not reload the same (valid) configuration again and again.
    if ((CatalogTokenCount > 0) && (!strcmp (text, CatalogText))) {
        echttp_parser_free (text);
        return 0;
    }

    houselog_event ("CATALOG", name, "LOAD", "FROM %s", path);
    if (CatalogText) houserail_catalog_archive (CatalogText);
    CatalogText = text;
    CatalogScale = 0;
    const char *error = houserail_catalog_parse (name);
    if (!error) {
        CatalogScale = houserail_catalog_positive (0, ".scale");
        if (CatalogScale == 0) CatalogScale = 160; // N Scale.
    }
    return error;
}

void houserail_catalog_set_scale (int scale) {

    if (scale > 0) CatalogScale = scale;
}

int houserail_catalog_get_scale (void) {
    return CatalogScale;
}

void houserail_catalog_default (const char *arg) {

    const char *name = 0;

    if (echttp_option_match ("-catalog=", arg, &name)) {
        if (CatalogUserDirectory) free(CatalogUserDirectory);
        CatalogUserDirectory = strdup(name);
    }
}

const char *houserail_catalog_initialize (int argc, const char **argv) {

    int i;
    for (i = 1; i < argc; ++i) {
        houserail_catalog_default (argv[i]);
    }
    return 0;
}

int houserail_catalog_find (int parent, const char *path, int type) {
    int i;
    if (parent < 0 || parent >= CatalogTokenCount) return -1;
    i = echttp_json_search(CatalogParsed+parent, path);
    if (i >= 0 && CatalogParsed[parent+i].type == type) return parent+i;
    return -1;
}

int houserail_catalog_present (int parent, const char *path) {
    if (parent < 0 || parent >= CatalogTokenCount) return 0;
    if (echttp_json_search(CatalogParsed+parent, path) < 0) return 0;
    return 1;
}

const char *houserail_catalog_string (int parent, const char *path) {
    int i = houserail_catalog_find(parent, path, PARSER_STRING);
    return (i >= 0) ? CatalogParsed[i].value.string : 0;
}

int houserail_catalog_integer (int parent, const char *path) {
    int i = houserail_catalog_find(parent, path, PARSER_INTEGER);
    return (i >= 0) ? CatalogParsed[i].value.integer : 0;
}

int houserail_catalog_integer_scaled (int parent, const char *path) {
    int i = houserail_catalog_find(parent, path, PARSER_INTEGER);
    return (i >= 0) ? (CatalogScale * CatalogParsed[i].value.integer) : 0;
}

int houserail_catalog_positive (int parent, const char *path) {
    int i = houserail_catalog_find(parent, path, PARSER_INTEGER);
    if (i < 0) return 0;
    if (CatalogParsed[i].value.integer < 0) return 0;
    return CatalogParsed[i].value.integer;
}

int houserail_catalog_positive_scaled (int parent, const char *path) {
    int i = houserail_catalog_find(parent, path, PARSER_INTEGER);
    if (i < 0) return 0;
    if (CatalogParsed[i].value.integer < 0) return 0;
    return CatalogScale * CatalogParsed[i].value.integer;
}

int houserail_catalog_boolean (int parent, const char *path) {
    int i = houserail_catalog_find(parent, path, PARSER_BOOL);
    return (i >= 0) ? CatalogParsed[i].value.boolean : 0;
}

int houserail_catalog_array (int parent, const char *path) {
    return houserail_catalog_find(parent, path, PARSER_ARRAY);
}

int houserail_catalog_array_length (int array) {
    if (array < 0
            || array >= CatalogTokenCount
            || CatalogParsed[array].type != PARSER_ARRAY) return 0;
    return CatalogParsed[array].length;
}

int houserail_catalog_object (int parent, const char *path) {
    return houserail_catalog_find(parent, path, PARSER_OBJECT);
}

int houserail_catalog_enumerate (int parent, int *index, int size) {

    int i, length;

    if (parent < 0 || parent >= CatalogTokenCount) return 0;
    const char *error = echttp_json_enumerate (CatalogParsed+parent, index, size);
    if (error) {
        fprintf (stderr, "Cannot enumerate %s: %s\n",
                 CatalogParsed[parent].key, error);
        return -1;
    }
    length = CatalogParsed[parent].length;
    for (i = 0; i < length; ++i) index[i] += parent;
    return length;
}

