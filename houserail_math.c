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
 * houserail_math.c - Perform basic geometry calculations.
 *
 * SYNOPSYS:
 *
 * This module implements geometry operations, straight lines and circle,
 * on a plan (2D).
 *
 * int houserail_math_interpolate (int v1, int v2, int ratio);
 *
 *    Interpolate a value at ratio % between v1 and v2: 0% is v1, 100% is v2.
 *
 * int houserail_math_rotate (int value, int increment);
 *
 *    Rotate and angle (and or substract), keeping the result within
 *    a -179 to 180 range.
 *
 * void houserail_math_straight (const struct TrackVertex *origin,
 *                               struct TrackVertex *end,
 *                               int angle, int length);
 *
 * void houserail_math_arc (const struct TrackVertex *origin,
 *                          struct TrackVertex *end,
 *                          int angle, int radius, int arc);
 */

#include <unistd.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <echttp.h>
#include <echttp_libc.h>

#include "houserail_math.h"

static const int trigonometry[ 91 ] = {
        0,  1745,  3490,  5234,  6976,  8716,  0453,  2187,  3917, 15643,
    17365, 19081, 20791, 22495, 24192, 25882, 27564, 29237, 30902, 32557,
    34202, 35837, 37461, 39073, 40674, 42262, 43837, 45399, 46947, 48481,
    50000, 51504, 52992, 54464, 55919, 57358, 58779, 60182, 61566, 62932,
    64279, 65606, 66913, 68200, 69466, 70711, 71934, 73135, 74315, 75471,
    76604, 77715, 78801, 79864, 80902, 81915, 82904, 83867, 84805, 85717,
    86603, 87462, 88295, 89101, 89879, 90631, 91355, 92051, 92718, 93358,
    93969, 94552, 95106, 95631, 96126, 96593, 97030, 97437, 97815, 98163,
    98481, 98769, 99027, 99255, 99452, 99620, 99756, 99863, 99939, 99985,
   100000
};

// FUTURE: when the angle is in 1/100 of a degree.
int houserail_math_interpolate (int v1, int v2, int ratio) {

    return ((100 - ratio) * v1 + ratio * v2) / 100;
}

int houserail_math_rotate (int value, int increment) {

    value += increment;
    if (value > 180) value -= 360;
    else if (value <= -180) value += 360;
    return value;
}

void houserail_math_straight (const struct TrackVertex *origin,
                              struct TrackVertex *end,
                              int angle, int length) {

    // This function uses type long long because intermediate results might
    // overflow as that this module uses insanely large coordinates to keep
    // good precision while still using integer arithmetic.
 
    long long sine = 1;
    long long cosine = 1;
 
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
    sine *= trigonometry[angle];
    cosine *= trigonometry[90-angle];

    end->x = origin->x + ((length * cosine) / 100000);
    end->y = origin->y + ((length * sine) / 100000);
    end->angle = origin->angle;
}

void houserail_math_center (const struct TrackVertex *origin,
                            struct TrackVertex *center,
                            int angle, int radius, int arc) {

    if (arc > 0)
        houserail_math_straight (origin, center,
                                 houserail_math_rotate (angle, 90), radius);
    else
        houserail_math_straight (origin, center,
                                 houserail_math_rotate (angle, -90), radius);
}

void houserail_math_arc (const struct TrackVertex *origin,
                         struct TrackVertex *end,
                         int angle, int radius, int arc) {

    struct TrackVertex center;
    houserail_math_center (origin, &center, angle, radius, arc);
    if (arc > 0) {
        int rotate = houserail_math_rotate (angle,
                                            houserail_math_rotate (arc, -90));
        houserail_math_straight (&center, end, rotate, radius);
    } else {
        int rotate = houserail_math_rotate (angle,
                                            houserail_math_rotate (arc, 90));
        houserail_math_straight (&center, end, rotate, radius);
    }
    end->angle = houserail_math_rotate (angle, arc);
}

