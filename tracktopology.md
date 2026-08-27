# HouseRail Track Topology Database

## Overview

The track topology is designed to closely match the parts used to build a layout. The geometry of each part (straight, curve, switch) is included, used to build the track display.

The following classes of objects are considered:

* Track segment models define the characteristics of each type of track segment, including its geometry (shape). The name of each model is typically the vendor's product ID. Three types of segments are supported: straight and curved standard tracks, and switch (a single switch controlled independently).

* Track segments list the actual parts that make the layout. Each track segment refers to the model it belongs to and its relationship to adjacent segments.

* Track detectors define the devices that detect the presence of cars. The technology employed (relay reeds, infrared detectors, current sensors, etc) does not really matter here, what counts at this level is the span of track where cars can be detected. Track detectors may not covers the whole length of the track: a coverage hole is handled as a dark territory.

* Signals describe every individual signal on the layout. Note that signals are linked by default to the next segment, according to their direction (a signal protects an area _before_ the train enters the area). This next segment is typically a switch, protected by 3 signals, but the software supports isolated or paired signals along standard segments. If the geometry is more complex, like two switches that work together, it is possible to explicitely declare a link with an arbitrary track segment.

The software can controls switches and signals through control points (e.g. the [HouseRelays](https://github.com/pascal-fb-martin/houserelays) service) based on a naming convention:

* a switch X is controlled through two control points, named "X:normal" and "X:reverse".
* A signal X is controlled through two control points, named "X:go" and "X:stop".

> [!NOTE]
> Flex tracks, interlocking and crossing track types are planned, but not supported yet. Crossing can be implemented as two independent segments, but no special rule protects against collisions (yet). Special cases, such as double tracks, can be represented as two independent segments.

## Lengths

All lengths and post values int this database represent the protype scale values, typically the full scale size, not the actual size of the parts. Which unit is considered (meter, inch, mile) is a convention, which should be consistent within the same layout. It is not critical that the prototype scale be consistent with the model railroad scale (N, HO, O, etc). It is probably reasonable to match the milepost signs visible on the layout, if these are physically consistent with each others (i.e. the distance between two adjacent mileposts matches the marking on these two posts).

> [!NOTE]
> It is important that the prototype scale used in this track topology matches the speed scale used when controlling the trains. See HouseDCC for more information.

A unique location on the layout is determined by the combination of a line name and post value, e.g. "main 170".

This database actually carries two way to measure a track's length:

* Posts, used for train tracking and location, as described above.
* Physical length, used for building displays. This length is typically derived from the track's vendor specification (actual length of the product and scale).

> [!NOTE}
> The physical length is also a prototype length. For an N scale track, the physical length from the vendor specification is typically multiplied by 160.

> [!NOTE}
> For accuracy of display calculation, physical length must be provided in millimiters (if metrics units are used). The software is designed to be unit-agnostic, but using imperial units has not been tested yet.

If the physical length of an element is not specified, it is derived from its post length, multiplied by the distance between two posts (see Global Parameters below).

An explicit scale value can be specified for a layout, using the `rail.scale` element (a positive number). If not present, the default is typically 160 (N scale), or the default scale of the track catalog, if any. Use with caution.

## Locations

There are two equivalent methods for identifying a specific track location on the layout:

* Use `line` and `post` (absolute post value), or
* Use `segments` and `post` (post value relative to the segment low post)..

The benefit of the second method is that a change in the list of segments does ot impact those locations on segments that were untouched. Otherwise, any change to segments might change how posts are calculated, and thus impact many locations in the layout.

## Signal Logic

A signal protects a portion of track, often in combination with other signals. This typically protects a switch, an interlocking or a bridge. If there is a group of adjacent such segments, the signal protects the whole group of segments by referencing the most upstream segment.

An interlocking is a special case, as some switches are connected through their branches, and the concept of most upstream segment becomes ambiguous (on which line?). In that case all signals and switch devices should refer (implicitely or explicitely) to the same switch, typically one on the main line.

Signals that protect a bridge, or a sequence of adjacent bridges, follow a logic similar to switches.

Signals can also protect a simple section of track. By default such signals protect the transition between their own segment and the next segment in each signal's direction. If an explicit link to a regular segment is provided, the signal protects the portion of tracks between its own segment and the linked segment. In that case, two signals protect the same portion of track if their links refer to each other's segment.

As a general rule, the software automatically detects the common cases by walking the track starting from the signal. If the topology is too complex, an explicit `defend` field should be provided, both for switches and signals.

> [!NOTE]
> If a `defend` field is provided, the software will use it has =is, without applying any transitive upstream logic.

When multiple signals are linked to the same segment, clearing one signal in the group cancels the other signals in that group that conflict with the new cleared signal (i.e. those that authorized access to the same tracks in opposite directions). If a switch that belongs to the same group (the linked segment or an adjacent one) changes state, all signals in that group are cancelled.

## Layout Identification

The track topology data includes two fields that are intended for documentation purposes:

* `rail.layout`: the name of this layout. This field is mandatory.
* `rail.description`: a longer text that describes the intent of the layout. This field is optional.

## Global Track Parameters

Global track parameters are configurable elements of the `rail.track` object that apply to all track elements:

* `speeds.restricted`: the restricted speed value.
* `speeds.reverse`: the civil speed limit on a switch reverse branch.
* `distances.stop`: the safe stop distance. This is used when a train gets close to a danger point: end of line, unaligned switch or another train. This value is a combination of the train maximum speed and of the granularity of the train tracking. The later depends on the spacing between detectors, the type of the detectors and the spacing between train's detectable spots. A safe first estimate is a value greater than the spacing between two detectors.
* `distances.slow`: the safe slow distance. This is used when a train approaches a danger point (see above). This distance is typically double the stop distance.
* `distances.post`: the distance between two posts. This is used when converting a post length to a physical length for a display. If not present, default is 1000.
* `display.signal.light`: a string, value `first`, `last` or `arrow`. If `first`, the signal symbols are oriented light-then-pole-then-foot in the direction of the signal. If set to `last`, the signal symbols are oriented foot-then-pole-then-light in the direction of the signal. If `arrow`, the signal is shown using an arrow beside the light to indicates the direction. This field is optional, the default is `last`.
* `display.signal.foot`: a string, value `show` or `hide`. The signal foots will not be shown on the track display if set to `hide` (the signal poles will still be shown). This field is ignored if `display.signal.light` is set to `arrow`; otherwise it is optional and the default is `show`.
* `display.colors.background`: a string that represents the color of the background for the track display. Any color string accepted in SVG is valid here. This field is optional, the default background color is `#355b1eff`.
* `display.colors.foreground`: a string that represents the color of the foreground for the track display. Any color string accepted in SVG is valid here. This field is optional, the default foreground color is `white`.
* `periods.poll`: the field poll period in milliseconds. The value must be within the range 10ms to 999ms. Default is 200ms.
* `catalog`: a string that represents the name of the track catalog to load. See section Track Catalogs.

## Track Models

The models are stored in the `rail.track.models` array. Each element is an object that follows the schema below:

* `id`: vendor's product ID of the part.
* `length`: the length of the part (standard track) or the length of the normal branch (switch track).
* `civil`: the civil speed limit for that track.
* `reverse`: the length of the reverse branch of a switch. Not present for standard tracks. A positive value denotes a switch.
* `shape`: this optional field is used to generate the track display. This is an array of one to three elements: an array with one element contains the length only, arc and radius are 0; an array with two elements contains the arc (in degrees) and radius of the track; an array with three elements contains the arc, radius and length. For regular tracks: if the angle is positive, the track is curved and the second element is the radius of the curve; if the angle is 0, the track is straight and the last element is its physical length. For a switch (`reverse` is positive): the arc and radius represent the branch portion, the arc sign indicates the right (positive) or left (negative) handedness of the switch, and the lentgth is the physical length of the normal (straight) portion. If the `shape` element is not present, the track segment is straight and its physical length is derived from its `length` element.

Models can also be loaded from a catalog identified by the `rail.track.catalog` element. See section Track Catalogs.

## Track Segments

The segments are specific to a layout and are stored in the `rail.track.segments` array. Each element is an object that follows the schema below:

* `id`: an identifier for this segment, unique within the layout. The `id` string must not include any '~' characters.
* `line`: a line identifier. All standard tracks connected to each other belong to the same line. In the case of a switch, this refers to the normal direction.
* `previous`: the ID of the previous segment (decreasing posts order). This field is optional: the missing link will be retrieved based on the `next` or `branch` links from other segments.
* `next`: the ID of the next segment (increasing posts order). This field is optional if the subsequent segment (in the JSON data) is on the same line, in which case this subsequent segment will be used as the next segment.
* `branch`: the ID of the subsequent segment attached to the reverse point (switch only). The presence of this field indicates a switch. When the `branch` field is present, the `common` field becomes mandatory.
* `common`: the ID of the linked segment leading to the common point of the switch. This is the same as `previous` or `next`, depending on the orientation of the switch: if `common` is the same as `previous`, the switch is 'diverging', otherwise it is 'converging' (switch only).
* `start`: this optional field provides the starting post value for that segment. This is typically used for a branch parallel to a main line, and connected to the main line through a single 'converging' switch. This can also be used if the line name changes. This start value is always a low post value: posts will increase from there.
* `curve`: this element is only required if the model is a standard curved track. It represent the direction of the curve for this segment: `left` or `right`. The software also supports `curve` set to 1 (right) or -1 (left).
* `display`: this field is optional and should be present only once for each set of interconnected tracks. It indicates the position and direction of this segment's origin point on the display. This is an array with three elements: x, y and angle (degrees). The display position of all other segments connected to that segment will be inferred transitively by walking the tracks.
* `defend`: an optional reference to another segment. This is used for switches that depend on each others. See the Signal Logic section for more details.

If is valid for a segment to have either no `previous` or no `next` segment, even after `next` and `previous` link inferrences, but not both can be missing at the same time. One missing link indicates an end to the line.

A layout is oriented: the previous/next linkage is considered ordered according to increasing posts; the `next` element links to increasing posts while `previous` links to decreasing posts. If a point of the track segment is a line terminal point, the corresponding linkage to the adjacent segment is missing.

The name of the line on the reverse point of a switch is determined from the name of the normal branch of the adjacent segment. If multiple switches are connected to each other, that name is determined by transitively following the _normal_ linkages until a standard segment has been found. If two switches are connected through their reverse points, that portion of track has no name. This case should be considered an interlocking anyway as one cannot operate the two switches independently without risking a derail.

> [!NOTE]
> The `display` segment does not need to be a `start` segment: the two concepts are independent. The `start` field defines the posts values along a line, while the `display` element defines the location on the display.

> [!NOTE]
> The `display` x and y values are not critical. The software calculates the size of the viewbox so that the display will show the whole layout, centered.

## Track Detectors

Detectors are specific to a layout and are stored in the `rail.track.detectors` array. Each element is an object that follows the schema below:

* `id`: an identifier for this detector, unique within the layout
* `line`: name of the track line where this detector resides.
* `segment`: name of the segment where this detector resides.
* `low`: the low post limit for vehicle detection.
* `high`: the high post limit for vehicle detection.

> [!NOTE]
> The fields `line` and `segment` are mutually exclusives. If `line` is present, `low` and `high` represent absolute post values. If `segment` is present, `low` and `high` represent values relatives to the segment low post.

> [!WARNING]
> In this design a detector can cover at most one full segment.

## Signals

Signals are specific to a layout and are stored in the `rail.track.signals` array. Each element is an object that follows the schema below:

* `id`: an identifier for this signal, unique within the layout
* `line`: name of the track line where this signal is located.
* `segment`: name of the segment where this signal is located.
* `post`: the post where this signal is located.
* `dir`: the direction that this signal protects.
* `defend`: this optional field refer to a track segment that the signal protects. The default is the next segment in the signal's direction. See the Signal Logic section above for more details.

> [!NOTE]
> The fields `line` and `segment` are mutually exclusives. If `line` is present, `post` represents an absolute post value. If `segment` is present, `post` represents a value relative to the segment low post.

> [!NOTE}
>  When switches are linked through branches (e.g. an interlocking), the `defend` field must be provided explicitely. All signals in the group must be linked to the same protected segment.

## Track Catalogs

Track models can be be stored separately from the layout, to make it possible to build databases of models shared by multiple layouts, using the `track.catalog` element (a string). When present, this directs the software to search for a file of that name (with the "json" extension added) in the following directories:

* The directory path provided with the `-catalog=PATH` option, or else
* directory `/var/house/rail/catalogs` (catalogs installed by the user), or
* directory `/usr/local/share/house/rail/catalogs` (catalogs installes with HouseRail).

> [!NOTE}
> If the layout references catalog "MyPreferredTrackVendor", then the software will search for a file names "MyPreferredTrackVendor.json".

A catalog data follows the JSON schema below:

* `description`: a free format text that describes the catalog (typically the vendor and product line). This field is optional.
* `scale`: an optional number to convert part dimensions to prototype sizes.
* `track.packs`: a description on how track elements are sold together. This is intended to produce a bill of material that describe what to purchase.
* `track.models`: an array that lists the track models in this catalog. This use the same schema as for the models in the track layout, with one critical exception: all length and radius values correspond to the actual part dimensions.

