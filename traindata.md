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

## Car Model

## Vehicle

## Train Consist

## Vehicle Detection logic

TBD

> [!WARNING]
> In this design a detector can cover at most one full segment.

