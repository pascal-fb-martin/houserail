// Animate.js: change the properties of HTML/SVG elements based on
// the current status returned by the server.

var RootUrl = "";
var LatestConfig = 0;
var LatestStatus = 0;

var ChangeTurnout = new Object();
ChangeTurnout['normal'] = 'reverse';
ChangeTurnout['reverse'] = 'normal';

var TrackSegments = null;
var KnownTrainLocations = new Array();

function searchSegments (line, low, high) {

    if (!TrackSegments) return null;

    var linesegments = TrackSegments[line];
    if (!linesegments) return null;

    if (low > high) { // Segments are oriented in increasing posts.
       var temp = low;
       low = high;
       high = temp;
    }

    var begin = 0;
    var end = linesegments.length - 1;
    var cursor;

    while (begin < end - 1) {
        cursor = Math.trunc ((end + begin) / 2);
        var segment = linesegments[cursor];

        var seglow = segment[2];
        var seghigh = segment[3];
        if (high <= seglow) {
           end = cursor - 1;
           continue;
        }
        if (low >= seghigh) {
           begin = cursor + 1;
           continue;
        }
        var changed = false;
        if (low >= seglow) {
           begin = cursor;
           changed = true;
        }
        if (high <= seghigh) {
           end = cursor;
           changed = true;
        }
        if (!changed) break; // Avoid infinite loops
    }

    // The line portion is between begin and end.
    // Search for the exact edge segment(s).

    for (; begin <= end; ++begin) {
        var segment = linesegments[begin];
        if ((low >= segment[2]) && (low < segment[3])) break;
    }
    if (begin > end) return null;

    for (; end >= begin; --end) {
        var segment = linesegments[end];
        if ((high > segment[2]) && (high <= segment[3])) break;
    }
    if (end < begin) return null;

    // Return the full information for each covered segment,
    // so that the caller can handle partial coverage.

    var result = new Array();
    for (cursor = begin; cursor <= end; ++cursor) {
        result.push (linesegments[cursor]);
    }
    return result;
}

function updateSegments (response) {

    if (response.latest) LatestConfig = response.latest;

    if (!response.rail.segment) return;
    var segments = response.rail.segment;

    // Split the sorted list into one list per line, each one still sorted.
    TrackSegments = new Object();
    for (var i = 0; i < segments.length; ++i) {
        var segment = segments[i];
        if (!TrackSegments[segment[1]])
            TrackSegments[segment[1]] = new Array();
        TrackSegments[segment[1]].push (segment);
    }
    LatestStatus = 0; // Force a display refresh.
}

function railSegments () {
    var url = RootUrl+"/track/segments";
    if (LatestConfig) url += "?known=" + LatestConfig;
    var command = new XMLHttpRequest();
    command.open("GET", url);
    command.onreadystatechange = function () {
        if (command.readyState === 4 && command.status === 200) {
            updateSegments (JSON.parse(command.responseText));
        }
    }
    command.send(null);
}

function setTurnout () {
    var id = this.id.split ('~');
    var url = RootUrl+"/switch?id="+id[0]+"&cmd="+ChangeTurnout[id[1]];
    var command = new XMLHttpRequest();
    command.open("GET", url);
    command.onreadystatechange = function () {
        if (command.readyState === 4 && command.status === 200) {
            updateDisplay (JSON.parse(command.responseText));
        }
    }
    command.send(null);
}

// Track segments color conventions:
//
var SegmentOccupied = 'yellow';
var SegmentVacant = 'inherit';
var SegmentInactive = 'grey';
var SegmentTrain = 'red';

function updateDisplay (response) {

    if (response.latest) LatestStatus = response.latest;

    // Note: the switch status ('normal' or 'reverse') combines with
    // the track segment status ('on', 'off'): the segment status color
    // follows the switch's active side, while the other side remains grey.

    // Switch (turnout) status
    //
    if (response.rail.switch) {
        for (var i = 0; i < response.rail.switch.length; ++i) {
            var turnout =  response.rail.switch[i];
            var id = turnout[0];
            var normal = document.getElementById (id + '~normal');
            var reverse = document.getElementById (id + '~reverse');
            var active, inactive;
            if (turnout[1] == 'normal') {
                active = normal;
                inactive = reverse;
            } else {
                active = reverse;
                inactive = normal;
            }
            var status = SegmentVacant; // Plan to keep the segment status.
            if (inactive) {
                status = inactive.getAttribute ('stroke');
                if (status == SegmentInactive) continue; // No change.

                inactive.setAttribute ('stroke', SegmentInactive);
                active.removeEventListener ('click', setTurnout);
            }
            if (active) {
                active.setAttribute ('stroke', status);
                active.parentNode.appendChild (active);
                active.addEventListener ('click', setTurnout);
            }
        }
    }

    // Segment occupancy status
    //
    for (var i = 0; i < response.rail.segment.length; ++i) {
        var segment = response.rail.segment[i];
        var stroke = (segment[1] == 'on') ? SegmentOccupied : SegmentVacant;
        var normal = document.getElementById (segment[0]+'~normal');
        var reverse = document.getElementById (segment[0]+'~reverse');
        if (reverse) {
            if (reverse.getAttribute('stroke') == SegmentInactive) {
                if (normal) normal.setAttribute ('stroke', stroke);
            } else {
                reverse.setAttribute ('stroke', stroke);
            }
        } else if (normal) {
            normal.setAttribute ('stroke', stroke);
        }
    }

    // Train location
    if (!TrackSegments) return; 

    // Erase all previously known trains locations, if any.
    // TBD: make it smarter and avoid touching trains that did not move?
    //
    for (var i = 0; i < KnownTrainLocations.length; ++i) {
        KnownTrainLocations[i].setAttribute ('stroke', 'none');
    }
    KnownTrainLocations = new Array();

    if (!response.rail.train) return;

    // Show the listed trains.
    // TBD: show exact train limits using dashed lines..
    //
    for (var i = 0; i < response.rail.train.length; ++i) {
        var train = response.rail.train[i];
        var path = train.path;
        for (var j = 0; j < path.length; ++j) {
            var section = path[j];
            var segments = searchSegments (section[0], section[1], section[2]);
            if (segments) {
                for (var k = 0; k < segments.length; ++k) {
                    var segment = segments[k];
                    var id = segment[0].split ('~');
                    var element;
                    if ((id.length > 1) && (id[1] == 'reverse')) {
                        element = document.getElementById (id[0]+'~branch');
                    } else {
                        element = document.getElementById (id[0]);
                    }
                    element.setAttribute ('stroke', SegmentTrain);
                    KnownTrainLocations.push(element);
                }
            }
        }
    }
}

function railStatus () {
    var url = RootUrl+"/status";
    if (LatestStatus) url += "?known=" + LatestStatus;
    var command = new XMLHttpRequest();
    command.open("GET", url);
    command.onreadystatechange = function () {
        if (command.readyState === 4 && command.status === 200) {
            updateDisplay (JSON.parse(command.responseText));
        }
    }
    command.send(null);
}

function animateStart (path) {
   RootUrl = path;
   railSegments();
   setInterval (railSegments, 5000);
   railStatus();
   setInterval (railStatus, 500);
}

