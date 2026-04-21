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
* Install [HouseRail](https://github.com/pascal-fb-martin/houserail).
* Clone this GitHub repository.
* make
* sudo make install, or
* make debian-package and install the generated package.

The [HouseDCC](https://github.com/pascal-fb-martin/housedcc) and [PiDCC](https://github.com/pascal-fb-martin/pidcc) applications must have been install on a Raspberry Pi computer accessible on the local network. (This HouseRail service does not need to run on a Raspberry Pi board.)

One or more [HouseRelays](https://github.com/pascal-fb-martin/houserelays) services must have been installed on Raspberry Pi boards.

## Configuration

All configuration is stored in the HouseDepot service. The HouseService group name is used to identify the layout. This means that each instance of HouseRail only handles a single layout.

The full configuration is actually split into two parts: static configuration and state. The static configuration contains items that reflect permanent user data, typically the track topology. The state contains items that may change more frequently, including changed initiated by the service itself or from another service, like the list of consists.

## Web API

```
/rail/move?id=STRING&to=STRING
```

Order a train to move to a specific tracklocation.

```
/rail/stop[?id=STRING]
```

Immediately stop the identified train, or all known trains if the id parameter is missing.

```
/rail/status[?known=NUMBER]
```

Return the last known state of trains and switches on the layout.

```
/rail/config
```

Return the current configuration, including the track topology.

## Configuration

TBD.

