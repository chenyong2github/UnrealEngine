// Copyright Epic Games, Inc. All Rights Reserved.
var enableRedirectionLinks = true;
var enableRESTAPI = true;

// Microsoft Notes: This file has additions to the Unreal Engine Matchmaker that were done in conjuction with Epic to
// add improved capabilities, resiliency and abilities to deploy and scale Pixel Streaming in Azure.
// "MSFT Improvement" -- Areas where Microsoft have added additional code to what is exported out of Unreal Engine
// "AZURE" -- Areas where Azure specific code was added (i.e., modules/azure.js, scale up/down, logging, metrics, etc.)

//////////////////////////// MSFT Improvement  ////////////////////////////	
// Added a config file for Matchmaker (MM)
const defaultConfig = {
	// The port clients connect to the matchmaking service over HTTP
	httpPort: 90,
	UseHTTPS: false,
	// The matchmaking port the signaling service connects to the matchmaker
	matchmakerPort: 9999,

	/////////////////////////////// AZURE ///////////////////////////////	
	// The amount of instances deployed per node, to be used in the autoscale policy (i.e., 1 unreal app running per GPU VM) -- FUTURE
	instancesPerNode: 1,
	// The amount of available signaling service / App instances we want to ensure are available before we have to scale up (0 will ignore)
	instanceCountBuffer: 5,
	// The percentage amount of available signaling service / App instances we want to ensure are available before we have to scale up (0 will ignore)
	percentBuffer: 25,
	//The amount of minutes of no scaling up activity before we decide we might want to see if we should scale down (i.e., after hours--reduce costs)
	idleMinutes: 60,
	// The percentage of active connections to total instances that we want to trigger a scale down once idleMinutes passes with no scaleup
	connectionIdleRatio: 25,
	// The minimum number of available app instances we want to scale down to during an idle period (idleMinutes passed with no scaleup)
	minIdleVmssInstanceCount: 0,
	// The total amount of VMSS nodes that we will approve scaling up to
	maxVmssInstanceScaleCount: 500,
	// The subscription used for autoscaling policy
	subscriptionId: "",
	// The Azure ResourceGroup where the Azure VMSS is located, used for autoscaling
	resourceGroup: "",
	// The Azure VMSS name used for scaling the Signaling Service / Unreal App compute
	virtualMachineScaleSet: "",
	// Azure App Insights ID for logging
	appInsightsId: "",
	// Log to file
	LogToFile: true,
	// Number of seconds between scaling evaluations
	scalingEvaluationInterval: 30
	/////////////////////////////////////////////////////////////////////

};

// Similar to the Signaling Server (SS) code, load in a config.json file for the MM parameters
const argv = require('yargs').argv;

var configFile = (typeof argv.configFile != 'undefined') ? argv.configFile.toString() : '.\\config.json';
console.log(`configFile ${configFile}`);
const config = require('./modules/config.js').init(configFile, defaultConfig);
console.log("Config: " + JSON.stringify(config, null, '\t'));
///////////////////////////////////////////////////////////////////////////


const express = require('express');
var cors = require('cors');
const app = express();
const http = require('http').Server(app);
const fs = require('fs');
const path = require('path');
const logging = require('./modules/logging.js');
logging.RegisterConsoleLogger();

/////////////////////////////// AZURE ///////////////////////////////
// Initialize the Azure module for scale and metric functionality
const azure = require('./modules/azure.js');
azure.init(config, app);
/////////////////////////////////////////////////////////////////////

if (config.LogToFile) {
	logging.RegisterFileLogger('./logs');
}

// A list of all the Cirrus server which are connected to the Matchmaker.
var cirrusServers = new Map();

//
// Parse command line.
//

if (typeof argv.httpPort != 'undefined') {
	config.httpPort = argv.httpPort;
}
if (typeof argv.matchmakerPort != 'undefined') {
	config.matchmakerPort = argv.matchmakerPort;
}

http.listen(config.httpPort, () => {
    console.log('HTTP listening on *:' + config.httpPort);
});


//////////////////////////// MSFT Improvement  ////////////////////////////	
// Added HTTPS code for Matchmaker from SS (due to some corporate policies requiring it)
if (config.UseHTTPS) {
	//HTTPS certificate details
	const options = {
		key: fs.readFileSync(path.join(__dirname, './certificates/client-key.pem')),
		cert: fs.readFileSync(path.join(__dirname, './certificates/client-cert.pem'))
	};

	var https = require('https').Server(options, app);

	//Setup http -> https redirect
	console.log('Redirecting http->https');
	app.use(function (req, res, next) {
		if (!req.secure) {
			if (req.get('Host')) {
				var hostAddressParts = req.get('Host').split(':');
				var hostAddress = hostAddressParts[0];
				if (httpsPort != 443) {
					hostAddress = `${hostAddress}:${httpsPort}`;
				}
				return res.redirect(['https://', hostAddress, req.originalUrl].join(''));
			} else {
				console.error(`unable to get host name from header. Requestor ${req.ip}, url path: '${req.originalUrl}', available headers ${JSON.stringify(req.headers)}`);
				return res.status(400).send('Bad Request');
			}
		}
		next();
	});

	https.listen(443, function () {
		console.log('Https listening on 443');
	});
}
///////////////////////////////////////////////////////////////////////////


// No servers are available so send some simple JavaScript to the client to make
// it retry after a short period of time.
function sendRetryResponse(res) {
	res.send(`All ${cirrusServers.size} Cirrus servers are in use. Retrying in <span id="countdown">10</span> seconds.
	<script>
		var countdown = document.getElementById("countdown").textContent;
		setInterval(function() {
			countdown--;
			if (countdown == 0) {
				window.location.reload(1);
			} else {
				document.getElementById("countdown").textContent = countdown;
			}
		}, 1000);
	</script>`);
}

// Get a Cirrus server if there is one available which has no clients connected.
function getAvailableCirrusServer() {
	for (cirrusServer of cirrusServers.values()) {
		if (cirrusServer.numConnectedClients === 0 && cirrusServer.ready === true) {

			//////////////////////////// MSFT Improvement  ////////////////////////////	
			// Check if we had at least 45 seconds since the last redirect, avoiding the 
			// chance of redirecting 2+ users to the same SS before they click Play.
			if( cirrusServer.lastRedirect ) {
				if( ((Date.now() - cirrusServer.lastRedirect) / 1000) < 45 )
					continue;
			}
			cirrusServer.lastRedirect = Date.now();
			///////////////////////////////////////////////////////////////////////////

			return cirrusServer;
		}
	}
	
	console.log('WARNING: No empty Cirrus servers are available');
	return undefined;
}

if(enableRESTAPI) {
	// Handle REST signalling server only request.
	app.options('/signallingserver', cors())
	app.get('/signallingserver', cors(),  (req, res) => {
		cirrusServer = getAvailableCirrusServer();
		if (cirrusServer != undefined) {
			res.json({ signallingServer: `${cirrusServer.address}:${cirrusServer.port}`});
			console.log(`Returning ${cirrusServer.address}:${cirrusServer.port}`);
		} else {
			res.json({ signallingServer: '', error: 'No signalling servers available'});
		}
	});
}

if(enableRedirectionLinks) {
	// Handle standard URL.
	app.get('/', (req, res) => {
		cirrusServer = getAvailableCirrusServer();
		if (cirrusServer != undefined) {
			res.redirect(`http://${cirrusServer.address}:${cirrusServer.port}/`);
			//console.log(req);
			console.log(`Redirect to ${cirrusServer.address}:${cirrusServer.port}`);
		} else {
			sendRetryResponse(res);
		}
	});

	// Handle URL with custom HTML.
	app.get('/custom_html/:htmlFilename', (req, res) => {
		cirrusServer = getAvailableCirrusServer();
		if (cirrusServer != undefined) {
			res.redirect(`http://${cirrusServer.address}:${cirrusServer.port}/custom_html/${req.params.htmlFilename}`);
			console.log(`Redirect to ${cirrusServer.address}:${cirrusServer.port}`);
		} else {
			sendRetryResponse(res);
		}
	});
}

//
// Connection to Cirrus.
//

const net = require('net');

function disconnect(connection) {
	console.log(`Ending connection to remote address ${connection.remoteAddress}`);
	connection.end();
}

const matchmaker = net.createServer((connection) => {
	connection.on('data', (data) => {
		try {
			message = JSON.parse(data);

			if(message)
				console.log(`Message TYPE: ${message.type}`);
		} catch(e) {
			console.log(`ERROR (${e.toString()}): Failed to parse Cirrus information from data: ${data.toString()}`);
			disconnect(connection);
			azure.appInsightsLogError(e);   //////// AZURE ////////
			return;
		}
		if (message.type === 'connect') {
			// A Cirrus server connects to this Matchmaker server.
			cirrusServer = {
				address: message.address,
				port: message.port,
				numConnectedClients: 0,
				lastPingReceived: Date.now()
			};
			cirrusServer.ready = message.ready === true;

			//////////////////////////// MSFT Improvement  ////////////////////////////	
			// Handles disconnects between MM and SS to not add dupes with numConnectedClients = 0 and redirect users to same SS
			// Check if player is connected and doing a reconnect. message.playerConnected is a new variable sent from the SS to
			// help track whether or not a player is already connected when a 'connect' message is sent (i.e., reconnect).
			if(message.playerConnected == true) {
				cirrusServer.numConnectedClients = 1;
			}

			// Find if we already have a ciruss server address connected to (possibly a reconnect happening)
			let server = [...cirrusServers.entries()].find(([key, val]) => val.address === cirrusServer.address && val.port === cirrusServer.port);

			// if a duplicate server with the same address isn't found -- add it to the map as an available server to send users to.
			if (!server || server.size <= 0) {
				console.log(`Adding connection for ${cirrusServer.address.split(".")[0]} with playerConnected: ${message.playerConnected}`)
				cirrusServers.set(connection, cirrusServer);
            } else {
				console.log(`RECONNECT: cirrus server address ${cirrusServer.address.split(".")[0]} already found--replacing. playerConnected: ${message.playerConnected}`)
				var foundServer = cirrusServers.get(server[0]);
				
				// Make sure to retain the numConnectedClients from the last one before the reconnect to MM
				if (foundServer) {					
					cirrusServers.set(connection, cirrusServer);
					console.log(`Replacing server with original with numConn: ${cirrusServer.numConnectedClients}`);
					cirrusServers.delete(server[0]);
				} else {
					cirrusServers.set(connection, cirrusServer);
					console.log("Connection not found in Map() -- adding a new one");
				}

				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("DuplicateCirrusConnection", 1);
				azure.appInsightsLogEvent("DuplicateCirrusConnection", message.address);
			}
			///////////////////////////////////////////////////////////////////////////

		} else if (message.type === 'streamerConnected') {
			// The stream connects to a Cirrus server and so is ready to be used
			cirrusServer = cirrusServers.get(connection);
			if(cirrusServer) {
				cirrusServer.ready = true;
				console.log(`Cirrus server ${cirrusServer.address}:${cirrusServer.port} ready for use`);

				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("StreamerConnected", 1);						
				azure.appInsightsLogEvent("StreamerConnected", cirrusServer.address);	
			} else {
				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("CirrusServerUndefined", 1);					
				azure.appInsightsLogEvent("CirrusServerUndefined", `No cirrus server found on streamer connect: ${connection.remoteAddress}`);
				disconnect(connection);
			}
		} else if (message.type === 'streamerDisconnected') {
			// The stream connects to a Cirrus server and so is ready to be used
			cirrusServer = cirrusServers.get(connection);
			if(cirrusServer) {
				cirrusServer.ready = false;
				console.log(`Cirrus server ${cirrusServer.address}:${cirrusServer.port} no longer ready for use`);

				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("StreamerDisconnected", 1);
				azure.appInsightsLogEvent("StreamerDisconnected", cirrusServer.address);
			} else {
				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("CirrusServerUndefined", 1);
				azure.appInsightsLogEvent("CirrusServerUndefined", `No cirrus server found on streamer disconnect: ${connection.remoteAddress}`);
				disconnect(connection);
			}
		} else if (message.type === 'clientConnected') {
			// A client connects to a Cirrus server.
			cirrusServer = cirrusServers.get(connection);
			if(cirrusServer) {
				cirrusServer.numConnectedClients++;
				console.log(`Client connected to Cirrus server ${cirrusServer.address}:${cirrusServer.port}`);

				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("ClientConnection", 1);
				azure.appInsightsLogEvent("ClientConnection", cirrusServer.address);
			} else {
				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("CirrusServerUndefined", 1);
				azure.appInsightsLogEvent("CirrusServerUndefined", `No cirrus server found on client connect: ${connection.remoteAddress}`);

				disconnect(connection);
			}
		} else if (message.type === 'clientDisconnected') {
			// A client disconnects from a Cirrus server.
			cirrusServer = cirrusServers.get(connection);
			if(cirrusServer) {
				cirrusServer.numConnectedClients--;
				console.log(`Client disconnected from Cirrus server ${cirrusServer.address}:${cirrusServer.port}`);

				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("ClientDisconnected", 1);
				azure.appInsightsLogEvent("ClientDisconnected", cirrusServer.address);
			} else {				
				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("CirrusServerUndefined", 1);
				azure.appInsightsLogEvent("CirrusServerUndefined", `No cirrus server found on client disconnect: ${connection.remoteAddress}`);

				disconnect(connection);
			}
		} else if (message.type === 'ping') {
			cirrusServer = cirrusServers.get(connection);
			if(cirrusServer) {
				cirrusServer.lastPingReceived = Date.now();
			} else {				
				/////////////////////////////// AZURE ///////////////////////////////	
				azure.appInsightsLogMetric("CirrusServerUndefined", 1);
				azure.appInsightsLogEvent("CirrusServerUndefined", `No cirrus server found on client disconnect: ${connection.remoteAddress}`);

				disconnect(connection);
			}
		} else {
			console.log('ERROR: Unknown data: ' + JSON.stringify(message));
			disconnect(connection);

			/////////////////////////////// AZURE ///////////////////////////////	
			azure.appInsightsLogMetric("MMBadMessageType", 1);
			azure.appInsightsLogEvent("MMBadMessageType", JSON.stringify(message));
		}

		/////////////////////////////// AZURE ///////////////////////////////	
		// Use the Azure Module to evaluate whether or not we should scale/up down the VMSS compute for streams
		registerAndDoScaleEval();
	});

	// A Cirrus server disconnects from this Matchmaker server.
	connection.on('error', () => {
		cirrusServer = cirrusServers.get(connection);
		if(cirrusServer) {
			cirrusServers.delete(connection);
			console.log(`Cirrus server ${cirrusServer.address}:${cirrusServer.port} disconnected from Matchmaker`);

			/////////////////////////////// AZURE ///////////////////////////////	
			azure.appInsightsLogEvent("MMCirrusDisconnect", `Cirrus server ${cirrusServer.address}:${cirrusServer.port} disconnected from Matchmaker`);
		} else {
			console.log(`Disconnected machine that wasn't a registered cirrus server, remote address: ${connection.remoteAddress}`);

			/////////////////////////////// AZURE ///////////////////////////////	
			azure.appInsightsLogEvent("MMCirrusDisconnect", `Disconnected machine that wasn't a registered cirrus server, remote address: ${connection.remoteAddress}`);
		}

		/////////////////////////////// AZURE ///////////////////////////////	
		azure.appInsightsLogMetric("MMCirrusDisconnect", 1);
	});
});

matchmaker.listen(config.matchmakerPort, () => {
	console.log('Matchmaker listening on *:' + config.matchmakerPort);
});

var scaleEvalRegistered = false;
function registerAndDoScaleEval()
{
	// situation can occur that scaling rules are ignored (scale process in progress)
	// therefore we register this Interval to do periodic checking if scaling is needed
	if(!scaleEvalRegistered) {
		scaleEvalRegistered = true;
		setInterval(function() {
			if(config.enableAutoScale) azure.evaluateAutoScalePolicy(cirrusServers);
			azure.checkIfNodesAreStillResponsive(cirrusServers);
		}, config.scalingEvaluationInterval * 1000);
	}
}