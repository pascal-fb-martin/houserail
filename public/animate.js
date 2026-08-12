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

var DisplayTransform = [1, 0, 0, 1, 0, 0];
var DisplayCenter = new Object();
var DisplayDragStart = new Object();
var DisplayDragging = false;
var DisplayView = new Object();
var DisplaySvg = null;
var PanZoomGroup;

function prepareDisplayPanZoom () {

    DisplaySvg = document.getElementById ('container');
    PanZoomGroup = document.getElementById ('panzoom');

    var box = DisplaySvg.getAttribute ('viewBox').split (' ');
    DisplayView.x = parseInt (box[0]);
    DisplayView.y = parseInt (box[1]);
    DisplayView.width = parseInt (box[2]);
    DisplayView.height = parseInt (box[3]);

    DisplayCenter.x = DisplayView.x + DisplayView.width / 2;
    DisplayCenter.y = DisplayView.y + DisplayView.height / 2;

    DisplaySvg.addEventListener ('mousedown', (e) => {

        DisplayDragging = true;
        DisplaySvg.style.cursor = 'grabbing';

        DisplayDragStart.x = e.clientX;
        DisplayDragStart.y = e.clientY;
    });

    DisplaySvg.addEventListener ('mousemove', (e) => {

        if (!DisplayDragging) return;

        // Adjust for the scale difference between SVG
        // coordinates and screen pixels, then pan the display.

        const rectangle = DisplaySvg.getBoundingClientRect();
        const scalewidth = DisplayView.width / rectangle.width;
        const scaleheight = DisplayView.height / rectangle.height;

        const dx = scalewidth * (e.clientX - DisplayDragStart.x);
        const dy = scaleheight * (e.clientY - DisplayDragStart.y);

        displayPan (dx, dy);

        // The current position is now the start for the next move.
        DisplayDragStart.x = e.clientX;
        DisplayDragStart.y = e.clientY;
    });

    DisplaySvg.addEventListener ('mouseup', (e) => {

        if (!DisplayDragging) return;

        DisplayDragging = false;
        DisplaySvg.style.cursor = 'auto';
    });

    DisplaySvg.addEventListener ('wheel', (e) => {

        e.preventDefault();
        const zoomIntensity = 0.1;
        const direction = e.deltaY < 0 ? 1 : -1;
        const scale = 1 + direction * zoomIntensity;

        displayZoom (scale);
    }, {passive: false});
}

function displayApplyTransform () {

    PanZoomGroup.setAttribute ('transform',
                               'matrix('+DisplayTransform.join (' ')+')');
}

function displayReset () {
    DisplayTransform = [1, 0, 0, 1, 0, 0];
    displayApplyTransform ();
}

function displayPan (h, v) {

    DisplayTransform[4] += h;
    DisplayTransform[5] += v;
    displayApplyTransform ();
}

function displayZoom (scale) {

    for (var i = 0; i < 4; i++) {
        DisplayTransform[i] *= scale;
    }
    DisplayTransform[4] += (1 - scale) * DisplayCenter.x;
    DisplayTransform[5] += (1 - scale) * DisplayCenter.y;
    displayApplyTransform ();
}

function searchSegments (line, low, high) {

    if (!TrackSegments) return null;

    var linesegments = TrackSegments[line];
    if (!linesegments) return null;

    if (low > high) { // Segments are oriented in increasing posts order.
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

    return linesegments.slice (begin, end+1);
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

function setSignal () {
    var id = this.id.split ('~');
    var color = this.getAttribute ('fill');
    var cmd;
    if (color == SignalGo) cmd = 'stop';
    else cmd = 'go';

    var url = RootUrl+"/signal?id="+id[0]+"&cmd="+cmd;
    var command = new XMLHttpRequest();
    command.open("GET", url);
    command.onreadystatechange = function () {
        if (command.readyState === 4) {
            if (command.status === 200) {
                updateDisplay (JSON.parse(command.responseText));
            } else if ((command.status === 404) || (command.status === 500)) {
                window.alert ('Error '+command.status+': '+command.statusText);
            }
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
var SegmentDirection = 'yellow';
var SegmentDirectionStopped = 'white';
var SignalStop = 'red';
var SignalGo = 'lime';
var SignalOld = 'white';

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
                active.removeEventListener ('mouseup', setTurnout);
            }
            if (active) {
                active.setAttribute ('stroke', status);
                active.parentNode.appendChild (active);
                active.addEventListener ('mouseup', setTurnout);
            }
        }
    }

    // Signal status
    //
    if (response.rail.signal) {
        for (var i = 0; i < response.rail.signal.length; ++i) {
            var signal =  response.rail.signal[i];
            var element = document.getElementById (signal[0] + '~sig');
            var fill = 'white';
            if (signal[1] == 'go') fill = SignalGo;
            else if (signal[1] == 'stop') fill = SignalStop;
            else fill = SignalOld;
            element.setAttribute ('fill', fill);
            element.addEventListener ('mouseup', setSignal);
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
        KnownTrainLocations[i].removeAttribute ('stroke-dasharray');
        KnownTrainLocations[i].removeAttribute ('stroke-dashoffset');
    }
    KnownTrainLocations = new Array();

    // Show the listed trains.
    if (!response.rail.train) return;

    for (var i = 0; i < response.rail.train.length; ++i) {
        var train = response.rail.train[i];
        var section;  // Keep the last section.
        var segments; // Keep the segments for the last section.
        for (var j = 0; j < train.path.length; ++j) {
            section = train.path[j];
            segments = searchSegments (section[0], section[1], section[2]);
            if (segments) {
                for (var k = 0; k < segments.length; ++k) {
                    var segment = segments[k];
                    var seglength = segment[3] - segment[2];
                    var back = section[1] - segment[2];
                    var front = section[2] - segment[2];

                    if (back < 0) back = 0;
                    else if (back > seglength) back = seglength;
                    if (front < 0) front = 0;
                    else if (front > seglength) front = seglength;

                    var element = null;
                    var id = segment[0].split ('~');
                    if ((id.length > 1) && (id[1] == 'reverse')) {
                        element = document.getElementById (id[0]+'~branch');
                    } else {
                        element = document.getElementById (id[0]);
                    }
                    if (!element) continue;

                    var trainlength = Math.abs (front - back);
                    if (trainlength < seglength) {
                        var gap = seglength - trainlength + 1;
                        element.setAttribute ('stroke-dasharray',
                                              ''+trainlength+' '+gap);
                        if (front < back) back = front; // reverse
                        if (back > 0)
                            element.setAttribute ('stroke-dashoffset', '-'+back);
                    }
                    if (train.color)
                        element.setAttribute ('stroke', train.color);
                    else
                        element.setAttribute ('stroke', SegmentTrain);
                    KnownTrainLocations.push(element);
                }
            }
        }
        if (segments) {
            // Draw the direction mark for this train.
            // Select the first or last segment based on the direction
            // (segments are always sorted by increasing post)
            var segment;
            if (section[2] > section[1])
                segment = segments[segments.length - 1]; // Direction up.
            else
                segment = segments[0]; // Direction down.

            var element = null;
            var id = segment[0].split ('~');
            if ((id.length > 1) && (id[1] == 'reverse')) {
                element = document.getElementById (id[0]+'~dir~branch');
            } else {
                element = document.getElementById (id[0]+'~dir');
            }
            if (element) {
                var length = segment[3] - segment[2];
                var offset = section[2] - segment[2];
                if (section[2] > section[1]) offset -= 1;
                else offset += 1;
                if (offset < 0) offset = 0;
                else if (offset >= length) offset = length - 1;
                element.setAttribute ('stroke-dasharray', '0 '+(length+1));
                if (offset > 0)
                    element.setAttribute ('stroke-dashoffset', '-'+offset);
                if (train.proceed[1] > 0)
                    element.setAttribute ('stroke', SegmentDirection);
                else
                    element.setAttribute ('stroke', SegmentDirectionStopped);
                KnownTrainLocations.push(element);
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

   prepareDisplayPanZoom ();

   RootUrl = path;
   railSegments();
   setInterval (railSegments, 5000);
   railStatus();
   setInterval (railStatus, 50);
}

