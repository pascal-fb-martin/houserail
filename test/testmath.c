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
 * testmath.c - test houserail_math.c
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * testmath
 */
#include <stdio.h>

#include "echttp.h"

#include "../houserail_math.h"

#include "testlib.h"

int main (int argc, const char **argv) {

    // houserail_math_interpolate (int v1, int v2, int ratio);

    starting ("houserail_math_interpolate ()");
    int original = Errors;
    int result = houserail_math_interpolate (10, 20, 0);
    assert (result == 10, "houserail_math_interpolate (10, 20, 0) returns 10");
    result = houserail_math_interpolate (10, 20, 100);
    assert (result == 20, "houserail_math_interpolate (10, 20, 100) returns 20");
    result = houserail_math_interpolate (10, 20, 50);
    assert (result == 15, "houserail_math_interpolate (10, 20, 50) returns 15");
    result = houserail_math_interpolate (10, 20, 20);
    assert (result == 12, "houserail_math_interpolate (10, 20, 20) returns 12");
    result = houserail_math_interpolate (10, 20, 70);
    assert (result == 17, "houserail_math_interpolate (10, 20, 70) returns 17");
    digest (Errors == original, "houserail_math_interpolate ()");

    // houserail_math_rotate (int value, int increment);
    // WARNING: all angles are expressed in 1/100 of a degree!

    starting ("houserail_math_rotate ()");
    original = Errors;
    result = houserail_math_rotate (4500, 18000);
    assert (result == -13500, "houserail_math_rotate (45, 180) returns -135");
    result = houserail_math_rotate (4500, -18000);
    assert (result == -13500, "houserail_math_rotate (45, -180) returns -135");
    result = houserail_math_rotate (-4500, 18000);
    assert (result == 13500, "houserail_math_rotate (-45, 180) returns 135");
    result = houserail_math_rotate (-4500, -18000);
    assert (result == 13500, "houserail_math_rotate (-45, -180) returns 135");
    result = houserail_math_rotate (4500, 9000);
    assert (result == 13500, "houserail_math_rotate (45, 90) returns 135");
    result = houserail_math_rotate (4500, -9000);
    assert (result == -4500, "houserail_math_rotate (45, -90) returns -45");
    result = houserail_math_rotate (4500, 13500);
    assert (result == 18000, "houserail_math_rotate (45, 135) returns 180");
    result = houserail_math_rotate (4500, -13500);
    assert (result == -9000, "houserail_math_rotate (45, -135) returns -90");
    result = houserail_math_rotate (13500, 9000);
    assert (result == -13500, "houserail_math_rotate (135, 90) returns -135");
    result = houserail_math_rotate (-13500, -9000);
    assert (result == 13500, "houserail_math_rotate (-135, -90) returns 135");
    digest (Errors == original, "houserail_math_rotate ()");

    // houserail_math_straight (const struct TrackVertex *origin,
    //                          struct TrackVertex *end,
    //                          int angle, int length);
    // WARNING: all angles are expressed in 1/100 of a degree!

    struct TrackVertex origin = {10, 10, 3000};
    struct TrackVertex end;

    starting ("houserail_math_straight ()");
    original = Errors;
    houserail_math_straight (&origin, &end, 0, 10);
    assert ((end.x == 20) && (end.y == 10) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 30} at 0 for 10");

    houserail_math_straight (&origin, &end, 18000, 10);
    assert ((end.x == 0) && (end.y == 10) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 30} at 180 for 10");
    digest (Errors == original, "houserail_math_straight ()");

    houserail_math_straight (&origin, &end, -18000, 10);
    assert ((end.x == 0) && (end.y == 10) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 0} at -180 for 10");
    digest (Errors == original, "houserail_math_straight ()");

    houserail_math_straight (&origin, &end, 9000, 10);
    assert ((end.x == 10) && (end.y == 20) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 0} at 90 for 10");
    digest (Errors == original, "houserail_math_straight ()");

    houserail_math_straight (&origin, &end, -9000, 10);
    assert ((end.x == 10) && (end.y == 0) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 0} at -90 for 10");
    digest (Errors == original, "houserail_math_straight ()");

    houserail_math_straight (&origin, &end, 4500, 10);
    assert ((end.x == 17) && (end.y == 17) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 0} at 45 for 10");
    digest (Errors == original, "houserail_math_straight ()");

    houserail_math_straight (&origin, &end, -4500, 10);
    assert ((end.x == 17) && (end.y == 3) && (end.angle == 3000),
                     "houserail_math_straight ({10, 10, 0} at -45 for 10");
    digest (Errors == original, "houserail_math_straight ()");

    // houserail_math_arc (const struct TrackVertex *origin,
    //                     struct TrackVertex *end,
    //                     int angle, int radius, int arc);
    // WARNING: all angles are expressed in 1/100 of a degree!

    starting ("houserail_math_arc ()");
    original = Errors;
    houserail_math_arc (&origin, &end, 0, 10, 9000);
    assert ((end.x == 20) && (end.y == 20) && (end.angle == 9000),
                     "houserail_math_arc ({10, 10, 30} at 0 for 90 deg, 10");
printf ("result: x %d y %d angle %d\n", end.x, end.y, end.angle);

    houserail_math_arc (&origin, &end, 0, 10, -9000);
    assert ((end.x == 20) && (end.y == 0) && (end.angle == -9000),
                     "houserail_math_arc ({10, 10, 30} at 0 for -90 deg, 10");
printf ("result: x %d y %d angle %d\n", end.x, end.y, end.angle);

    houserail_math_arc (&origin, &end, 9000, 10, -9000);
    assert ((end.x == 20) && (end.y == 20) && (end.angle == 0),
                     "houserail_math_arc ({10, 10, 30} at 90 for -90 deg, 10");
printf ("result: x %d y %d angle %d\n", end.x, end.y, end.angle);

    houserail_math_arc (&origin, &end, 0, 10, 18000);
    assert ((end.x == 10) && (end.y == 30) && (end.angle == 18000),
                     "houserail_math_arc ({10, 10, 30} at 0 for 180 deg, 10");
printf ("result: x %d y %d angle %d\n", end.x, end.y, end.angle);

    houserail_math_arc (&origin, &end, 0, 10, -18000);
    assert ((end.x == 10) && (end.y == -10) && (end.angle == 18000),
                     "houserail_math_arc ({10, 10, 30} at 0 for -180 deg, 10");
printf ("result: x %d y %d angle %d\n", end.x, end.y, end.angle);

    houserail_math_arc (&origin, &end, 9000, 10, 18000);
    assert ((end.x == -10) && (end.y == 10) && (end.angle == -9000),
                     "houserail_math_arc ({10, 10, 30} at 90 for 180 deg, 10");
printf ("result: x %d y %d angle %d\n", end.x, end.y, end.angle);

    digest (Errors == original, "houserail_math_arc ()");

    return summary ("testmath");
}

