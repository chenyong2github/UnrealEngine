// Copyright Microsoft Corp. All Rights Reserved.
// The following module adds the capabilities of scaling up and down Unreal Pixel Streaming in Azure.
// Virtual Machine Scale Sets (VMSS) compute in Azure is used to host the Signaling Server and Unreal app,
// which is scaled out to allow clients their own session. Review the parameters in the altered defaultConfig
// in matchmaker.js to set the scale out policies for your specific requirements.

// A variable to hold the last time we scaled up, used for determining if we are in a determined idle state and might need to scale down (via idleMinutes and connectionIdleRatio)
var lastScaleupTime = Date.now();
// A varible to the last time we scaled down, used for a reference to know how quick we should consider scaling down again (to avoid multiple scale downs too soon)
var lastScaledownTime = Date.now();
// The number of total app instances that are connecting to the matchmaker
var totalInstances = 0;
// The number of total client connections (users) streaming
var totalConnectedClients = 0;
// The min minutes between each scaleup (so we don't scale up every frame while we wait for the scale to complete)
var minMinutesBetweenScaleups = 1;
var minMinutesBetweenScaledowns = 2;
// This stores the current Azure Virtual Machine Scale Set node count (sku.capacity), retried by client.get(rg_name, vmss_name)
var currentVMSSNodeCount = -1;
// The stores the current Azure Virtual Machine Scale Set provisioning (i.e., scale) state (Succeeded, etc..)
var currentVMSSProvisioningState = null;
// The amount of ms for checking the VMSS state (10 seconds * 1000 ms) = 10 second intervals
var vmssUpdateStateInterval = 10 * 1000;
// The amount of percentage we need to scale up when autoscaling with a percentage policy
var scaleUpPercentage = 10;
// The amount of scaling up when we fall below a desired node buffer count policy
var scaleUpNodeCount = 5;

const fs = require('fs');
// Azure SDK Clients
const { ComputeManagementClient, VirtualMachineScaleSets } = require('@azure/arm-compute');
const msRestNodeAuth = require('@azure/ms-rest-nodeauth');
const logger = require('@azure/logger');
const appInsights = require('applicationinsights');
var config;

// TODO Add autoscaling and logging tweaks in this file and take the code out of the matchmaker.js file
function initAzureModule(configObj, app) {

	config = configObj;
	logger.setLogLevel('info');

	if (config.appInsightsId) {
		appInsights.setup(config.appInsightsId).setSendLiveMetrics(true).start();
	}
	if (!appInsights || !appInsights.defaultClient) {
		console.log("No valid appInsights object to use");
	}

	// this is an async method and we need to prime the outputs before we can use them
	getVMSSNodeCountAndState(config.subscriptionId, config.resourceGroup, config.virtualMachineScaleSet);

	// Added for health check of the VM/App when using Azure Traffic Manager
	app.get('/ping', (req, res) => {
		res.send('ping');
	});

	return null;
}

function appInsightsLogError(err) {
	
	if (!appInsights || !appInsights.defaultClient) {
		return;
	}

	appInsights.defaultClient.trackMetric({ name: "MatchMakerErrors", value: 1 });
	appInsights.defaultClient.trackException({ exception: err });
}

function appInsightsLogEvent(eventName, eventCustomValue) {

	if (!appInsights || !appInsights.defaultClient) {
		return;
	}

	appInsights.defaultClient.trackEvent({ name: eventName, properties: { customProperty: eventCustomValue } });
}

function appInsightsLogMetric(metricName, metricValue) {

	if (!appInsights || !appInsights.defaultClient) {
		return;
	}

	appInsights.defaultClient.trackMetric({ name: metricName, value: metricValue });
}

var lastVMSSCapacity = 0;
var lastVMSSProvisioningState = "";

// This goes out to Azure and grabs the current VMSS provisioning state and current capacity
function getVMSSNodeCountAndState(subscriptionId, resourceGroup, virtualMachineScaleSet) {

	const options = {
		resource: 'https://management.azure.com'
	}

	// Use an Azure system managed identity to get a token for managing the given resource group
	msRestNodeAuth.loginWithVmMSI(options).then((creds) => {
		const client = new ComputeManagementClient(creds, subscriptionId);
		var vmss = new VirtualMachineScaleSets(client);

		// Get the latest details about the VMSS in Azure
		vmss.get(resourceGroup, virtualMachineScaleSet).then((result) => {
			if (result == null || result.sku == null) {
				console.error(`ERROR getting VMSS sku info`);
				return;
			}

			// Set our global variables so we know the totaly capacity and VMSS status
			currentVMSSNodeCount = result.sku.capacity;
			currentVMSSProvisioningState = result.provisioningState;

			// Only log if it changed
			if (currentVMSSNodeCount != lastVMSSCapacity || currentVMSSProvisioningState != lastVMSSProvisioningState) {
				console.log(`VMSS Capacity: ${currentVMSSNodeCount} and State: ${currentVMSSProvisioningState}`);
			}

			lastVMSSCapacity = currentVMSSNodeCount;
			lastVMSSProvisioningState = currentVMSSProvisioningState;
			appInsightsLogMetric("VMSSGetSuccess", 1);
		}).catch((err) => {
			console.error(`ERROR getting VMSS info: ${err}`);
			appInsightsLogError(err);
			appInsightsLogMetric("VMSSGetError", 1);
		});
	}).catch((err) => {
		console.error(err);
		appInsightsLogError(err);
		appInsightsLogMetric("MSILoginGetError", 1);
	});
}

// This returnes the amount of connected clients
function getConnectedClients(cirrusServers) {

	var connectedClients = 0;

	for (cirrusServer of cirrusServers.values()) {
		// we are interested in the amount of cirrusServers that have 1 or more players connected to them. We are not interested in the amount of players
		connectedClients += (cirrusServer.numConnectedClients > 1 ? 1 : cirrusServer.numConnectedClients);

		if (cirrusServer.numConnectedClients > 1) {
			console.log(`WARNING: cirrusServer ${cirrusServer.address} has ${cirrusServer.numConnectedClients}`);
        }
	}

	console.log(`Total Connected Clients Found: ${connectedClients}`);
	return connectedClients;
}

// This scales out the Azure VMSS servers with a new capacity
function scaleSignalingWebServers(newCapacity) {

	const options = {
		resource: 'https://management.azure.com'
	}

	//msRestNodeAuth.interactiveLogin().then((creds) => {  // Used for local testing
	// Use an Azure system managed identity to get a token for managing the given resource group
	msRestNodeAuth.loginWithVmMSI(options).then((creds) => {
		const client = new ComputeManagementClient(creds, config.subscriptionId);
		var vmss = new VirtualMachineScaleSets(client);

		var updateOptions = new Object();
		updateOptions.sku = new Object();
		updateOptions.sku.capacity = newCapacity;

		// Update the VMSS with the new capacity
		vmss.update(config.resourceGroup, config.virtualMachineScaleSet, updateOptions).then((result) => {
			console.log(`Success Scaling VMSS: ${result}`);
			appInsightsLogMetric("VMSSScaleSuccess", 1);
		}).catch((err) => {
			console.error(`ERROR Scaling VMSS: ${err}`);
			appInsightsLogError(err);
			appInsightsLogMetric("VMSSScaleUpdateError", 1);
		});
	}).catch((err) => {
		console.error(err);
		appInsightsLogError(err);
		appInsightsLogMetric("MSILoginError", 1);
	});
}

// This scales up a VMSS cluster for Unreal streams to a new node count
function scaleupInstances(newNodeCount) {
	// Make sure we don't try to scale past our desired max instances
	if(newNodeCount > config.maxVmssInstanceScaleCount) {
		console.log(`New Node Count is higher than Max Node Count. Setting New Node Count to Max.`);
		newNodeCount = config.maxVmssInstanceScaleCount;
	}

	appInsightsLogEvent("ScaleUp", newNodeCount);

	lastScaleupTime = Date.now();

	scaleSignalingWebServers(newNodeCount);
}

// This scales down a VMSS cluster for Unreal streams to a new node count
function scaledownInstances(newNodeCount) {
	console.log(`Scaling down to ${newNodeCount}!!!`);
	lastScaledownTime = Date.now();

	// If set, make sure we don't try to scale below our desired min node count
	if ((config.minIdleVmssInstanceCount > 0) && (newNodeCount < config.minIdleVmssInstanceCount)) {
		console.log(`Using minIdleVmssInstanceCount to scale down: ${config.minIdleVmssInstanceCount}`);
		newNodeCount = config.minIdleVmssInstanceCount;
	}

	// Mode sure we keep at least 1 node
	if (newNodeCount <= 0)
		newNodeCount = 1;

	appInsightsLogEvent("ScaleDown", newNodeCount);

	scaleSignalingWebServers(newNodeCount);
}

// Called when we want to review the autoscale policy to see if there needs to be scaling up or down
function evaluateAutoScalePolicy(cirrusServers) {
	getVMSSNodeCountAndState(config.subscriptionId, config.resourceGroup, config.virtualMachineScaleSet);
	totalInstances = cirrusServers.size;
	totalConnectedClients = getConnectedClients(cirrusServers);

	console.log(`Current VMSS count: ${currentVMSSNodeCount} - Current Cirrus Servers Connected: ${totalInstances} - Current Cirrus Servers with clients: ${totalConnectedClients}`);
	appInsightsLogMetric("TotalInstances", totalInstances);
	appInsightsLogMetric("TotalConnectedClients", totalConnectedClients);

	var availableConnections = Math.max(totalInstances - totalConnectedClients, 0);

	var timeElapsedSinceScaleup = Date.now() - lastScaleupTime;
	var minutesSinceScaleup = Math.round(((timeElapsedSinceScaleup % 86400000) % 3600000) / 60000);

	var timeElapsedSinceScaledown = Date.now() - lastScaledownTime;
	var minutesSinceScaledown = Math.round(((timeElapsedSinceScaledown % 86400000) % 3600000) / 60000);
	var percentUtilized = 0;
	var remainingUtilization = 100;

	// Get the percentage of total available signaling servers taken by users
	if (totalConnectedClients > 0 && totalInstances > 0) {
		percentUtilized = (totalConnectedClients / totalInstances) * 100;
		remainingUtilization = 100 - percentUtilized;
	}

	//console.log(`Minutes since last scaleup: ${minutesSinceScaleup} and scaledown: ${minutesSinceScaledown} and availConnections: ${availableConnections} and % used: ${percentUtilized}`);
	appInsightsLogMetric("PercentUtilized", percentUtilized);
	appInsightsLogMetric("AvailableConnections", availableConnections);

	// Don't try and scale up/down if there is already a scaling operation in progress
	if (currentVMSSProvisioningState != 'Succeeded') {
		console.log(`Ignoring scale check as VMSS provisioning state isn't in Succeeded state: ${currentVMSSProvisioningState}`);
		appInsightsLogMetric("VMSSProvisioningStateNotReady", 1);
		appInsightsLogEvent("VMSSNotReady", currentVMSSProvisioningState);
		return;
	}

	// Make sure all the cirrus servers on the VMSS have caught up and connected to the MM before considering scaling, or at least 15 minutes since starting up 
	if ((totalInstances/config.instancesPerNode) < currentVMSSNodeCount && minutesSinceScaleup < 15) {
		console.log(`Ignoring scale check as only ${totalInstances/config.instancesPerNode} VMSS nodes out of ${currentVMSSNodeCount} total VMSS nodes have connected`);
		appInsightsLogMetric("CirrusServersNotAllReady", 1);
		appInsightsLogEvent("CirrusServersNotAllReady", currentVMSSNodeCount - totalInstances);
		return;
    }

	// Adding hysteresis check to make sure we didn't just scale up and should wait until the scaling has enough time to react
	//if (minutessincescaleup < minminutesbetweenscaleups) {
	//	console.log(`waiting to scale since we already recently scaled up or started the service`);
	//	return;
	//}

	console.log('----------------------------------');
	for (cirrusServer of cirrusServers.values()) {
		console.log(`${cirrusServer.address}:${cirrusServer.port} - ${cirrusServer.ready === true ? "": "not "}ready`);	
	}
	console.log('minutesSinceScaledown:       '+minutesSinceScaledown);
	console.log('minMinutesBetweenScaledowns: '+minMinutesBetweenScaledowns);
	console.log('config.connectionIdleRatio:  '+config.connectionIdleRatio);
	console.log('config.idleMinutes:          '+config.idleMinutes);
	console.log('percentUtilized:             '+percentUtilized);
	console.log('config.instanceCountBuffer:  '+percentUtilized);
	console.log('availableConnections:        '+availableConnections);
	console.log('totalConnectedClients:       '+totalConnectedClients);
	console.log('currentVMSSNodeCount:        '+currentVMSSNodeCount);
	console.log('----------------------------------');

	// If available user connections is less than our desired buffer level scale up
	if ((config.instanceCountBuffer > 0) && (availableConnections < config.instanceCountBuffer)) {
		var newNodeCount = Math.ceil((config.instanceCountBuffer + totalConnectedClients)/config.instancesPerNode);
		console.log(`Not enough available connections in buffer -- scale up from ${currentVMSSNodeCount} to ${newNodeCount}`);
		appInsightsLogMetric("VMSSNodeCountScaleUp", 1);
		appInsightsLogEvent("Scaling up VMSS node count", availableConnections);
		scaleupInstances(newNodeCount);
		return;
	}
	// Else if the remaining utilization percent is less than our desired min percentage. scale up 10% of total instances
	else if ((config.percentBuffer > 0) && (remainingUtilization < config.percentBuffer)) {
		var newNodeCount = Math.ceil((totalInstances * (1+(scaleUpPercentage * .01)))/config.instancesPerNode);
		console.log(`Below percent buffer -- scaling up from ${currentVMSSNodeCount} to ${newNodeCount}`);
		appInsightsLogMetric("VMSSPercentageScaleUp", 1);
		appInsightsLogEvent("Scaling up VMSS percentage", newNodeCount);
		scaleupInstances(newNodeCount);
		return;
	}
	// Else if our current VMSS nodes are less than the desired node count buffer (i.e., we started with 2 VMSS but we wanted a buffer of 5)
	else if ((config.instanceCountBuffer > 0) && ((currentVMSSNodeCount*config.instancesPerNode) < config.instanceCountBuffer)) {
		var newNodeCount = Math.ceil(config.instanceCountBuffer / config.instancesPerNode);
		console.log(`Requested buffer is higher than available instance count -- scale up from ${currentVMSSNodeCount} to ${newNodeCount}`);
		appInsightsLogMetric("VMSSDesiredBufferScaleUp", 1);
		appInsightsLogEvent("Scaling up VMSS to meet initial desired buffer", newNodeCount);
		scaleupInstances(currentVMSSNodeCount + newNodeCount);
		return;
	}

	// Adding hysteresis check to make sure we didn't just scale down and should wait until the scaling has enough time to react
	if (minutesSinceScaledown < minMinutesBetweenScaledowns) {
		console.log(`Waiting to evaluate scale down since we already recently scaled down or started the service`);
		appInsightsLogEvent("Waiting to scale down due to recent scale down", minutesSinceScaledown);
		return;
	}
	// Else if we've went long enough without scaling up to consider scaling down when we reach a low enough usage ratio
	else {
		var newNodeCount = currentVMSSNodeCount;
		var scalingType = "";
		if(config.instanceCountBuffer > 0) {
			var minimumNodeCount = Math.ceil(config.instanceCountBuffer / config.instancesPerNode);
			newNodeCount = Math.ceil((totalInstances-availableConnections+config.instanceCountBuffer)/config.instancesPerNode);
			scalingType = "InstanceCountBuffer";
		}
		else if(config.percentBuffer > 0) {
			var minimumNodeCount = Math.ceil((1*(1+(config.percentBuffer*.01)))/config.instancesPerNode); // in case of buffer sizes > 100%
			newNodeCount = Math.ceil(((totalInstances-availableConnections)*(1+(config.percentBuffer*0.1)))/config.instancesPerNode);
			scalingType = "PercentBuffer";
		}

		newNodeCount = Math.max(newNodeCount, minimumNodeCount);

		if(newNodeCount != currentVMSSNodeCount)
		{
			console.log(`Scaling down for scale config ${scalingType}: Current node count: ${currentVMSSNodeCount} - Minimum node count: ${minimumNodeCount} - New node count: ${newNodeCount}`);
			appInsightsLogMetric("VMSSScaleDown", 1);
			appInsightsLogEvent("Scaling down VMSS due to idling", percentUtilized + "%, count:" + newNodeCount);
			scaledownInstances(newNodeCount);
		}
	}
}

function checkIfNodesAreStillResponsive(cirrusServers) {
	var threshold = 35;
	var brokenServers = [];
	for (cirrusServer of cirrusServers.values()) {
		console.log(Math.round((Date.now() - cirrusServer.lastPingReceived)/1000));
		if(Date.now() - cirrusServer.lastPingReceived > threshold*1000)
		{
			let server = [...cirrusServers.entries()].find(([key, val]) => val.address === cirrusServer.address && val.port === cirrusServer.port);
			brokenServers.push(server[0]);
		}
	}
	if(brokenServers.length > 0)
	{
		console.log(`${brokenServers.length} servers have been found that have not pinged the MM in the last ${threshold} seconds. Ending connection and removing from list.`);
		for(var i=0; i<brokenServers.length; i++) {
			var conn = brokenServers[i];
			var cirrusServer = cirrusServers.get(conn);
			cirrusServers.delete(conn);
			conn.end();
		}
	}
}

module.exports = {
	init: initAzureModule,
	appInsightsLogError,
	appInsightsLogEvent,
	appInsightsLogMetric,
	getVMSSNodeCountAndState,
	evaluateAutoScalePolicy,
	scaledownInstances,
	scaleupInstances,
	scaleSignalingWebServers,
	getConnectedClients,
	checkIfNodesAreStillResponsive
}