# HouseRail Train Database

## Overview

The train database is designed to document the vehicle models and then each individual vehicle owned. (Eventually the database of models will be made separate from the database of owned vehicles.)

The following classes of objects are considered:

* Car models define the characteristics of each type of vehicle equipment. The name of each model is typically the vendor's product ID. A model describes the geometry of the part: its length and the list of detectable spots. The type of vehicle does not matter here: there are only two types of vehicles, trailing cars and locomotives. This service learns the list of locomotives and their DCC characteristics from [HouseDCC]{https://github.com/pascal-fb-martin/housedcc}. The HouseTrain database is only concerned with vehicle tracking: how to detect vehicles presence on the tracks.

* Vehicles defines the list of owned equipment, and the model of each one.

* Train consists define the list and order of vehicles that make a train.


The lengths and post values int this database represent the [TO BE DECIDED STILL] protype scale values, typically the full scale size, not the actual size of the parts. Which unit is considered (meter, inch, mile) is purely a convention, which should be consistent within the same layout. It is not critical that the prototype scale be consistent with the model railroad scale (N, HO, O, etc). It is probably reasonable to match the milepost signs visible on the layout, if these are physically consistent with each others (i.e. the distance between two adjacent mileposts matches the marking on these two posts).

> [!NOTE]
> It is important that the prototype scale used in this track topology matches the speed scale used when controlling the trains. See HouseDCC for more information.

## Car Models

* `id`: vendor's product ID of the part.
* `length`: length of the car.
* `spots`: an array of relative positions that denote where the car's detectable center points are located. See The _Vehicle Detection Logic_ section for a complete description.

## Vehicles

* `id`: user's ID of the vehicle.
* `model`: the vendor's ID of the model that this vehicle belongs to.

## Train Consists

* `id`: user's ID of the train. If there is only one locomotive in this consist, this can be the same ID as the locomotive.
* `parked`: when true this optional field indicates that this train consist is not currently operated on the layout. Default is false.
* `cars`: an array of vehicle IDs that lists all the vehicles in the consist, ordered from head to tail.

## Vehicle Detection logic

The basis for vehicle detection is that there are sensor devices (detectors) that reports the presence  of a vehicle within a section of the tracks. How large is this section depends on the technology used. Such a section may cover an entire track segment, or just a few centimers of a segment.

These sensors are sensitive to detectable points on each car. For example a track circuit detects the presence of wheel axles that short current between the two rails, while relay reeds detect the presence of small magnets attached to the underbelly of each car.

There are two events, which follow different logics:

* occupancy: the detector transitioned from not detecting to detecting a vehicle. This means that there was no detection spot previously within the detector range. The goal is then to find the most probable spot that triggered this detection, i.e. the closer spot moving toward that detection zone.

* vaccancy: the detector transitioned from detecting to not detecting a vehicle. This means that there was at least one or more detection spots within the detector range, and all of them moved away. The goal is to find the last detection spot to leave the detector range.

The first step of the detection logic is to find if a train covers the detector's range. If the detector range is wide, multiple train could be present within that range: it is assumed here that only one train is present within range of the detector. It does not matter yet if the event is an occupancy or a vaccancy: detectable spots may move in and out of the detection zone while the train is moving over the detector.

If a train covering the detector's zone has been identified, the next step is to find which detectable spot has entered or exited the detection zone. If this is an occupancy, all that is needed is to identify the closest spot that is moving toward the detection zone. If this is a vaccancy, one need to move the train until no detectable spot remain within the detection zone. One way is to iterate: move the train to "vaccate" those spot within range, repeat until no spot is within range.
 
> [!NOTE]
> The size of the detection range matters in the case of a vaccancy. Reed relays have a very small range, and each spot will trigger independent occupancy and vaccancy events. A detection method with a wide range (e.g. track circuit) may have multiple spots within range at any point in time. In the worst case the spots last detected within the range may have vaccated without causing a vaccancy events because other spots have replaced them within range. this is this worst case that the iterative method is meant to cover.

If no train covers the detection zone, the event must be an occupancy: if that was a vacancy, what train would have vacated the zone? In that case, search for the nearest train that is moving forward to the detection zone. The first spot of that train is the one that entered the detection zone.

> [!WARNING]
> This design assumes that a detection zone can cover at most one full segment and never cross segment boundaries.

