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
 * on a plan (2D). All angles are expressed in 1/100 of a degree.
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
long long houserail_math_interpolate (long long v1, long long v2, int ratio) {

    return ((100 - ratio) * v1 + ratio * v2) / 100;
}

int houserail_math_rotate (int value, int increment) {

    value += increment;
    if (value > 18000) value -= 36000;
    else if (value <= -18000) value += 36000;
    return value;
}

void houserail_math_straight (const struct TrackVertex *origin,
                              struct TrackVertex *end,
                              int angle, int length) {

    // Fold the angle to the first quadrant (0 to 90 degrees)
    int sine = 1;
    int cosine = 1;
 
    if (angle < 0) angle += 36000;
    else if (angle >= 36000) angle -= 36000;
 
    if (angle >= 27000) { // Fold fourth quadrant to first
        angle = 36000 - angle;
        sine = -1;
    } else if (angle >= 18000) { // Fold third quadrant to first
        angle -= 18000;
        cosine = sine = -1;
    } else if (angle > 9000) { // Fold second quadrant to first
        angle = 18000 - angle;
        cosine = -1;
    }

    // This function uses type long long because intermediate results might
    // overflow as the software uses insanely large coordinates to keep
    // good precision while still using integer arithmetic.

    int degrees = angle / 100;
    long long sine1 = sine * trigonometry[degrees];
    long long cosine1 = cosine * trigonometry[90-degrees];

    int fraction = angle % 100;
    if (fraction != 0) {
        if (++degrees > 90) {
            degrees = 180 - degrees;
            cosine *= -1; // Reverse the sign.
        }
        long long sine2 = sine * trigonometry[degrees];
        long long cosine2 = cosine * trigonometry[90-degrees];

        sine1 = houserail_math_interpolate (sine1, sine2, fraction);
        cosine1 = houserail_math_interpolate (cosine1, cosine2, fraction);
    }

    end->x = origin->x + ((length * cosine1) / 100000);
    end->y = origin->y + ((length * sine1) / 100000);
    end->angle = origin->angle;
}

void houserail_math_center (const struct TrackVertex *origin,
                            struct TrackVertex *center,
                            int angle, int radius, int arc) {

    if (arc > 0)
        houserail_math_straight (origin, center,
                                 houserail_math_rotate (angle, 9000), radius);
    else
        houserail_math_straight (origin, center,
                                 houserail_math_rotate (angle, -9000), radius);
}

void houserail_math_arc (const struct TrackVertex *origin,
                         struct TrackVertex *end,
                         int angle, int radius, int arc) {

    struct TrackVertex center;
    houserail_math_center (origin, &center, angle, radius, arc);
    if (arc > 0) {
        int rotate = houserail_math_rotate (angle,
                                            houserail_math_rotate (arc, -9000));
        houserail_math_straight (&center, end, rotate, radius);
    } else {
        int rotate = houserail_math_rotate (angle,
                                            houserail_math_rotate (arc, 9000));
        houserail_math_straight (&center, end, rotate, radius);
    }
    end->angle = houserail_math_rotate (angle, arc);
}

