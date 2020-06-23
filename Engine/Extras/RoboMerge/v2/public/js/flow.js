// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

function showFlowGraph(data, botName) {
	let renderGraph = (function() {
		let w = new Worker('/js/graphviz-worker.js');

		let promise;
		w.onmessage = event => {
			if (event.data[0] === 'error')
				promise.reject(event.data[1]);
			else
				promise.resolve(event.data[1]);
		};
		return src => {
			promise = $.Deferred();
			w.postMessage(src);
			return promise;
		};
	})();

	const addLinesForBranchMap = (outLines, bot, branchList) => {
		const aliases = new Map, nodeImportance = new Map;
		const nodeLabels = [];
		const getId = alias => {
			const info = aliases.get(alias);
			return info ? info.id : alias;
		};

		// build map of all aliases and list of node information
		for (let branch of branchList) {
			let id = `${bot}_${branch.upperName.replace(/[^\w]+/g, '_')}`;
			let tooltip = branch.rootPath;
			if (branch.aliases && branch.aliases.length !== 0) {
				tooltip += ` (${branch.aliases.join(', ')})`;
			}
			let info = {id: id, tooltip: tooltip, name: branch.name};
			for (const alias of branch.aliases)
				aliases.set(alias, info);
			nodeLabels.push(info);
		}

		// special case branches named Main
		nodeImportance.set(getId("MAIN"), 10);

		let links = [];
		for (let branch of branchList) {
			let forcedDests = new Set(branch.forceFlowTo.map(getId));
			let defaultDests = new Set(branch.defaultFlow.map(getId));
			let blockAssetDests = new Set(branch.blockAssetTargets.map(getId));

			for (let flow of branch.flowsTo) {
				let src = getId(branch.upperName);
				let dst = getId(flow);
				nodeImportance.set(src, (nodeImportance.get(src) || 0) + 1);
				nodeImportance.set(dst, (nodeImportance.get(dst) || 0) + 1);

				let edgeStyles =
					branch.convertIntegratesToEdits ?	'color=purple, arrowhead=diamond':
					forcedDests.has(dst) ?				'' :
					defaultDests.has(dst) ?				'color=blue' :
					blockAssetDests.has(dst) ?			'color=darkgray, style=dashed, arrowhead=odiamond' :
														'color=darkgray, style=dashed';

				links.push([src, dst, edgeStyles ? ` [${edgeStyles}]` : '']);
			}
		}

		let nodeInfo = [];
		for (const info of nodeLabels) {
			const {id, tooltip, name} = info;
			let factor = (Math.min(nodeImportance.get(id) || 0, 10) - 1) / 9;
			nodeInfo.push({
				importance: factor,
				marginX: (.2 * (1 - factor) + .4 * factor).toPrecision(1),
				marginY: (.1 * (1 - factor) + .25 * factor).toPrecision(1),
				fontSize: (14 * (1 - factor) + 20 * factor).toPrecision(2),
				info: info
			});
		}
		// sort by reverse order of importance
		nodeInfo.sort((lhs, rhs) => rhs.importance - lhs.importance);
		for (let info of nodeInfo) {
			outLines.push(`${info.info.id} [label="${info.info.name}", ` + `tooltip="${info.info.tooltip}", margin="${info.marginX, info.marginY}", ` +
				`fontsize=${info.fontSize}${info.importance > .5 ? ', style="filled,bold"' : ''}];`);
		}
		for (let link of links) {
			outLines.push(`${link[0]} -> ${link[1]}${link[2]};`);
		}
	};

	let colors = ['moccasin', 'plum', 'lightcyan', 'darksalmon', 'gold', 'skyblue',
				'coral', 'khaki', 'yellowgreen', 'thistle', 'peachpuff', 'honeydew'];
	let colorIndex = 0;

	let bots = new Map;
	for (let branch of data) {
		if (botName && branch.bot !== botName)
			continue;
		let branchList = bots.get(branch.bot);
		if (!branchList) {
			branchList = [];
			bots.set(branch.bot, branchList);
		}
		branchList.push(branch.def);
	}

	let graphSrcIter = (function*() {
		let preface = [
			'digraph robomerge {',
			'fontname="sans-serif"; labelloc=top; fontsize=16;',
			'edge [penwidth=2]; nodesep=.7; ranksep=1.2;',
		];

		for (let [bot, branchList] of bots) {
			let lines = preface.slice();
			lines.push(`label = "${bot}";`);
			lines.push(`tooltip="${bot} integration paths";`);
			lines.push(`node [shape=box, style=filled, fontname="sans-serif", fillcolor=${colors[colorIndex++ % colors.length]}];`);

			addLinesForBranchMap(lines, bot, branchList);
			lines.push('}');
			yield [bot, lines.join('\n')];
		}
	})();

	let graphContainer = $('<div class="clearfix">');
	let flowGraph = $('<div class="flow-graph" style="display:inline-block;">').appendTo(graphContainer);
	let first = true;
	flowGraph.append($('<div>').css('text-align', 'center').text("Building graph..."));
	let present = function(bot, graphSvg) {
		if (graphSvg) {
			$('#graph-key-template')
				.clone()
				.removeAttr('id')
				.css('display', 'inline-block')
				.appendTo(graphContainer);

			$('#graph-loading-text').hide();
			let span = $('<span>').css('margin','auto').html(graphSvg);
			let svgEl = $('svg', span).addClass('branch-graph').removeAttr('width');
			// scale graph to 70% of default size
			let height = parseInt(parseInt(svgEl.attr('height')) * .7);
			svgEl.attr('height', height + 'px').css('vertical-align', 'top');

			let inserted = false;
			for (let existing of $('svg', flowGraph)) {
				let el = $(existing);
				if (parseInt(el.attr('height')) < height) {
					span.insertBefore(el.parent());
					inserted = true;
					break;
				}
			}
			if (first) {
				first = false;
				flowGraph.empty();
			}
			if (!inserted)
				flowGraph.append(span);
		}

		let {value, done} = graphSrcIter.next();
		if (!done) {
			let [bot, src] = value;
			console.log(src);
			renderGraph(src).done(function(graphSvg) {
				present(bot, graphSvg);
			});
		}
	};

	present();
	return graphContainer;
};
