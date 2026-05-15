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
 * houserail.c - Main loop of the HouseRail program.
 *
 * SYNOPSYS:
 *
 *   houserail [-group=NAME]
 *
 * The group name is used to identify the model train layout.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_static.h"

#include "houseportalclient.h"
#include "houselog.h"
#include "houseconfig.h"
#include "housestate.h"
#include "housediscover.h"
#include "housedepositor.h"
#include "housedepositorstate.h"

#include "housecontrol.h"

#include "houserail_fleet.h"
#include "houserail_track.h"
#include "houserail_train.h"

#define DEBUG if (echttp_isdebug()) printf

static char JsonBuffer[65537];

static int LiveState = -1;
static int ConfigState = -1;

static int rail_header (char *buffer, int size, int stateid) {

    int cursor;
    cursor = snprintf (buffer, size,
                       "{\"host\":\"%s\",\"timestamp\":%lld"
                           ",\"rail\":{\"layout\":\"%s\",\"latest\":%lu",
                       houselog_host(),
                       (long long)time(0),
                       housedepositor_group(),
                       housestate_current (stateid));
    return cursor;
}

static int rail_export (void) {

    int c = rail_header (JsonBuffer, sizeof(JsonBuffer), ConfigState);
    int empty = c;

    c += houserail_track_export (JsonBuffer+c, sizeof(JsonBuffer)-c, ",");
    c += houserail_train_export (JsonBuffer+c, sizeof(JsonBuffer)-c, ",");
    if (c == empty) return 0;
    return c;
}

/* No configuration for the user to change at this time.
static const char *rail_save (const char *reason) {

    housestate_changed (ConfigState);

    rail_export ();
    houseconfig_save (JsonBuffer, reason);

    echttp_content_type_json ();
    return JsonBuffer;
}
*/

static const char *rail_status (const char *method, const char *uri,
                               const char *data, int length) {

    if (housestate_same (LiveState)) return "";

    int cursor = rail_header (JsonBuffer, sizeof(JsonBuffer), LiveState);
    int empty = cursor;

    cursor += houserail_train_status (JsonBuffer+cursor, sizeof(JsonBuffer)-cursor);

    if (cursor == empty) return "";

    echttp_content_type_json ();
    return JsonBuffer;
}

static const char *rail_move (const char *method, const char *uri,
                             const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *to = echttp_parameter_get("to");
    const char *slow = echttp_parameter_get("slow");

    if (!id) {
        echttp_error (404, "missing device ID");
        return "";
    }
    if (!to) {
        echttp_error (400, "missing destination ID");
        return "";
    }
    const char *error = houserail_train_move (id, to, slow?atoi(slow):0);
    if (error) {
        echttp_error (500, error);
        return "";
    }
    housestate_changed (LiveState);
    return rail_status (method, uri, data, length);
}

static const char *rail_stop (const char *method, const char *uri,
                              const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *urgent = echttp_parameter_get("urgent");

    int emergency = urgent?atoi(urgent):0;

    const char *error = houserail_train_stop (id, emergency);
    if (error) {
        echttp_error (500, error);
        return "";
    }
    housestate_changed (LiveState);
    return rail_status (method, uri, data, length);
}

static const char *rail_config (const char *method, const char *uri,
                                const char *data, int length) {

    if (housestate_same (ConfigState)) return "";

    rail_export ();
    echttp_content_type_json ();
    return JsonBuffer;
}

static void rail_background (int fd, int mode) {

    time_t now = time(0);

    houseportal_background (now);
    housediscover (now);
    houselog_background (now);
    houseconfig_background (now);
    housedepositor_periodic (now);
    housedepositor_state_background (now);
    housecontrol_background (now);
    houserail_fleet_background (now);
    houserail_train_background (now);
}

static const char *rail_update (void) {

    housestate_changed (ConfigState);
    const char *error = 0;
    error = houserail_track_reload ();
    if (error) return error;
    error = houserail_train_reload ();
    if (error) return error;
    return error;
}

static void ontrackchange (const char *name,
                           long long timestamp, const char *old, const char *new) {
    DEBUG (__FILE__ ": track state update for %s, state %s\n", name, new);
    houserail_track_input (name, timestamp, new);
}

static void rail_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        static const char *path[] = {"rail:/rail"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }
    housediscover_initialize (argc, argv);
    houselog_initialize ("dcc", argc, argv);
    housedepositor_initialize (argc, argv);

    error = houseconfig_initialize ("rail", rail_update, argc, argv);
    if (error) goto fatal;

    error = houserail_fleet_initialize (housedepositor_group(), argc, argv);
    if (error) goto fatal;
    error = houserail_train_initialize (argc, argv);
    if (error) goto fatal;

    LiveState = housestate_declare ("live");
    ConfigState = housestate_declare ("config");
    housestate_cascade (ConfigState, LiveState);

    echttp_cors_allow_method("GET");
    echttp_protect (0, rail_protect);

    echttp_route_uri ("/rail/status", rail_status);
    echttp_route_uri ("/rail/move",   rail_move);
    echttp_route_uri ("/rail/stop",   rail_stop);
    echttp_route_uri ("/rail/config", rail_config);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&rail_background);
    housedepositor_state_load ("rail", argc, argv);
    housedepositor_state_share (1);
    housecontrol_subscribe ("track", ontrackchange);
    houserail_track_subscribe (houserail_train_track);

    houselog_event ("SERVICE", "rail", "STARTED", "ON %s", houselog_host());
    echttp_loop();
    exit(0);

fatal:
    houselog_trace (HOUSE_FAILURE, "RAIL", "Cannot initialize: %s\n", error);
    exit(1);
}

