// Copyright Epic Games, Inc. All Rights Reserved.
// boilerplate.js -- This script should be loaded for every Robomerge page to provide a consistent UI and dataset for each page,
//                   as well as provide common, shared functions to our client-side Javascript.


/********************
 * COMMON FUNCTIONS *
 ********************/

// Common mutex variable around displaying error messages
let clearer = null
function clearErrorText() {
	const $err = $('#error')
	$err.addClass('_hidden');
	if (!clearer) {
		clearer = setTimeout(() => {
			if (clearer) {
				$err.empty();
			}
		}, 1000);
	}
}
function setErrorText(text) {
	if (clearer) {
		clearTimeout(clearer);
		clearer = null;
	}
	$('#error').text(text).removeClass('_hidden');
}
function setError(xhr, error) {
	if (xhr.responseText)
		setErrorText(xhr.responseText);
	else if (xhr.status == 0) {
		setErrorText("Connection error");
		document.title = "ROBOMERGE (error)";
	}
	else
		setErrorText("HTTP "+xhr.status+": "+error);
}

// Add way to prompt the user on leave/reload before the perform an action
function addOnBeforeUnload(message) {
	$( window ).on("beforeunload", function() {
		return message
	 })
}
function removeOnBeforeUnload() {
	$( window ).off("beforeunload")
}

// Reload the branchlist div
let updateBranchesPending = false;
function updateBranchList(botlinkhash) {
	if (updateBranchesPending)
		return;
	console.log("Updating Branch List...")
	updateBranchesPending = true;
	getBranchList(function(data) {
		updateBranchesPending = false;
		var bl = $('#branchList').empty();
		if (data.started) {
			bl.append(renderBranchList(data.branches));
		} else {
			bl.append($('<button>').addClass('btn btn-xs').text("View Logs").click(function() {
				window.location.href = "/api/last_crash";
			}));
			bl.append($('<button>').addClass('btn btn-xs btn-danger').text("Restart").click(startBot));
		}

		// highlight requested link
		if (botlinkhash) {
			$('#link-' + botlinkhash).click();
			window.location.hash = botlinkhash
		}
		else if (window.location.hash) {
			var linkId = "link-" + window.location.hash.substr(1).toUpperCase();
			$('#' + linkId).click();
		}
		else {
			$($('.botlink')[0]).click();
		}
	});
}

var opPending = false;
function branchOp(botname, branchName, op, callback) {
	if (opPending) {
		alert("Operation already pending");
		return;
	}
	opPending = true;
	clearErrorText();
	$.ajax({
		url: '/api/bot/'+encodeURIComponent(botname)+'/branch/'+encodeURIComponent(branchName)+op,
		type: 'post',
		contentType: 'application/json',
		success: function(data) {
			opPending = false;
			callback(data);
		},
		error: function(xhr, error, status) {
			opPending = false;
			setError(xhr, error);
			callback(null);
		}
	});
}
function toQuery(queryMap) {
	var queryString = "";
	for (var key in queryMap) {
		if (queryString.length > 0) {
			queryString += "&";
		}
		queryString += encodeURIComponent(key) + '=' + encodeURIComponent(queryMap[key]);
	}
	return queryString;
}

function getBranchList(callback) {
	clearErrorText();
	$.ajax({
		url: '/api/branches',
		type: 'get',
		dataType: 'json',
		success: callback,
		error: function(xhr, error, status) {
			setError(xhr, error);
			callback(null);
		}
	});
}

function getBranch(botname, branchname, callback) {
	clearErrorText();
	$.ajax({
		url: `/api/bot/${botname}/branch/${branchname}`,
		type: 'get',
		dataType: 'json',
		success: callback,
		error: function(xhr, error, status) {
			setError(xhr, error);
			callback(null);
		}
	});
}

function isBranchBlocked(branch) {
	return branch.pause_info && branch.pause_info.type && branch.pause_info.type === 'branch-stop'
}

function isBranchManuallyPaused(branch) {
	return branch.pause_info && branch.pause_info.type && branch.pause_info.type === 'manual-lock'
}

function handleUserPermissions(user) {
	if (!user) {
		return;
	}

	let isFTE = false;
	if (user.privileges && Array.isArray(user.privileges)) {
		for (const tag of user.privileges) {
			if (tag === 'fte') {
				isFTE = true;
				break;
			}
		}
	}

	// Fulltime employees should see the log buttons.
	if (isFTE) {
		$('#fteButtons').show();
	}
}

// Adds a message along with bootstrap alert styles
// https://www.w3schools.com/bootstrap/bootstrap_ref_js_alert.asp
function displayMessage(message, alertStyle, closable = true) {
	// Focus user attention
	window.scrollTo(0, 0);

	let messageDiv = $(`<div class="alert ${alertStyle} fade in show" role="alert">`)

	// Add closable button
	if (closable) {
		messageDiv.addClass("alert-dismissible")

		// Display 'X' button
		let button = $('<button type="button" class="close" data-dismiss="alert" aria-label="Close">')
		button.html('&times;')
		messageDiv.append(button)

		// Fade out after 10 seconds
		setTimeout(function() {
			messageDiv.slideUp().alert('close')
		}, 10000)
	}

	messageDiv.append(message)

	$('#status_message').append(messageDiv)
}
// Helper functions wrapping displayMessage()
function displaySuccessfulMessage(message, closable = true) {
	displayMessage(`<strong>Success!</strong> ${message}`, "alert-success", closable)
}
function displayInfoMessage(message, closable = true) {
	displayMessage(`${message}`, "alert-info", closable)
}
function displayWarningMessage(message, closable = true) {
	displayMessage(`<strong>Warning:</strong> ${message}`, "alert-warning", closable)
}
function displayErrorMessage(message, closable = true) {
	displayMessage(`<strong>Error:</strong> ${message}`, "alert-danger", closable)
}

// This function takes string arrays of key names (requiredKeys, optionalKeys) and attempts to get them from the query parameters.
// If successful, returns an object of { key: value, ... }
// If failure, displays an error message and returns null
function processQueryParameters(requiredKeys, optionalKeys) {
	var urlParams = new URLSearchParams(window.location.search);
	var returnObject = {}

	requiredKeys.forEach(keyName => {
		// These should have been vetted by roboserver.ts prior to getting here, but shouldn't assume
		if (!urlParams.has(keyName)) {
			displayErrorMessage(`Query parameter '${keyName}' required for operation. Aborting...`)
			return null
		} else {
			returnObject[keyName] = urlParams.get(keyName)
		}
	});

	if (optionalKeys) {
		optionalKeys.forEach(keyName => {
			if (urlParams.has(keyName)) {
				returnObject[keyName] = urlParams.get(keyName)
			}
		})
	}

	return returnObject
}

// Creates a consistent UI for our pages
function generateRobomergeHeader() {
    // Create top-right div which contains logged-in user information and Robomerge uptime/version info
	let topright = $('<div id="top-right">')

    let loggedInUser = $('<div id="signed-in-user" hidden>')
    loggedInUser.append('<span><i class="glyphicon glyphicon-user"></i></span>')
	loggedInUser.append('<span class="user-name"></span><span class="tags"></span>')
	
	let logOutButton = $('<button id="log-out" class="btn btn-xs btn-warning">Sign out</button>')
	logOutButton.click(function() {
		document.cookie = 'auth=; path=; redirect_to=;';
		window.location.href = '/login';
	})
    loggedInUser.append(logOutButton)

    topright.append(loggedInUser)
    topright.append('<div id="uptime"></div>')
    topright.append('<div id="version"></div>')
    
    // Show Robomerge logo
	let logo = $('<div id="logo">')
		.append($('<img src="/img/logo.png">'))
		.click(function() {
			// Provide some secret functionality to the homepage
			if (window.location.pathname === "/") {
				if (event.ctrlKey) {
					stopBot();
				} else {
					updateBranchList();
				}

				return false;
			}
		})

    // Empty div for displaying messages to the user
    let statusMessageDiv = $('<div id="status_message">')

	// Create header if one does not exist
	if ($("header").length == 0) {
		$('head').after($('<header>'))
	}
	
	$('header').replaceWith($('<header>').append(topright, [logo, $('<hr>'), statusMessageDiv]))
}
function generateRobomergeFooter() {
	let logHolder = $('<div id="log_holder">')
	let logButtonBar = $('<div class="log-button-bar">')
	logHolder.append(logButtonBar)

	let fteButtonDiv = $('<div id="fteButtons" style="display:inline">')
	fteButtonDiv.hide()
	logButtonBar.append(fteButtonDiv)

	let logButton = $('<button id="logButton">')
	logButton.addClass("btn btn-sm btn-default")
	logButton.attr("onclick", "window.location.href='/api/logs'")
	logButton.text("Logs")
	fteButtonDiv.append(logButton)

	let lastCrashButton = $('<button id="lastCrashButton">')
	lastCrashButton.addClass("btn btn-sm btn-default")
	lastCrashButton.attr("onclick", "window.location.href='/api/last_crash'")
	lastCrashButton.text("Last Crash")
	fteButtonDiv.append(lastCrashButton)

	let p4TasksButton = $('<button id="p4TasksButton">')
	p4TasksButton.addClass("btn btn-sm btn-default")
	p4TasksButton.attr("onclick", "window.location.href='/api/p4tasks'")
	p4TasksButton.text("P4 Tasks")
	fteButtonDiv.append(p4TasksButton)

	let branchesButton = $('<button id="branchesButton">')
	branchesButton.addClass("btn btn-sm btn-default")
	branchesButton.attr("onclick", "window.location.href='/api/branches'")
	branchesButton.text("Branch Data")
	fteButtonDiv.append(branchesButton)

	let rightButtonDiv = $('<div class="log-button-bar-right">')
	logButtonBar.append(rightButtonDiv)
	
	let helpButton = $('<button id="helpButton" style="color:DARKBLUE">')
	helpButton.addClass("btn btn-sm btn-default")
	helpButton.click(function() {
		// Placeholder until I create a help page
		displayInfoMessage(
			'<b>Need Help?</b> Message <tt>@here</tt> in <a href="https://company.slack.com/messages/robomerge-help/">#robomerge-help</a> on Slack.'
		)
	})
	helpButton.html('<strong>Need help?</strong>')
	rightButtonDiv.append(helpButton)
	
	if ($('footer').length == 0) {
		$('html').append($('<footer>'))
	}
	
	$('footer').replaceWith($('<footer>').append(logHolder))
}


// Create a consistent table header scheme for branch map data
function createBranchMapHeaders(includeActions=false) {
	let header = $('<thead>')
	let headerRow = $('<tr>')
	header.append(headerRow)

	headerRow.append($('<th style="width:auto">').text('Branch Name'))
	headerRow.append($('<th style="width:15%">').text('Status'))

	if (includeActions) {
		headerRow.append($('<th style="width:14em">').text('Actions'))
	}

	headerRow.append($('<th style="width:8em">').text('Last Change'))

	return header
}
/*
 * For use in conjunction with createBranchMapHeaders(), render a table row element for a given branchbot and branch.
 * branchData should be the specific branch object from getBranchList() (i.e. def= {...}, bot = "...", etc.)
*/
function appendBranchMapRow(branchData, tbody, includeActions=false) {
	// Create row
	let row = $('<tr>')

	// Branch Name Column 
	row.append(renderBranchNameCell(branchData))
	
	// Status Column 
	row.append(renderStatusCell(branchData))

	if (includeActions) {
		row.append(renderActionsCell(branchData))
	}

	// Last Change Column 
	row.append(renderLastChangeCell(branchData))

	// Append the finished row to the supplied table body
	tbody.append(row)
}
// Helper function to create branch name column
function renderBranchNameCell(branchData) {
	let branchTableData = $('<td>')
	let branchTableDataHeaderDiv = $('<div>').appendTo(branchTableData)

	// Start with the bolded branch name
	let branchName = branchTableDataHeaderDiv.append($('<strong>').text(branchData.def.name))

	// if the branch is active, display status
	if (branchData.status_msg)
	{
		$('<span>')
			.css('font-style', 'italic')
			.css('font-size', '12px')
			.css('margin-left', '20px')
			.text(branchData.status_msg)
			.attr('title', "Since " + new Date(branchData.status_since))
			.appendTo(branchName)
	}

	// Display Reconsidering text if applicable
	if (branchData.queue && branchData.queue.length > 0)
	{
		var queueDiv = $('<div>').appendTo(branchName).append("Reconsidering: ");
		for (let i=0;i<branchData.queue.length;++i)
		{
			$('<span class="label label-primary">').css('margin', '3px').appendTo(queueDiv).text(`CL: ${branchData.queue[i].cl}`);
		}
	}
	
	// Appending the P4 Stream Rootpath floating to the right side
	let rootPath = $('<small style="float: right; font-family: monospace;">').text(branchData.def.rootPath)
	branchTableDataHeaderDiv.append(rootPath)

	// Append info button to branch name
	if (branchData.def.isMonitored) {
		$('<button class="btn btn-xs btn-primary" style="margin:0px 10px"><span class="glyphicon glyphicon-info-sign" aria-hidden="true" style="margin:2px"></span></button>').appendTo(rootPath).click(function() {
			alert(JSON.stringify(branchData, null, '  '));
		});
	}

	// Append Pause Info
	if (branchData.is_paused && branchData.pause_info) {
		// Display pause data if applicable
		const manualLock = isBranchManuallyPaused(branchData)
		const unpauseTime = branchData.pause_info.endsAt ? new Date(branchData.pause_info.endsAt) : 'never';
		const msg = manualLock ? 
			'Paused by a human (please contact this human before unpausing).\n' :
			`Will unpause and retry on: ${unpauseTime}.\n`;
		$('<span>').css('font-style', 'italic').css('font-size', '12px').css('margin-left', '20px').appendTo(branchName).text(msg);

		let preformattedPauseInfo = $('<pre class="pause_info" style="white-space:pre-wrap">').text(branchData.pause_info_str)
		branchTableData.append(preformattedPauseInfo)
	}

	return branchTableData
}
// Helper function to help use format duration status text
function printDurationInfo(stopString, millis) {
	var time = millis / 1000; // starting off in seconds
	if (time < 60) {
		return [`${stopString} less than a minute ago`, 'gray'];
	}
	time = Math.round(time / 60);
	if (time < 120) {
		return [`${stopString} ${time} minute${time < 2 ? '' : 's'} ago`, time < 10 ? 'gray' : time < 20 ? 'orange' : 'red'];
	}

	time = Math.round(time / 60);
	if (time < 36) {
		return [`${stopString} ${time} hours ago`, 'red'];
	}

	return [`${stopString} for more than a day`, 'red'];
}
// Helper function to create status data cell
function renderStatusCell(branchData) {
	// Helper data
	const manualLock = isBranchManuallyPaused(branchData)

	// Create font holder for the stylized status text
	let status = $('<div>')
	let statusText = $('<font>').appendTo(status)
	if (!branchData.def.isMonitored) {
		status.addClass("unmanagedtext")
		status.text('Unmonitored')
		return $('<td>').append(status)
	}
	else if(manualLock) {
		statusText.text('PAUSED')
		statusText.addClass('important_status')
		statusText.attr('color', 'ORANGE')
	}
	// If we are paused and it wasn't manual, it's a blockage.
	else if (branchData.is_paused) {
		statusText.text('BLOCKED')
		statusText.addClass('important_status')
		statusText.attr('color', 'DARKRED')
	}
	else if (branchData.is_active) {
		statusText.text('ACTIVE')
		statusText.attr('color', 'GREEN')
	}
	else {
		statusText.text('RUNNING')
		statusText.attr('color', 'GREEN')
	}

	if (branchData.pause_info && branchData.pause_info.startedAt) {
		var pausedSince = new Date(branchData.pause_info.startedAt);
		var stopString = manualLock ? 'Paused' : 'Blocked';
		var [pausedDurationStr, pausedDurationColor] = printDurationInfo(stopString, Date.now() - pausedSince.getTime());
		var pausedDivs = $('<div>').css({
			color: pausedDurationColor,
			'font-size': '8pt'
		}).text(pausedDurationStr);

		// If we have an acknowledged date, print acknowledged text
		if (branchData.pause_info.acknowledgedAt) {
			var acknowledgedSince = new Date(branchData.pause_info.acknowledgedAt);
			var [ackDurationStr, ackDurationColor] = printDurationInfo("Acknowledged", Date.now() - acknowledgedSince.getTime());
			$('<div>').css({
				color: ackDurationColor,
				'font-size': '8pt'
			}).text(`${ackDurationStr} by `).append($('<strong>').text(branchData.pause_info.acknowledger)).appendTo(pausedDivs);
		} else if (manualLock) {
			$('<div>').css({
				color: "black",
				'font-size': '8pt'
			}).text("Paused by: ").append($('<strong>').text(branchData.pause_info.owner)).appendTo(pausedDivs);
		} else {
			// Determine who is responsible for resolving this.
			const pauseCulprit = branchData.pause_info.owner || branchData.pause_info.author || "unknown culprit";
			$('<div>').css({
				color: "black",
				'font-size': '8pt'
			}).text("Resolver: ").append($('<strong>').text(pauseCulprit + " (unacknowledged)")).appendTo(pausedDivs);
		}

		pausedDivs.appendTo(status);
	}

	return $('<td>').append(status)
}
// Helper function to create action buttons
function createActionButton(buttonGroup, buttonText, onClick, title) {
	let button = $('<button class="btn btn-primary" type="button">').append(buttonText)
	buttonGroup.append(button)

	if (onClick) {
		button.click(onClick)
	}

	if (title) {
		button.attr('title', title)
		button.attr('data-toggle', 'tooltip')
		button.attr('data-placement', 'auto bottom')
		button.tooltip({ delay: {show: 300, hide: 100} })
	}

	return button
}

// Helper function to create action dropdown menu items
function createActionOption(dropdownList, optionText, onClick, title) {
	let option = $('<a href="#">').text(optionText)
	dropdownList.append(
		$('<li>').append(option)
	)

	if (onClick) {
		option.click(onClick)
	}

	if (title) {
		option.attr('title', title)
		option.attr('data-toggle', 'tooltip')
		option.attr('data-placement', 'auto')
		// Show slower, Hide faster -- so users can select other options without waiting for the tooltip to disappear
		option.tooltip({ delay: {show: 500, hide: 0} })
	}

	return option
}
// Helper function to create actions cell
function renderActionsCell(branch) {
	let actionCell = $('<td>')

	// If the branch is not monitored, we can't provide actions. Simply provide text response and return.
	if (!branch.def.isMonitored) {
		actionCell.append($('<div class="unmanagedtext">').text("No Actions Available"))
		return actionCell
	}

	// Start collecting available actions in our button group
	let buttonGroup = $('<div class="btn-group">').appendTo(actionCell)
	// Keep track if we have an individual buttons in our group yet -- this controls how we render the dropdown menu
	let noIndividualButtons = true

	// If the branch is blocked (and not by a human), and it isn't acknowledged yet, expose Acknowledge as a visable button
	if (isBranchBlocked(branch) && !isBranchManuallyPaused(branch)) {

		let ackFunc = function() {
			branchOp(branch.bot, branch.def.name, '/acknowledge', function(success) {
				if (success) {
					displaySuccessfulMessage(`Acknowledged blockage in ${branch.bot}:${branch.def.name}.`)
				} else {
					displayErrorMessage(`Error acknowledging blockage in ${branch.bot}:${branch.def.name}, please check logs.`)
					$('#helpButton').trigger('click')
				}
				updateBranchList(branch.bot.toUpperCase())
			})
		}

		// Acknowledge
		if (!branch.pause_info.acknowledger) {
			createActionButton(
				buttonGroup,
				[$('<span class="glyphicon glyphicon-exclamation-sign" aria-hidden="true" style="color:yellow">'), '&nbsp;', "Acknowledge"],
				ackFunc,
				'Take ownership of this blockage'
			)
		} 
		// Take ownership
		else {
			createActionButton(
				buttonGroup,
				"Take Ownership",
				ackFunc,
				`Take ownership of this blockage over ${branch.pause_info.acknowledger}`
			)
		}
		noIndividualButtons = false
	}
	
	// Add options
	let optionsList = $('<ul class="dropdown-menu">')
	
	// Unpause
	if (isBranchManuallyPaused(branch)) {
		createActionButton(
			buttonGroup,
			'Unpause',
			function() {
				branchOp(branch.bot, branch.def.name, '/unpause', function(success) {
					if (success) {
						displaySuccessfulMessage(`Unpaused ${branch.def.name}.`)
					} else {
						displayErrorMessage(`Error unpausing ${branch.def.name}, please check logs.`)
						$('#helpButton').trigger('click')
					}
					updateBranchList(branch.bot.toUpperCase())
				})
			},
			`Resume Robomerge monitoring of ${branch.def.name}`
		)
		noIndividualButtons = false
	}
	// Resume & Skip
	else if (isBranchBlocked(branch)) {
		// Resume (same as paused, just formatted better)
		createActionOption(optionsList, `Resume ${branch.def.name}`, function() {
			branchOp(branch.bot, branch.def.name, '/unpause', function(success) {
				if (success) {
					displaySuccessfulMessage(`Resuming ${branch.def.name}.`)
				} else {
					displayErrorMessage(`Error resuming ${branch.def.name}, please check logs.`)
					$('#helpButton').trigger('click')
				}
				updateBranchList(branch.bot.toUpperCase())
			})
		},  `Resume Robomerge monitoring of ${branch.def.name}`)

		// Skip CL
		createActionOption(optionsList, `Skip Changelist ${branch.pause_info.change}`, function() {
			// First, confirm they actually want to skip!
			if (!confirm("-=- WARNING -=- ACHTUNG -=- CUIDADO -=- \u{8B66}\u{544A} -=-\n\n" +
				"Skipping a CL may result in dependant changes being unable to merge down.\n\n" +
				"Please only do this if you are asked to do so by an Engineer.")) {
					// They said to cancel
					return
				}

			// Proceed with skip
			const queryData = {
				cl: branch.pause_info.change,
				reason: "manually skipped through Robomerge homepage"
			};
			
			// Perform skip
			branchOp(branch.bot, branch.def.name, '/set_last_cl?' + toQuery(queryData), function(success) {
				if (success) {
					// Now unpause the branch
					branchOp(branch.bot, branch.def.name, '/unpause', function(pSuccess) {
						if (!pSuccess) {
							displayErrorMessage(`Error resuming ${branch.def.name} after skipping CL ${branch.pause_info.change}, please check logs.`)
							$('#helpButton').trigger('click')
						} else {
							displaySuccessfulMessage(`Skipped CL ${branch.pause_info.change} in ${branch.bot}:${branch.def.name}.`)
						}
					})
				} else {
					displayErrorMessage(`Error skipping CL ${branch.pause_info.change} in ${branch.bot}:${branch.def.name}, please check logs.`)
					$('#helpButton').trigger('click')
				}
				updateBranchList(branch.bot.toUpperCase())
			})
		}, `Skip past the blockage caused by changelist ${branch.pause_info.change}.` +
			"This option should only be selected if the work does not need to be merged or you will merge this work youself.")
	} 
	// Pause
	else {
		createActionOption(optionsList, `Pause ${branch.def.name}`, function() {
			var pauseReasonPrompt = promptFor({
				msg: `Why are you pausing ${branch.def.name}?`}
			);
			if (pauseReasonPrompt) {
				pauseReason = pauseReasonPrompt.msg.toString().trim()
				// Request a reason.
				if (pauseReason === "") {
					displayErrorMessage(`Please provide reason for pausing ${branch.bot}:${branch.def.name}. Cancelling pause request.`)
					return
				}

				branchOp(branch.bot, branch.def.name, "/pause?" + toQuery(pauseReasonPrompt), function(success) {
					if (success) {
						displaySuccessfulMessage(`Paused ${branch.def.name}: "${pauseReasonPrompt.msg}"`)
					} else {
						displayErrorMessage(`Error pausing ${branch.def.name}, please check logs.`)
						$('#helpButton').trigger('click')
					}
					updateBranchList(branch.bot.toUpperCase())
				})
			}
		},  `Pause Robomerge monitoring of ${branch.def.name}`)
	}

	// Stomp placeholder
	optionsList.append($('<li class="disabled">').append($('<a href="#">').text("Stomp")))

	// Add seperator
	optionsList.append('<li class="divider">')

	// Add Reconsider action
	createActionOption(optionsList, `Reconsider...`, function() {
		var data = promptFor({
			cl: `Enter the CL to reconsider (should be a CL from ${branch.def.name}):`
		});
		if (data) {
			// Ensure they entered a CL
			if (isNaN(parseInt(data.cl))) {
				displayErrorMessage("Please provide a valid changelist number to reconsider.")
				return
			}

			branchOp(branch.bot, branch.def.name, "/reconsider?" + toQuery(data), function(success) {
				if (success) {
					displaySuccessfulMessage(`Reconsidering ${data.cl} in ${branch.bot}:${branch.def.name}...`)
				} else {
					displayErrorMessage(`Error reconsidering ${data.cl}, please check logs.`)
					$('#helpButton').trigger('click')
				}
				updateBranchList(branch.bot.toUpperCase())
			});
		}
	}, `Manually submit a CL to Robomerge to process (should be a CL from ${branch.def.name})`)

	// Create dropdown menu button
	let actionsDropdownButton = $('<button class="btn dropdown-toggle" type="button" data-toggle="dropdown">')
	if (noIndividualButtons) {
		actionsDropdownButton.text("Actions\n")
		actionsDropdownButton.addClass('btn-default')
	} else {
		actionsDropdownButton.addClass('btn-primary')
	}
	actionsDropdownButton.append(
		$('<span class="caret">')
	)

	// Ensure we don't do a refresh of the branch list while displaying a dropdown
	buttonGroup.on('show.bs.dropdown', function() {
		// Check to see if we have our mutex function
		if (typeof pauseAutoRefresh === 'function') {
			pauseAutoRefresh()
		}
	})
	buttonGroup.on('hide.bs.dropdown', function() {
		// Check to see if we have our mutex function
		if (typeof resumeAutoRefresh === 'function') {
			resumeAutoRefresh()
		}
	})

	// Append to button group
	buttonGroup.append(actionsDropdownButton, [optionsList])

	//$('.dropdown-toggle').dropdown();
	return actionCell
}
// Helper function to create last change cell
function renderLastChangeCell(branchData) {
	let swarmLink = $('<a>')
	swarmLink.attr("href", `https://p4-swarm.companyname.net/changes/${encodeURIComponent(branchData.last_cl)}`)
	swarmLink.text(branchData.last_cl)

	// On shift+click, we can set the CL instead
	swarmLink.click(function(evt) {
		if (evt.shiftKey)
		{
			var data = promptFor({
				cl: {prompt: 'Enter CL', default: branchData.last_cl},
			});
			if (data) {
				data.reason = "manually set through Robomerge homepage"
				branchOp(branchData.bot, branchData.def.name, "/set_last_cl?" + toQuery(data), function(success) {
					if (success) {
						updateBranchList(branchData.bot.toUpperCase())
						displaySuccessfulMessage(`Successfully set ${branchData.bot}:${branchData.def.name} to changelist ${data.cl}`)
					} else {
						displayErrorMessage(`Error setting ${branchData.bot}:${branchData.def.name} to changelist ${data.cl}, please check logs.`)
						$('#helpButton').trigger('click')
					}
				});
			}
			if (evt.preventDefault)
				evt.preventDefault();
			return false;
		}
		return true;
	});

	return $('<td>').append(swarmLink)
}

// Helper functions to wrap createBranchMapHeaders() and appendBranchMapRow() and construct a table
function renderSingleBranchTable(branchData) {
	let table = $('<table style="table-layout:fixed" class="table table-striped" cellpadding="0" cellspacing="0">')
	table.append(createBranchMapHeaders())
	let tbody = $('<tbody>')
	table.append(tbody)
	table.append(appendBranchMapRow(branchData, tbody))
	return table
}

/***********************
 * MAIN EXECUTION CODE *
 ***********************/



// Define some standard header and footer for our pages
generateRobomergeHeader()
generateRobomergeFooter()

// Since we are on the login page, do not show login information
if (window.location.pathname !== "/login") {
	// This call populates user information and uptime statistics UI elements.
	getBranchList(function(data) {
		var uptime = $('#uptime').empty();
		if (data) {
			clearErrorText();
			handleUserPermissions(data.user);

			if (data.started){
				uptime.append($('<b>').text("Running since: ")).append(new Date(data.started).toString());
			}
			else {
				document.title = "ROBOMERGE (stopped)" + document.title
				setErrorText('Not running');
			}

			if (data.user) {
				const $container = $('#signed-in-user').show();
				$('.user-name', $container).text(data.user.displayName);
				$('.tags', $container).text(data.user.privileges && Array.isArray(data.user.privileges) ? ` (${data.user.privileges.join(', ')})` : '');

				if (data.insufficientPrivelege) {
					setErrorText('There are bots running but logged in user does not have admin access');
				}

			}
			else {
				$('#signed-in-user').hide();
			}

			if (data.version) {
				$('#version').text(data.version);
			}
		}
	});
}