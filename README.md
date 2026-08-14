# HouseRail

A web service to control train traffic on a model railroad layout.

## Overview

This service provides a web user interface to control train traffic:

- Direct train to move to a specific location.
- Operate switches according to the train's destination.

This service typically control trains and switches using the DCC protocol, but the DCC protocol itself is implemented as a separate service: [HouseDCC](https://github.com/pascal-fb-martin/housedcc).

> This is a work in progress that is one piece of a larger project.

A typical train traffic control system runs the following services:

- This HouseRail service issue train and signaling commands (move, lights) according to the location of trains on the layout or user actions. These command are submitted using a generic train web API, with no explicit DCC dependencies.
- HouseDCC accepts these generic web commands, translates them into specific DCC messages and submits these messages to PiDCC (through a local pipe).
- PiDCC generates the PWM wave form conform to the DCC standard and uses it to control the GPIO pins attached to a power booster.
- One or more HouseRelays services to access train detectors on the tracks.

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install, or
* make debian-package and install the generated package.

The [HouseDCC](https://github.com/pascal-fb-martin/housedcc) and [PiDCC](https://github.com/pascal-fb-martin/pidcc) applications must have been install on a Raspberry Pi computer accessible on the local network. (This HouseRail service may, but does not need to, run on a Raspberry Pi board.)

One or more [HouseRelays](https://github.com/pascal-fb-martin/houserelays) services must have been installed on Raspberry Pi boards.

## Web API

```
/rail/train/status[?known=NUMBER]
```

Return the last known state of trains. This is a nearly complete status, typically what you would want for a train list display, or for an external application to monitor train traffic.

This returns JSON data with the following format:

* host:          the name of the host replying.
* timestamp:     the time when the response was built.
* latest:        the current state of the server (see the `known` parameter).
* rail.layout:   the name of the layout managed by this server.
* rail.train:    the current status of trains.

The `rail.train` item is an array of objects, each one reprenting a single train:

* id:      This train's ID.
* head:    An array describing the location of the train's head.
* tail:    An array describing the location of the train's tail.
* proceed: An array containing the train's direction ("up" or "down") and speed.
* spots:   An array containing the ordered list of detectable spots locations.
* cars:    An array containing the ordered list of cars (an array of IDs).

If a train is parked (i.e not present on the layout), items `head`, `tail` and `proceed` are not present.

A location is an array of 3 items: line name, post and segment ID.

```
/rail/status[?known=NUMBER]
```

Return the last known state of the trains, tracks, switches and signals on the layout. This contains enough information to show the locations of trains, track occupancies, orientations of switches and color of signals.

This returns JSON data with the following format:

* host:          the name of the host replying.
* timestamp:     the time when the response was built.
* latest:        the current state of the server (see the `known` parameter).
* rail.layout:   the name of the layout managed by this server.
* rail.detector: the current status of each individual occupancy detector.
* rail.segment:  the current status of track segments.
* rail.switch:   the current status of switches.
* rail.signal:   the current status of signals.
* rail.train:    a limited status of trains. This is an array of objects.

Each `detector`, `segment`, `switch` and `signal` entry is an array of arrays, where each inner array has two elements: the ID of the device followed by its status. Switches can be `reverse`, `normal` or `invalid`, signals can be `red` or `green`, detectors and segments can be `on` or `off`.

A `segment` item is a summary of the status of the detectors on that segment.

The information provided for each train is enough to locate the train on a track display: `id`, `color`, `proceed` and `path`. The `proceed` item provides the direction of travel. The `path` item represents the tracks that are covered by the train. It is an array of portions of track lines, where each portion may span multiple segments. Each portion is an array: line, low and high.

```
/rail/train/consist?id=STRING&cars=STRING[+STRING..]
```

Declare a new train consist. This may replace an existing train consist. The cars listed must be known, and must not be part of a consist already.

```
/rail/train/delete?id=STRING
```

Remove an existing train consist.

```
/rail/enter?id=STRING&dir=STRING&at=STRING[&color=STRING
```

Position a train on the layout. The `dir` parameter is either `up` or `down` and represents the orientation of the train (see DCC consist concepts). When the train has only one locomotive, this is the orientation of that locomotive (i.e. the direction that the cabin faces). The `at` parameter denotes a detector or segment that the train was positioned in front of. The `color` parameter sets the color that will be used when showing the train on the track display. Using a different color for each train makes it easier to identify which train is where. The default color is `Red`.

```
/rail/park?id=STRING
```

Park a train off the layout.

```
/rail/move?id=STRING[&dir=STRING][&slow=1|0]
```

Order a train to move in a specified direction. The slow option forces the train to move at restricted speed. Otherwise the train will follow the track's civil speed at each location. The `dir` parameter is either `forward` or `backward`. The default value for `dir` is `forward`.

This returns an updated status of the trains.

```
/rail/stop[?id=STRING]
```

Immediately stop the identified train, or all known trains if the id parameter is missing.

This returns an updated status of the trains.

```
/rail/switch?id=STRING&cmd=normal|reverse
```

Set the specified switch to the specified state.

This returns an updated status of the tracks.

```
/rail/signal?id=STRING&cmd=clear|stop|off
```

Set the specified signal to the specified state.

This returns an updated status of the tracks.

```
/rail/config
```

Return the current configuration, including the track topology.

```
/rail/identify[?known=NUMBER]
```

Provide the few items needed to update a web page's title and detect configuration changes:

* host:          the name of the host replying.
* timestamp:     the time when the response was built.
* latest:        the current state of the server configuration (see the `known` parameter).
* rail.layout:   the name of the layout managed by this server.

> [!NOTE]
> The `latest` field matches the configuration, not the live status. This means that its value changes only when a new configuration is loaded.

```
/rail/track/display
```

Return the track display page's content. This an HTML envelop around a SVG rendering of the track layout, matching the current layout configuration. This is intended to populate the tracks display page of the HouseRail web UI.

```
/rail/track/segments[?known=NUMBER]
```

Return the list of track segments, sorted by line and high post. The JSON schema is:

* host:          the name of the host replying.
* timestamp:     the time when the response was built.
* latest:        the current state of the server configuration (see the `known` parameter).
* rail.layout:   the name of the layout managed by this server.
* rail.segment:  the track segments. This is an array of arrays. Each subarray contains the ID, line, low and high posts.

> [!NOTE]
> The `latest` field matches the configuration, not the live status. This means that its value changes only when a new configuration is loaded.

> [!NOTE]
> Switches appear as two segments: ID+'~normal' and ID+'~reverse'. This is because each side belongs to a different line.

## Configuration

The layout configuration is stored in the HouseDepot service. The HouseService group name is used to identify which layout to load. This means that each instance of HouseRail only handles a single layout.

The full configuration is actually split into two parts: static configuration and state. The static configuration contains items that reflect permanent user data, typically the track topology and the rolling stock available. The state contains items that may change more frequently, including changed initiated by the service itself, like the list of consists.

The schema of the static configuration is described is two documents:

* [Track Configuration](https://github.com/pascal-fb-martin/houserail/blob/main/tracktopology.md)
* [Train Data](https://github.com/pascal-fb-martin/houserail/blob/main/traindata.md)

The software comes with a few layouts and track catalogs:

* catalogs/AtlasCode80.json describes the characteristics of (many) Atlas Model Railroad Company's Code 80 N scale tracks.
* catalogs/KatoUnitrackN.json describes the characteristics of (many) Kato's N scale UniTrack.
* layouts/ProgramTrack.json contains just a handful of straight Unitrack segments. This is the original physical layout that HouseRail is tested against.
* layouts/SimpleLoopKato.json contains a loop with a pocket track. This was the original layout used to test display generation and switch operations.
* layouts/SimpleWithSpurs.json is a loop with three spurs, loosely inspired by the Atlas N-503 layout.

The `layoutvalidate` tool is provided to both validate that the configuration is valid and verify that the generated topology and fleet are correct:

```
   layoutvalidate <path to config file>
```

This prints a trace of the internal track topology that the file would generate.

A `-bill` option can be used to print out the list of track parts to use for the layout, model by model:

```
   layoutvalidate -bill <path to config file>
```

The track display HTML can be printed using the `-display` option. This is intended for testing and validation only: the HouseRail service will generate the track display automatically.

```
   layoutvalidate -display <path to config file>
```

If the additional `-trace` option is used, the tool will also prints traces that give details about how the tool walks the track topology to calculate the display's geometry:

```
   layoutvalidate -trace -display <path to config file>
```

> [!WARNING]
> These traces are voluminous and somewhat confusing: they are intended for dire circumstances only.

