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
 * testlib.h - A minimal unit test framework.
 */

static int Errors = 0;

static void assert (int passed, const char *label) {
    if (passed) {
        printf ("== %s: passed\n", label);
    } else {
        printf ("** %s: failed\n", label);
        Errors += 1;
    }
}

static int summary (const char *label) {
    if (Errors == 0) printf ("== %s: passed\n", label);
    else        printf ("** %s: %d failures\n", label, Errors);
    return Errors;
}

