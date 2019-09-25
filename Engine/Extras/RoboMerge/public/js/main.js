// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
"use strict"

function promptFor(map) {
	var result = {};
	var success = true;
	$.each(Object.keys(map), function(_, key) {
		if (success) {
			var p = map[key];
			var dflt = "";
			if (typeof(p) === "object") {
				dflt = p.default || "";
				p = p.prompt || key;
			}

			var data = prompt(p, dflt);
			if (data === null) {
				success = false;
			}
			else {
				result[key] = data;
			}
		}
	});
	return success ? result : null;
}

class BotPips {
	constructor() {
		this.blocked = 0;
		this.paused = 0;
		this.$blocked = null;
		this.$paused = null;
	}

	createElements($container) {
		const elem = $('<span class="label">').css('display','none').css('position', 'relative').css('bottom','2px');
		this.$blocked = elem.clone().addClass('label-danger').appendTo($container);
		this.$paused = elem.clone().addClass('label-warning').appendTo($container);
	}

	show() {
		if (this.paused !== 0) {
			this.$paused.css('display', '').text(this.paused);
		}

		if (this.blocked !== 0) {
			this.$blocked.css('display', '').text(this.blocked);
		}
	}
}

let branchGraphs = new Map();
function renderBranchList(branches) {
	branches.sort(function(a,b) {
		var comp = a.bot.localeCompare(b.bot);
		if (comp !== 0)
			return comp;
		return a.def.upperName.localeCompare(b.def.upperName);
	});

	// Create main div and a list that will serve as our naviagation bar
	var mainDiv = $('<div>').css("margin", "30px");
	var navBar = $('<ul class="nav nav-tabs">').appendTo(mainDiv);

	// Create div to hold all our tabs (the navBar will toggle between them for us)
	var tabHolder = $('<div>').appendTo(mainDiv);
	

	// Go through each branchbot and display table data
	var botPips, table, tbody, lastbot // Since we get data by branchbot (Main, Devs, etc.) and not by Robomerge bot (Fortnite, Engine, etc.), 
									   //we'll need to keep track of which bot we're on for table rendering
	$.each(branches, function(_, branch) {
		// If this is the first entry for the bot, create a new tab, table and graph
		if (branch.bot !== lastbot) {
			// Create tab for branchbot
			var branchbotTab = $('<li role="presentation">').appendTo(navBar);
			// Pips control the display of the number of paused/blocked branches
			botPips = new BotPips;
			var branchLink = $('<a class="botlink">')
			.attr('id', "link-" + branch.bot.toUpperCase())
			.attr('href', '#' + branch.bot)
			.appendTo(branchbotTab)
			.append(
				$('<b>').text(branch.bot)).click(function() {
					navBar.children('li').each(function(_, tab) {
						$(tab).removeClass('active');
					});
					tabHolder.children().detach();
					tabHolder.append(branchRoot);
					branchbotTab.addClass("active");
				}
			);
			branchLink.append(" ");
			botPips.createElements(branchLink);

			// Create table and headers
			var branchRoot = $('<div>');
			table = $('<table class="table table-striped" style="table-layout:fixed" cellpadding="0" cellspacing="0">').appendTo(branchRoot);
			table.append(createBranchMapHeaders(true))

			// Create Table Body
			tbody = $('<tbody>').appendTo(table);

			// Finally, append branch graph to global branchGraphs Map
			var graph = $('<div>').appendTo(branchRoot);
			var graphObj = branchGraphs.get(branch.bot);
			const branchSpecCl = branch.branch_spec_cl;
			if (!graphObj || graphObj.cl !== branchSpecCl) {
				graphObj = {elem: showFlowGraph(branches, branch.bot), cl: branchSpecCl};
				branchGraphs.set(branch.bot, graphObj);
			}
			graph.append(graphObj.elem);

			// Ensure we keep track of what bot we're on
			lastbot = branch.bot
		}
		
		// Append branch to table (with actions column!)
		table.append(appendBranchMapRow(branch, tbody, true))

		// up the pip count
		if (branch.is_paused) {
			if (isBranchManuallyPaused(branch)) {
				botPips.paused++;
			}
			else {
				botPips.blocked++;
			}
		}
		botPips.show();
	});

	return mainDiv;
}

function setVerbose(enabled) {
	clearErrorText();
	$.ajax({
		url: '/api/control/verbose/'+(enabled?"on":"off"),
		type: 'post',
		success: updateBranchList,
		error: function(xhr, error, status) {
			setError(xhr, error);
			callback(null);
		}
	});
}

function stopBot() {
	if (confirm("Are you sure you want to stop RoboMerge?")) {
		clearErrorText();
		$.ajax({
			url: '/api/control/stop',
			type: 'post',
			success: updateBranchList,
			error: function(xhr, error, status) {
				setError(xhr, error);
				callback(null);
			}
		});
	}
}
function startBot() {
	clearErrorText();
	$.ajax({
		url: '/api/control/start',
		type: 'post',
		success: updateBranchList,
		error: function(xhr, error, status) {
			setError(xhr, error);
			callback(null);
		}
	});
}

const escapeHtml = str => str
	.replace(/&/g, '&amp;')
	.replace(/</g, '&lt;')
	.replace(/>/g, '&gt;')
	.replace(/"/g, '&quot;')
	.replace(/'/g, '&#039;');