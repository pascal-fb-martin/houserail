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
 * testscout.c - test houserail_scout.c
 *
 * SYNOPSYS:
 *
 * Command line:
 *
 * testscout
 */
#include <stdio.h>

#include "echttp.h"

#include "../houserail_scout.h"

#include "testlib.h"

int main (int argc, const char **argv) {

    struct RangeIndex testindex;
    houserail_scout_initialize (&testindex, 100);

    houserail_scout_add (&testindex, 1, "mmm", 0, 100);
    houserail_scout_add (&testindex, 3, "mmm", 150, 250);
    houserail_scout_add (&testindex, 4, "aaa", 0, 25);
    houserail_scout_add (&testindex, 2, "mmm", 100, 150);
    houserail_scout_add (&testindex, 7, "zzz", 25, 125);
    houserail_scout_add (&testindex, 5, "aaa", 25, 125);
    houserail_scout_add (&testindex, 6, "zzz", 0, 25);

    houserail_scout_finalize (&testindex);

    int result = houserail_scout_inside (&testindex, "mmm", 20);
    assert (result == 1, "Search segment for (mmm,20)");

    result = houserail_scout_inside (&testindex, "mmm", 120);
    assert (result == 2, "Search segment for (mmm,120)");

    result = houserail_scout_inside (&testindex, "mmm", 170);
    assert (result == 3, "Search segment for (mmm,170)");

    result = houserail_scout_inside (&testindex, "aaa", 20);
    assert (result == 4, "Search segment for (aaa,20)");

    result = houserail_scout_inside (&testindex, "aaa", 30);
    assert (result == 5, "Search segment for (aaa,30)");

    result = houserail_scout_inside (&testindex, "zzz", 20);
    assert (result == 6, "Search segment for (zzz,20)");

    result = houserail_scout_inside (&testindex, "zzz", 100);
    assert (result == 7, "Search segment for (zzz,100)");

    houserail_scout_erase (&testindex);

    return summary ("houserail_scout.c");
}

