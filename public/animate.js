// Animate.js: change the properties of HTML/SVG elements based on
// the current status returned by the server.

var LatestStatus = 0;
var RootUrl = "";

var ChangeTurnout = new Object();
ChangeTurnout['normal'] = 'reverse';
ChangeTurnout['reverse'] = 'normal';

function setTurnout () {
    var id = this.id.split ('~');
    var url = RootUrl+"/switch?id="+id[0]+"&cmd="+ChangeTurnout[id[1]];
    var command = new XMLHttpRequest();
    command.open("GET", url);
    command.onreadystatechange = function () {
        if (command.readyState === 4 && command.status === 200) {
            var response = JSON.parse(command.responseText);
            updateDisplay (response);
        }
    }
    command.send(null);
}

function turnoutSetReverse () {
}

function updateDisplay (response) {

    if (response.latest) LatestStatus = response.latest;

    // TBD: switch (turnout) status
    for (var i = 0; i < response.rail.switch.length; ++i) {
        var turnout =  response.rail.switch[i];
        var id = turnout[0];
        var active, inactive;
        if (turnout[1] == 'normal') {
            active = document.getElementById (id + '~normal');
            inactive = document.getElementById (id + '~reverse');
        } else {
            inactive = document.getElementById (id + '~normal');
            active = document.getElementById (id + '~reverse');
        }
        if (inactive) {
            inactive.setAttribute ('stroke', 'grey');
            active.removeEventListener ('click', setTurnout);
        }
        if (active) {
            active.setAttribute ('stroke', 'inherit');
            active.parentNode.appendChild (active);
            active.addEventListener ('click', setTurnout);
        }
    }

    // TBD: show trains
}

function railStatus () {
    var url = RootUrl+"/status";
    if (LatestStatus) url += "?known=" + LatestStatus;
    var command = new XMLHttpRequest();
    command.open("GET", url);
    command.onreadystatechange = function () {
        if (command.readyState === 4 && command.status === 200) {
            var response = JSON.parse(command.responseText);
            updateDisplay (response);
        }
    }
    command.send(null);
}

function animateStart (path) {
   RootUrl = path;
   railStatus();
   setInterval (railStatus, 1000);
}

