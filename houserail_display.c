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
 * houserail_display.c - Build the track topology display.
 *
 * SYNOPSYS:
 *
 * layoutdisplay [options..] <file>
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_libc.h>
#include <echttp_hash.h>

#include <houselog.h>
#include <houseconfig.h>

#include "houserail_topology.h"
#include "houserail_catalog.h"
#include "houserail_scout.h"
#include "houserail_track.h"  // Only to get data structures.

static int TestMode = 0;

#define DEBUG if (TestMode) printf

struct TrackSegmentDisplay {

    struct TrackDisplayShape shape;

    int done;
    struct TrackDisplayLocation origin; // Can be explicit in the layout, too
    struct TrackDisplayLocation end;
    struct TrackDisplayLocation reverse; // If a switch.
};

struct TrackDetectorDisplay {

    struct TrackDisplayLocation indicator;
};

static const struct TrackOptions *LayoutOptions = 0;

static const struct TrackModel *LayoutModels = 0;
static int                      LayoutModelsCount = 0;

static const struct TrackSegment  *LayoutSegments = 0;
static struct TrackSegmentDisplay *LayoutSegmentsDisplay = 0;
static int                         LayoutSegmentsCount = 0;

/* TBD: Show detectors on the display?
static const struct TrackDetector *LayoutDetectors = 0;
static int                         LayoutDetectorsCount = 0;
*/

const char *load_config (void) {

    if (LayoutSegmentsDisplay) {
       free (LayoutSegmentsDisplay);
       LayoutSegmentsDisplay = 0;
    }

    LayoutOptions = houserail_topology_options();

    LayoutModelsCount = houserail_topology_model_count ();
    LayoutModels = houserail_topology_models ();

    LayoutSegmentsCount = houserail_topology_segment_count ();
    LayoutSegments = houserail_topology_segments ();

/* TBD: Show detectors on the display?
    LayoutDetectorsCount = houserail_topology_detector_count ();
    LayoutDetectors = houserail_topology_detectors ();
*/

    LayoutSegmentsDisplay =
        calloc (LayoutSegmentsCount, sizeof (struct TrackSegmentDisplay));

    // Avoid frequent indirections.
    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        LayoutSegmentsDisplay[i].origin = LayoutSegments[i].display;
        if (LayoutSegmentsDisplay[i].origin.angle == 360) {
            LayoutSegmentsDisplay[i].origin.angle = 0;
        }
    }

    return 0;
}

static int calculate_straight_length (const struct TrackSegment *segment) {

    // Explicit length.
    if (segment->shape.straight > 0) return segment->shape.straight;

    // Legacy from an early version.
    if ((segment->shape.arc == 0) && (segment->shape.radius > 0))
            return segment->shape.radius;

    // Infer from the "post" length if there is nothing better.
    return LayoutModels[segment->model].length * LayoutOptions->postDistance;
}

static int rotate (int value, int increment) {
    value += increment;
    if (value > 180) value -= 360;
    else if (value <= -180) value += 360;
    return value;
}

static void move_straight (const struct TrackDisplayLocation *origin,
                           struct TrackDisplayLocation *end, int angle, int length) {

    // Angle:                    0   15   30   45   60   75    90
    static const int base[] =   {0, 259, 500, 707, 866, 966, 1000};

    int sine = 1;
    int cosine = 1;

    if (angle < 0) angle += 360;
    else if (angle >= 360) angle -= 360;

    // Fold the angle to the first quadrant (o to 90 degrees)
    if (angle >= 270) { // Fourth quadrant
        angle = 360 - angle;
        sine = -1;
    } else if (angle >= 180) {
        angle -= 180;
        cosine = sine = -1;
    } else if (angle > 90) {
        angle = 180 - angle;
        cosine *= -1;
    }
    int index = angle / 15;
    sine *= base[index];
    cosine *= base[6-index];

    end->x = origin->x + ((length * cosine) / 1000);
    end->y = origin->y + ((length * sine) / 1000);
    end->angle = origin->angle;
}

static void move_center (const struct TrackDisplayLocation *origin,
                         struct TrackDisplayLocation *center,
                         int angle, int radius, int arc) {

    if (arc > 0)
        move_straight (origin, center, rotate (angle, 90), radius);
    else
        move_straight (origin, center, rotate (angle, -90), radius);
}

static void move_circle (const struct TrackDisplayLocation *origin,
                         struct TrackDisplayLocation *end,
                         int angle, int radius, int arc) {

    struct TrackDisplayLocation center;
    move_center (origin, &center, angle, radius, arc);
    if (arc > 0)
        move_straight (&center, end, rotate (angle, rotate (arc, -90)), radius);
    else
        move_straight (&center, end, rotate (angle, rotate (arc, 90)), radius);
    end->angle = rotate (angle, arc);
printf ("---      move_circle from (%d, %d) angle %d arc %d, center (%d, %d), to (%d, %d, %d)\n", origin->x, origin->y, angle, arc, center.x, center.y, end->x, end->y, end->angle);
}

static void move_to_branch (const struct TrackSegment *segment,
                            struct TrackDisplayLocation *origin) {

    struct TrackDisplayLocation reverse =
         LayoutSegmentsDisplay[segment->index].reverse;

    const struct TrackSegment *upcoming = LayoutSegments + segment->branch;

printf ("---   Move to branch: start at reverse (%d, %d, %d)\n", reverse.x, reverse.y, reverse.angle);
    if (upcoming->next == segment->index) {
        // The reverse point is the end point of the upcoming segment:
        // retrieve its true origin.
printf ("---   Reverse is connected to the end of %s, move to the origin.\n", upcoming->id);
        if ((upcoming->shape.arc == 0) || (upcoming->branch >= 0)) {
printf ("---   (straight move)\n");
            int length = calculate_straight_length (upcoming);
            move_straight (&reverse, origin, reverse.angle, length);
            origin->angle = rotate (reverse.angle, 180);
        } else {
printf ("---   (circle move on %s: radius %d, arc %d)\n", upcoming->id, upcoming->shape.radius, 0-upcoming->shape.arc);
            move_circle (&reverse, origin, reverse.angle,
                         upcoming->shape.radius, 0-upcoming->shape.arc);
        }
        origin->angle = rotate (origin->angle, 180); // Turn around

    } else if (upcoming->branch == segment->index) {
        // Two switches are connected branch to branch.
printf ("---   Two switches connected branch to branch.\n");
printf ("---   (circle move)\n");
        move_circle (&reverse, origin, reverse.angle,
                     upcoming->shape.radius, 0-upcoming->shape.arc);
    } else {
        *origin = reverse;
    }
printf ("---   Move to branch: found origin of %s at (%d, %d, %d)\n", upcoming->id, origin->x, origin->y, origin->angle);
}

static void calculate_endpoints (int start,
                                 const struct TrackDisplayLocation *origin) {

    int i = start;
    const struct TrackSegment *segment = LayoutSegments + i;
    struct TrackSegmentDisplay *display = LayoutSegmentsDisplay + i;
    if (display->done) return;
    int previous = segment->previous;
    struct TrackDisplayLocation cursor = *origin;
    for (;;) {

        if (display->done) break;

        display->origin = cursor;
        display->reverse.x = display->reverse.y = display->reverse.angle = 0;
        int length = calculate_straight_length (segment);

        if (segment->shape.arc == 0) {
            move_straight (&cursor, &(display->end), cursor.angle, length);
        } else {
            int curveangle = cursor.angle;
            const struct TrackDisplayLocation *curveorigin = &cursor;
            struct TrackDisplayLocation *curveend = &(display->end);
            if (segment->branch >= 0) { // This is a switch, straight main line
                move_straight (&cursor, &(display->end), cursor.angle, length);
                if (segment->common == segment->next) {
                    // Move backward to the reverse point.
                    curveorigin = &(display->end);
                    curveangle = rotate (curveangle, 180);
                }
                curveend = &(display->reverse);
            }
            move_circle (curveorigin, curveend, curveangle,
                         segment->shape.radius, segment->shape.arc);
        }
        cursor = display->end;
        display->done = 1;
printf ("--- Done with segment %s (%d, %d, %d) to (%d, %d, %d), going toward %s\n", segment->id, display->origin.x, display->origin.y, display->origin.angle, display->end.x, display->end.y, display->end.angle, (segment->next >= 0)?LayoutSegments[segment->next].id:"(null)");
if (segment->branch >= 0) printf ("---    Segment %s is a switch, reverse point at (%d, %d, %d)\n", segment->id, display->reverse.x, display->reverse.y, display->reverse.angle);

        if (segment->branch >= 0) {
printf ("--- Taking a detour to %s\n", LayoutSegments[segment->branch].id);
            struct TrackDisplayLocation subwalk;
            move_to_branch (segment, &subwalk);
            calculate_endpoints (segment->branch, &subwalk);
printf ("--- end of detour\n");
        }

        if (segment->next < 0) break;
        const struct TrackSegment *upcoming = LayoutSegments + segment->next;
        if (upcoming->previous != i) {
printf ("--- Upcoming segment %s points back to %s, not %s\n", upcoming->id, LayoutSegments[upcoming->previous].id, segment->id);
            if (upcoming->branch == i) {
                // This entered a switch through a branch. Calculate
                // the location of the common endpoint and restart
                // a walk originating from there.
printf ("--- Upcoming segment %s is a switch: start a subwalk from there at (%d, %d, %d).\n", upcoming->id, display->end.x, display->end.y, display->end.angle);
                struct TrackDisplayLocation common;
                move_circle (&(display->end), &common, cursor.angle,
                             upcoming->shape.radius, 0-upcoming->shape.arc);
printf ("--- ended on segment %s at (%d, %d, %d).\n", upcoming->id, common.x, common.y, common.angle);
                if (upcoming->common == upcoming->next) {
                    // Need to move back to that switch's origin.
                    struct TrackDisplayLocation normal;
                    int l = calculate_straight_length (upcoming);
                    move_straight (&common, &normal, rotate (common.angle, 180), l);
printf ("--- moved to segment %s origin at (%d, %d, %d).\n", upcoming->id, normal.x, normal.y, normal.angle);
                    calculate_endpoints (upcoming->index, &normal);
                } else {
                    calculate_endpoints (upcoming->index, &common);
                }
printf ("--- end of subwalk\n");
            }
            break; // We are done with this branch.
        }

        i = segment->next;
        if (i == start) break;
        segment = LayoutSegments + i;
        display = LayoutSegmentsDisplay + i;
    }

    // Walkback from the original point. This is odd because walking back
    // means a 180 degree turn, so the angles (and their directions) change.

    if (previous < 0) return;
    i = previous;
    segment = LayoutSegments + i;
    display = LayoutSegmentsDisplay + i;
    cursor = *origin;

    for (;;) {

        if (display->done) break;

        display->end = cursor;
        int length = calculate_straight_length (segment);

        if (segment->shape.arc == 0) {
            move_straight (&cursor, &(display->origin), rotate (cursor.angle, 180), length);
        } else {
            int curveangle = cursor.angle;
            const struct TrackDisplayLocation *curveorigin = &(display->end);
            if (segment->branch >= 0) { // This is a switch.
                move_straight (&cursor, &(display->origin), rotate (cursor.angle, 180), length);
                if (segment->common == segment->previous) {
                    // For this curve, the move is forward.

                    curveorigin = &(display->origin);
                    curveangle = rotate (curveangle, -180);
                }
                move_circle (curveorigin, &(display->reverse),
                             rotate (curveangle, 180),
                             segment->shape.radius, segment->shape.arc);
            } else {
                move_circle (&(display->end), &(display->origin),
                             rotate (curveangle, 180),
                             segment->shape.radius, 0 - segment->shape.arc);
                display->origin.angle = rotate (display->origin.angle, 180);
            }
        }
        cursor = display->origin;
        display->done = 1;
printf ("--- Done backward with segment %s (%d, %d, %d) to (%d, %d, %d), going to %s\n", segment->id, display->origin.x, display->origin.y, display->origin.angle, display->end.x, display->end.y, display->end.angle, (segment->previous >= 0)?LayoutSegments[segment->previous].id:"(null)");
if (segment->branch >= 0) printf ("---    Segment %s is a switch, reverse point at (%d, %d, %d)\n", segment->id, display->reverse.x, display->reverse.y, display->reverse.angle);
        if (segment->branch >= 0) {
            struct TrackDisplayLocation subwalk;
            move_to_branch (segment, &subwalk);
            calculate_endpoints (segment->branch, &subwalk);
        }

        if (segment->previous < 0) break;
        const struct TrackSegment *upcoming = LayoutSegments + segment->previous;
        if (upcoming->next != i) {
            if (upcoming->branch == i) {
                // This entered a switch through a branch. Calculate
                // the location of the switch origin and restart
                // a walk from there.
                struct TrackDisplayLocation common;
                move_circle (&(display->origin), &common, rotate (cursor.angle, 180),
                             upcoming->shape.radius, 0-upcoming->shape.arc);
                if (upcoming->common == upcoming->next) {
                    // Need to move back to that switch's origin.
                    struct TrackDisplayLocation normal;
                    move_straight (&common, &normal, rotate (cursor.angle, 180), length);
                    calculate_endpoints (segment->previous, &normal);
                } else {
                    calculate_endpoints (segment->previous, &common);
                }
            }
            break;
        }

        i = segment->previous;
        if (i == start) break;
        segment = LayoutSegments + i;
        display = LayoutSegmentsDisplay + i;
    }
}

static void calculate_viewbox (struct TrackDisplayLocation *min,
                               struct TrackDisplayLocation *max) {

    min->x = min->y = 1000000000;
    max->x = max->y = 0;

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        const struct TrackSegment *segment = LayoutSegments+ i;
        struct TrackSegmentDisplay *display = LayoutSegmentsDisplay + i;
        if (!display->done) {
            fprintf (stderr, "Skipping segment %s. Not connected?\n", segment->id);
            continue;
        }
        if (min->x > display->origin.x) min->x = display->origin.x;
        if (min->y > display->origin.y) min->y = display->origin.y;
        if (max->x < display->origin.x) max->x = display->origin.x;
        if (max->y < display->origin.y) max->y = display->origin.y;

        if (min->x > display->end.x)    min->x = display->end.x;
        if (min->y > display->end.y)    min->y = display->end.y;
        if (max->x < display->end.x)    max->x = display->end.x;
        if (max->y < display->end.y)    max->y = display->end.y;

        if (segment->branch >= 0) { // This is a switch.
            if (min->x > display->reverse.x) min->x = display->reverse.x;
            if (min->y > display->reverse.y) min->y = display->reverse.y;
            if (max->x < display->reverse.x) max->x = display->reverse.x;
            if (max->y < display->reverse.y) max->y = display->reverse.y;
        }
    }
}

static void generate_html_head (void) {
    printf ("<html>\n"
            "<body style=\"margin: 0;\">\n"
            "<div style=\"background-color: #355b1eff;\" width=\"100%%\">\n");
}

static void generate_svg_head (const struct TrackDisplayLocation *min,
                               int width, int height) {

    printf ("<svg\n"
            "  width=\"100%%\"\n"
            "  height=\"100%%\"\n"
            "  viewBox=\"%d %d %d %d\"\n"
            "  version=\"1.1\"\n"
            "  id=\"svg1\"\n"
            "  xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
            "  xmlns=\"http://www.w3.org/2000/svg\"\n"
            "  xmlns:svg=\"http://www.w3.org/2000/svg\">\n",
           min->x, min->y, width, height);
}

static void draw_path (const char *id, const char *d, int width) {

    printf ("      <path id=\"%s\" d=\"%s\""
                       " fill=\"none\" stroke-width=\"%d\"/>\n",
           id, d, width);
}

static void draw_straight (const char *id,
                           const struct TrackDisplayLocation *origin,
                           const struct TrackDisplayLocation *end, int width) {

    char d[80];
    if (origin->y == end->y) {
        snprintf (d, sizeof(d), "M %d %d H %d", origin->x, origin->y, end->x);
    } else if (origin->x == end->x) {
        snprintf (d, sizeof(d), "M %d %d V %d", origin->x, origin->y, end->y);
    } else {
        snprintf (d, sizeof(d), "M %d %d L %d %d",
                  origin->x, origin->y, end->x, end->y);
    }
    draw_path (id, d, width);
}

static void draw_curve (const char *id,
                        const struct TrackDisplayLocation *origin,
                        const struct TrackDisplayLocation *end,
                        const struct TrackDisplayShape *shape, int width) {

   char d[80];
   snprintf (d, sizeof(d), "M %d %d A %d %d %d 0 %d %d %d",
             origin->x, origin->y,
             shape->radius, shape->radius, origin->angle, (shape->arc > 0)?1:0,
             end->x, end->y);
   draw_path (id, d, width);
}

static void generate_track (const struct TrackSegment *segment, int width) {

   struct TrackSegmentDisplay *display = LayoutSegmentsDisplay + segment->index;

   int gap = width / 7;
   struct TrackDisplayLocation origin;
   struct TrackDisplayLocation end;
   move_straight (&(display->origin), &origin, display->origin.angle, gap);

   if (segment->shape.arc == 0) {
       move_straight (&(display->end), &end, rotate (display->end.angle, 180), gap);
       draw_straight (segment->id, &origin, &end, width);
   } else {
       char id[32];
       if (segment->branch >= 0) {
           // This is a switch. There is always a straight portion.
           move_straight (&(display->end), &end, rotate (display->end.angle, 180), gap);
           draw_straight (segment->id, &origin, &end, width);

           snprintf (id, sizeof(id), "%s:reverse", segment->id);
           if (segment->common == segment->previous) {
               move_straight (&(display->reverse), &end, rotate (display->reverse.angle, 180), gap);
           } else {
               origin = end;
               move_straight (&(display->reverse), &end, rotate (display->reverse.angle, 180), gap);
           }
       } else {
           // This is a regular curve.
           snprintf (id, sizeof(id), "%s", segment->id);
           move_straight (&(display->end), &end, rotate (display->end.angle, 180), gap);
       }
       draw_curve (id, &origin, &end, &(segment->shape), width);
   }
}

static void generate_tracks (int width) {

    int i;
    printf ("    <g id=\"tracks\" stroke=\"#f0f0f0\">\n");
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        const struct TrackSegment *segment = LayoutSegments + i;
        if (!LayoutSegmentsDisplay[i].done) continue;
        generate_track (segment, width);
    }
    printf ("    </g>\n");
}

static void generate_svg_tail (void) {
    printf ("</svg>\n");
}

static void generate_html_tail (void) {
    printf ("</div>\n</body>\n>/html>\n");
}

static int find_preferred_origin (void) {

    int i;
    for (i = 0; i < LayoutSegmentsCount; ++i) {
        const struct TrackSegment *segment = LayoutSegments + i;
        struct TrackSegmentDisplay *display = LayoutSegmentsDisplay + i;
        if ((!display->done) && (segment->display.angle != 0)) return i;
    }
    return -1;
}

static void walk_the_layout (void) {

    int done = 0;
    for (;;) {
        int i = find_preferred_origin();
        if (i < 0) break;
        struct TrackDisplayLocation origin = LayoutSegmentsDisplay[i].origin;
        calculate_endpoints (i, &origin);
        done += 1;
    }

    if (!done) {
        // There is no explicit origin. Just use the first segment.
        struct TrackDisplayLocation origin = {0, 0, 0};
        calculate_endpoints (0, &origin);
    }

/*
    // Find segments not calculated yet that are linked to calculated segments.
    for (;;) {
        int i = find_uncalculated ();
        if (i < 0) break;
        // TBD
    }
*/
}

static const char *generate_display (void) {

    const char *error = houserail_topology_reload ();
    if (error) return error;
    error = load_config ();
    if (error) return error;

    // Find where each segment fits and what is the viewbox
    walk_the_layout ();
    struct TrackDisplayLocation min;
    struct TrackDisplayLocation max;
    calculate_viewbox (&min, &max);

    int margin = (max.x - min.x) / 30;
    min.x -= margin;
    min.y -= margin;
    max.x += margin;
    max.y += margin;
    int width = max.x - min.x;
    int height = max.y - min.y;

    int strokewidth = margin / 3;

    generate_html_head ();
    generate_svg_head (&min, width, height);
    generate_tracks (strokewidth);
    generate_svg_tail ();
    generate_html_tail ();

    return 0;
}

int main (int argc, const char **argv) {

    const char *arg = argv[argc-1];
    argc -= 1;

    char option[120];
    const char *prefix = "./";
    if ((arg[0] == '/') || (arg[0] == '.')) prefix = "";
    snprintf (option, sizeof(option), "--config=%s%s", prefix, arg);
    houseconfig_default (option);
    houserail_catalog_default ("--catalog=.");

    const char *error =
        houserail_catalog_initialize (argc, argv);
    if (error) goto fatal;
    error = houserail_topology_initialize (argc, argv);
    if (error) goto fatal;
    error = houseconfig_initialize
                ("layoutdisplay", generate_display, argc, argv);
    if (error) goto fatal;

    return 0;

fatal:
    printf ("Configuration error: %s\n", error);
    return 1;
}

