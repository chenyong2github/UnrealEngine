// Copyright Epic Games, Inc. All Rights Reserved.

// repeat with the interval of 2 seconds
let timerId = setInterval(() => requestEvents(), 4000);

function logMessage(msg)
{
	console.log(msg);
	document.getElementById('debugDiv').innerHTML += '<p><pre>' + msg + '</pre></p>';
}

function clearLog() {
	document.getElementById('debugDiv').innerHTML = '';
}

// Based on https://developer.mozilla.org/en-US/docs/Learn/HTML/Forms/Sending_forms_through_JavaScript
function sendCmd(data, handler) {
	var XHR = new XMLHttpRequest();

	// Define what happens on successful data submission
	XHR.onload = function (event) {

		// Convert the response's JSON text into a javascript Object
		var replyObj = JSON.parse(event.srcElement.responseText);
		logMessage("RECEIVE_OK: " + JSON.stringify(replyObj, null, 4));

		if ('reply' in replyObj)
			if (handler != undefined)
				handler(replyObj.reply);
		if ('events' in replyObj)
			handleEvents(replyObj.events);
	};

	// Define what happens in case of error
	XHR.onerror = function (event) {
		logMessage("SEND FAILED.");
	};

	// Set up our request
	XHR.open('POST', 'http://127.0.0.1:40080');

	XHR.setRequestHeader('Content-Type', 'application/json');

	// Finally, send our data.
	logMessage("SEND: " + JSON.stringify(data, null, 4));
	XHR.send(JSON.stringify(data));
}

function handleEvents(events)
{
	if (events.length == 0)
		return;

	logMessage("Handling events");
}

function requestEvents() {
	return;
	sendCmd(
		{
			'cmd': 'getevents', 'params': {} },
		function () {
			logMessage("Handling reply");
		})
}