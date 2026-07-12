# HouseTrain - a model train control service
#
# Copyright 2025, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=houserail
HCAT=Train

# Application build. --------------------------------------------

all: testscout testpath testtrack testtrain testend

clean:
	rm -f *.o *.a testscout

rebuild: clean all

%.o: %.c
	gcc -c -Wall -g -O2 -o $@ $<

testscout: testscout.o ../houserail_scout.o
	gcc -g -O -o testscout testscout.o ../houserail_scout.o -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

testpath: testpath.o ../houserail_path.o
	gcc -g -O -o testpath testpath.o ../houserail_path.o -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

testtrack: testtrack.o ../houserail_track.o ../houserail_scout.o
	gcc -g -O -o testtrack testtrack.o ../houserail_track.o ../houserail_scout.o -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

testtrain: testtrain.o ../houserail_train.o ../houserail_path.o ../houserail_track.o ../houserail_scout.o
	gcc -g -O -o testtrain testtrain.o ../houserail_train.o ../houserail_path.o ../houserail_track.o ../houserail_scout.o -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

testend: testend.o ../houserail_train.o ../houserail_path.o ../houserail_track.o ../houserail_scout.o
	gcc -g -O -o testend testend.o ../houserail_train.o ../houserail_path.o ../houserail_track.o ../houserail_scout.o -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

