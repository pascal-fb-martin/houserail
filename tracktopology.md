# HouseRail Track Topology Database

## Overview

The track topology is designed to closely match the parts used to build a layout. The geometry of each part (straight, curve, flex) is not relevant here.

The following classes of objects are considered:

* Track segment models define the characteristics of each type of track segment. The name of each model is typically the vendor's product ID. A model describes the geometry of the part. There are three types of segments: standard (no switch), switch (a single switch controlled independently) and interlocking (typically 4 switches controlled as one entity);

* Track segments define the actual parts that make the layout. Each track segment refers to the model it belongs to and its relationship to adjacent segments.

* Track detectors define the devices that detect the presence of cars. The technology employed (relay reeds, infrared detectors, current sensors, etc) does not really matter here, what counts at this level is the span of track where cars can be detected. Track detectors may not covers the whole length of the track: a coverage hole is handled as a dark territory.

> [!NOTE]
> The interlocking track type is reserved for future use.

All lengths and post values int this database represent the protype scale values, typically the full scale size, not the actual size of the parts. Which unit is considered (meter, inch, mile) is purely a convention, which should be consistent within the same layout. It is not critical that the prototype scale be consistent with the model railroad scale (N, HO, O, etc). It is probably reasonable to match the milepost signs visible on the layout, if these are physically consistent with each others (i.e. the distance between two adjacent mileposts matches the marking on these two posts).

> [!NOTE]
> It is important that the prototype scale used in this track topology matches the speed scale used when controlling the trains. See HouseDCC for more information.

A unique location on the layout is determined by the combination of a line name and post value, e.g. "main 170".

## Track Models

The models are stored in the `track.models` array. Each element is an object the follows the schema below:

* `id`: vendor's product ID of the part.
* `length`: the length of the part (standard track) or the length of the normal branch (switch track).
* `reverse`: the length of the reverse branch of a switch. Not present for standard tracks.

> [!NOTE]
> Models will eventually be stored separately from the layout, to make it possible to build a database of models shared by multiple layouts.

## Track Segments

The segments are specific to a layout and are stored in the `track.segments` array. Each element is an object the follows the schema below:

* `id`: an identifier for this segment, unique within the layout.
* `line`: a line identifier. All standard tracks connected to each other belong to the same line. In the case of a switch, this refers to the normal branch.
* `previous`: the ID of the previous segment attached to the entry point.
* `next`: the ID of the subsequent segment attached to the exit point.
* `reverse`: the ID of the subsequent segment attached to the reverse point (switch only).

If a point of the track segment is a line terminal point, the corresponding linkage to the adjacent segment is missing.

The name of the line on the reverse point of a switch is determined from the name of the normal branch of the adjacent segment. If multiple switches are connected to each other, that name is determined by transitively following the _normal_ linkages until a standard segment has been found. If two switches are connected through their reverse points, that portion of track has no name. This case should be considered an interlocking anyway as one cannot operate the two switches independently without risking a derail.

A separate field is used to mark the post origin segment. Post value 0 is applied to the endpoint of that segment that is not linked to any other segment.

* `track.origin`: ID of the track segment where post value zero is located.

## Track Detectors

Detectors are specific to a layout and are stored in the `track.detectors` array. Each element is an object the follows the schema below:

* `id`: an identifier for this detector, unique within the layout
* `line`: name of the track line where this detector resides.
* `low`: the low post limit for vehicle detection.
* `high`: the high post limit for vehicle detection.

> [!WARNING]
> In this design a detector can cover at most one full segment.

