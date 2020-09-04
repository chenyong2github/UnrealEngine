// Copyright Epic Games, Inc. All Rights Reserved.
"use strict";

const EDGE_STYLES = {
	roboshelf: [['color', 'purple'], ['arrowhead', 'diamond']],
	forced: [],
	defaultFlow: [['color', 'blue']],
	onRequest: [['color', 'darkgray'], ['style', 'dashed']],
	blockAssets: [['color', 'darkgray'], ['style', 'dashed'], ['arrowhead', 'odiamond']]
};

function renderGraph(src) {
	var hpccWasm = window["@hpcc-js/wasm"];
	return hpccWasm.graphviz.layout(src, "svg", "dot");
}

function showFlowGraph(data, botName) {

	const addLinesForBranchGraph = (outLines, bot, branchList) => {
		const aliases = new Map;
		const nodeLabels = [];
		const getId = alias => {
			const info = aliases.get(alias);
			return info ? info.id : alias;
		};

		// build map of all aliases and list of node information
		for (let branch of branchList) {
			let id = `_${bot}_${branch.upperName.replace(/[^\w]+/g, '_')}`;
			let tooltip = branch.rootPath;
			if (branch.aliases && branch.aliases.length !== 0) {
				tooltip += ` (${branch.aliases.join(', ')})`;
			}
			const info = { id: id, tooltip: tooltip, name: branch.name };
			if (branch.config.graphNodeColor) {
				info.graphNodeColor = branch.config.graphNodeColor;
			}
			// note: branch.upperName is always in branch.aliases
			for (const alias of branch.aliases) {
				aliases.set(alias, info);
			}

			nodeLabels.push(info);
		}

		const branchesById = new Map;
		for (const branch of branchList) {
			const branchId = getId(branch.upperName);
			branchesById.set(branchId, {
				id: branchId,
				forcedDests: new Set(branch.forceFlowTo.map(getId)),
				defaultDests: new Set(branch.defaultFlow.map(getId)),
				blockAssetDests: new Set(branch.blockAssetTargets ? branch.blockAssetTargets.map(getId) : [])
			});
		}

		const linkOutward = new Set, linkInward = new Set;
		const nodeImportance = new Map;

		// special case branches named Main
		nodeImportance.set(getId("MAIN"), 10);

		const links = [];
		for (const roboBranch of branchList) {
			const branch = branchesById.get(getId(roboBranch.upperName));

			for (let flow of roboBranch.flowsTo) {
				let src = branch.id;
				let dst = getId(flow);
				nodeImportance.set(src, (nodeImportance.get(src) || 0) + 1);
				nodeImportance.set(dst, (nodeImportance.get(dst) || 0) + 1);

				const isForced = branch.forcedDests.has(dst);
				const edgeStyle =
					roboBranch.convertIntegratesToEdits ?	'roboshelf' :
					isForced ?								'forced' :
					branch.defaultDests.has(dst) ?			'defaultFlow' :
					branch.blockAssetDests.has(dst) ?		'blockAssets' : 'onRequest';


				const styles = [...EDGE_STYLES[edgeStyle]];
				const link = { src, dst, styles };
				links.push(link);
				if (isForced) {
					linkOutward.add(src + dst);
				}
				else if (edgeStyle === 'blockAssets' || edgeStyle === 'onRequest') {
					linkInward.add(dst + src);
				}
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

		for (const info of nodeInfo) {
			const attrs = [
				['label', `"${info.info.name}"`],
				['tooltip', `"${info.info.tooltip}"`],
				['margin', `"${info.marginX},${info.marginY}"`],
				['fontsize', info.fontSize],
			];

			if (info.importance > .5) {
				attrs.push(['style', '"filled,bold"']);
			}
			if (info.info.graphNodeColor) {
				attrs.push(['fillcolor', `"${info.info.graphNodeColor}"`]);
			}

			const attrStrs = attrs.map(([key, value]) => `${key}=${value}`);
			outLines.push(`${info.info.id} [${attrStrs.join(', ')}];`);
		}

		links.reverse();
		for (const link of links) {
			const combo = link.dst + link.src;

			if (combo && linkOutward.has(combo) && linkInward.has(combo)) {
				link.styles.push(['constraint', 'false']);
			}
			const styleStrs = link.styles.map(([key, value]) => `${key}=${value}`);
			const suffix = styleStrs.length === 0 ? '' : ` [${styleStrs.join(', ')}]`;
			outLines.push(`${link.src} -> ${link.dst}${suffix};`);
		}
	};


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
			lines.push(`node [shape=box, style=filled, fontname="sans-serif", fillcolor=moccasin];`);

			addLinesForBranchGraph(lines, bot, branchList);
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
				.appendTo(graphContainer)

			$('#graph-loading-text').hide();
			let span = $('<span>').css('margin','auto').html(graphSvg);
			let svgEl = $('svg', span).addClass('branch-graph').removeAttr('width');
			// scale graph to 70% of default size
			let height = Math.round(parseInt(svgEl.attr('height')) * .7);
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
			renderGraph(src).then(graphSvg => present(bot, graphSvg));
		}
	};

	present();
	return graphContainer;
};
