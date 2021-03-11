// Copyright Epic Games, Inc. All Rights Reserved.

import { BranchDefForStatus, BranchStatus } from "../../src/robo/status-types"

type Branch = {
	id: string
	forcedDests: Set<string>
	defaultDests: Set<string>
	blockAssetDests: Set<string>
}

type Info = {
	id: string
	tooltip: string
	name: string
	graphNodeColor?: string
}

type NodeInfo = {
	importance: number
	marginX: string; marginY: string; fontSize: string
	info: Info
}

type Link = {
	src: string
	dst: string
	styles: [string, string][]
}

const EDGE_STYLES: {[key: string]: [string, string][]} = {
	roboshelf: [['color', 'purple'], ['arrowhead', 'diamond']],
	forced: [],
	defaultFlow: [['color', 'blue']],
	onRequest: [['color', 'darkgray'], ['style', 'dashed']],
	blockAssets: [['color', 'darkgray'], ['style', 'dashed'], ['arrowhead', 'odiamond']]
}

function setDefault<K, V>(map: Map<K, V>, key: K, def: V): V {
	const val = map.get(key)
	if (val) {
		return val
	}
	map.set(key, def)
	return def
}

function renderGraph(src: string): Promise<any> {
	const hpccWasm = (window as any)["@hpcc-js/wasm"];
	return hpccWasm.graphviz.layout(src, "svg", "dot");

}

function decoratedAlias(bot: string, alias: string) {
	return bot + ':' + alias
}
/** */
class Graph {
	readonly nodeLabels = new Map<string, Info[]>()
	readonly links: Link[] = []

	private readonly aliases = new Map<string, Info>()
	private readonly branchesById = new Map<string, Branch>()

	private readonly linkOutward = new Set<string>()
	private readonly linkInward = new Set<string>()

	private readonly nodeImportance = new Map<string, number>()

	private branchList: BranchStatus[] = []

	constructor(
		private allBranches: BranchStatus[],
		private whitelist: string[] | null = null
	) {

	}

	singleBot(botName: string) {
		this.branchList = this.allBranches.filter(b => b.bot === botName)

		// special case branches named Main
		this.nodeImportance.set(this.getIdForBot(botName)('MAIN'), 10)

		for (const branchStatus of this.branchList) {
			this.addBranch(branchStatus)
		}
		this.createIds()

		for (const branchStatus of this.branchList) {
			const getId = this.getIdForBot(branchStatus.bot)
			for (const flow of branchStatus.def.flowsTo) {
				this.addEdge(branchStatus, getId(flow))
			}
		}

		return this.makeGraph(botName, botName + 'integration paths')
	}

	allBots(whitelist: string[] | null = null) {
		this.branchList = whitelist
			? this.allBranches.filter(b => whitelist.indexOf(b.bot) >= 0)
			: this.allBranches

		this.findSharedNodes()
		for (const branchStatus of this.branchList) {
			this.addBranch(branchStatus, branchStatus.bot)
		}
		this.createIds()

		for (const branchStatus of this.branchList) {
			const getId = this.getIdForBot(branchStatus.bot)
			for (const flow of branchStatus.def.flowsTo) {
				this.addEdge(branchStatus, getId(flow))
			}
		}

		return this.makeGraph('All bots', 'Flow including shared bots')
	}

	private makeGraph(title: string, tooltip: string) {
		const lines: string[] = [
			'digraph robomerge {',
			'fontname="sans-serif"; labelloc=top; fontsize=16;',
			'edge [penwidth=2]; nodesep=.7; ranksep=1.2;',
			`label = "${title}";`,
			`tooltip="${tooltip}";`,
			`node [shape=box, style=filled, fontname="sans-serif", fillcolor=moccasin];`,
		]

		const nodeGroups: [string, NodeInfo[]][] = []
		for (const [groupName, v] of this.nodeLabels) {
			const nodeInfo: NodeInfo[] = []
			nodeGroups.push([groupName, nodeInfo])

			for (const info of v) {
				const {id, tooltip, name} = info
				let factor = (Math.min(this.nodeImportance.get(id) || 0, 10) - 1) / 9
				nodeInfo.push({
					importance: factor,
					marginX: (.2 * (1 - factor) + .4 * factor).toPrecision(1),
					marginY: (.1 * (1 - factor) + .25 * factor).toPrecision(1),
					fontSize: (14 * (1 - factor) + 20 * factor).toPrecision(2),
					info: info
				})
			}
		}

		for (const [groupName, nodeInfo] of nodeGroups) {
			if (groupName !== 'nogroup') {
				lines.push(`subgraph cluster_${groupName} {
	label="${groupName}";
`)
			}

			for (const info of nodeInfo) {
				const attrs: [string, string][] = [
					['label', `"${info.info.name}"`],
					['tooltip', `"${info.info.tooltip}"`],
					['margin', `"${info.marginX},${info.marginY}"`],
					['fontsize', info.fontSize],
				]

				if (info.importance > .5) {
					attrs.push(['style', '"filled,bold"'])
				}

				if (info.info.graphNodeColor) {
					attrs.push(['fillcolor', `"${info.info.graphNodeColor}"`])
				}

				const attrStrs = attrs.map(([key, value]) => `${key}=${value}`)
				lines.push(`${info.info.id} [${attrStrs.join(', ')}];`)
			}

			if (groupName !== 'nogroup') {
				lines.push('}')
			}
		}

		for (const link of this.links) {
			// when there's forced/unforced pair, only the former is a constrait
			// (this makes flow )
			const combo = link.dst + link.src

			if (combo && this.linkOutward.has(combo) && this.linkInward.has(combo)) {
				link.styles.push(['constraint', 'false'])
			}

			const styleStrs = link.styles.map(([key, value]) => `${key}=${value}`)
			const suffix = styleStrs.length === 0 ? '' : ` [${styleStrs.join(', ')}]`
			lines.push(`${link.src} -> ${link.dst}${suffix};`)
		}
		lines.push('}')
		return lines
	}

	private addBranch(branchStatus: BranchStatus, group: string | null = null) {
		const branch = branchStatus.def
		if (this.aliases.has(decoratedAlias(branchStatus.bot, branch.upperName)))
			return

		let tooltip = branch.rootPath
		if (branch.aliases && branch.aliases.length !== 0) {
			tooltip += ` (${branch.aliases.join(', ')})`
		}
		const info: Info = {
			id: `_${branchStatus.bot}_${branch.upperName.replace(/[^\w]+/g, '_')}`,
			tooltip, name: branch.name
		}
		if (branch.config.graphNodeColor) {
			info.graphNodeColor = branch.config.graphNodeColor
		}

		// note: branch.upperName is always in branch.aliases
		for (const alias of branch.aliases) {
			this.aliases.set(decoratedAlias(branchStatus.bot, alias), info)
		}
		setDefault(this.nodeLabels, group || 'nogroup', []).push(info)
	}

	// dstName 
	private addEdge(srcBranchStatus: BranchStatus, dst: string) {
		const src = this.getIdForBranch(srcBranchStatus)
		const branch = this.branchesById.get(src)!

		this.nodeImportance.set(src, (this.nodeImportance.get(src) || 0) + 1)
		this.nodeImportance.set(dst, (this.nodeImportance.get(dst) || 0) + 1)

		const isForced = branch.forcedDests.has(dst)
		const edgeStyle =
			srcBranchStatus.def.convertIntegratesToEdits ?	'roboshelf' :
			isForced ?										'forced' :
			branch.defaultDests.has(dst) ?					'defaultFlow' :
			branch.blockAssetDests.has(dst) ?				'blockAssets' : 'onRequest'

		const styles = [...EDGE_STYLES[edgeStyle]]

		const link: Link = {src, dst, styles}
		this.links.push(link)
		if (isForced) {
			this.linkOutward.add(src + dst)
		}
		else if (edgeStyle === 'blockAssets' || edgeStyle === 'onRequest') {
			this.linkInward.add(dst + src)
		}
	}

	private findSharedNodes() {
		const streamMap = new Map<string, [string | null, BranchStatus[]]>()
		for (const branchStatus of this.branchList) {
			const [color, streams] = setDefault(streamMap, branchStatus.def.rootPath, [null, []])
			streams.push(branchStatus)
			const nodeColor = branchStatus.def.config.graphNodeColor
			if (!color && nodeColor) {
				streamMap.set(branchStatus.def.rootPath, [nodeColor, streams])
			}
		}

		// create a shared info for each set of nodes monitoring the same stream
		for (const [k, v] of streamMap) {
			const [color, streams] = v
			if (streams.length > 1) {
				const info: Info = {
					id: k.replace(/[^\w]+/g, '_'), tooltip: 'tooltip todo', name: k
				}
				if (color) {
					info.graphNodeColor = color
				}

				for (const branchStatus of streams) {

					for (const alias of branchStatus.def.aliases) {
						this.aliases.set(decoratedAlias(branchStatus.bot, alias), info)
					}
				}

				setDefault(this.nodeLabels, 'nogroup', []).push(info)
			}
		}
	}

	private createIds() {
		for (const branchStatus of this.branchList) {
			const branch = branchStatus.def
			const getId = this.getIdForBot(branchStatus.bot)
			const branchId = getId(branch.upperName)
			this.branchesById.set(branchId, {
				id: branchId,
				forcedDests: new Set<string>(branch.forceFlowTo.map(getId)),
				defaultDests: new Set<string>(branch.defaultFlow.map(getId)),
				blockAssetDests: new Set<string>(branch.blockAssetTargets ? branch.blockAssetTargets.map(getId) : [])
			})
		}
	}

	private getIdForBranch(branch: BranchStatus) {
		// note: branch.upperName is always in branch.aliases
		return this.getIdForBot(branch.bot)(branch.def.upperName)
	}

	private getIdForBot(bot: string) {
		return (alias: string) => {
			const info = this.aliases.get(decoratedAlias(bot, alias))
			return info ? info.id : alias
		}
	}
}

function makeGraph(data: BranchStatus[], botNameParam: string | string[]) {

	let whitelist: string[] | null = null
	let botName: string | null

	const graph = new Graph(data)
	if (Array.isArray(botNameParam)) {
		botName = null
		if (botNameParam.length > 0) {
			return graph.allBots(botNameParam.map(s => s.toUpperCase()))
		}
		return graph.allBots()
	}
	return graph.singleBot(botNameParam.toUpperCase())
}

export function showFlowGraph(data: BranchStatus[], botNameParam: string | string[]) {
	const lines = makeGraph(data, botNameParam)

	const graphContainer = $('<div class="clearfix">')
	const flowGraph = $('<div class="flow-graph" style="display:inline-block;">').appendTo(graphContainer)
	flowGraph.append($('<div>').css('text-align', 'center').text("Building graph..."))

	renderGraph(lines.join('\n'))
	.then(svg => {
		$('#graph-key-template')
			.clone()
			.removeAttr('id')
			.css('display', 'inline-block')
			.appendTo(graphContainer)

		$('#graph-loading-text').hide()
		const span = $('<span>').css('margin','auto').html(svg)
		const svgEl = $('svg', span).addClass('branch-graph').removeAttr('width')
		// scale graph to 70% of default size
		const height = Math.round(parseInt(svgEl.attr('height')!) * .7)
		svgEl.attr('height', height + 'px').css('vertical-align', 'top')

		flowGraph.empty()
		flowGraph.append(span)
	})

	return graphContainer
}
