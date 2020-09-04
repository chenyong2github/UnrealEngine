// Copyright Epic Games, Inc. All Rights Reserved.

import { setDefault, sortBy } from '../common/helper';
import { ContextualLogger } from '../common/logger';
import { PerforceContext } from '../common/perforce';
import { Branch, BranchGraphInterface } from '../robo/branch-interfaces';

type TargetName = string & { __targetBrand: any }
type Stream = string & { __streamBrand: any }
type BotName = string & { __BotNameBrand: any }

type ComputeResult = Map<Edge, Edge[]>

type Success = 'failed' | 'succeeded'

// deliberately not much in here - assuming stream for the moment
export class Node {
	constructor(public stream: Stream) // of form //x[/x]*
	{}

	offerDebugName(name: string) {
		this._debugName = this._debugName || name
	}

	get debugName(): string {
		return this._debugName || (this.stream as string)
	}
	
	private _debugName: string | null = null
}

type EdgeFlag = 'automatic' | 'general'

export class Edge {
	constructor(
		public source: Node,
		public target: Node,

		public flags: Set<EdgeFlag>,


		public bot: BotName,					// all edges will be bot-specific in practice, although maybe not necessary
		public sourceAnnotation: any		// type specified by client code, e.g. NodeBot servicing this edge, or just last cl
	) {}
}

// all aliases are of the form '<bot>:<name>', name lowercase
export function makeTargetName(bot: BotName, nodeName: string) {
	return `${bot}:${nodeName.toLowerCase()}` as TargetName
}

function makeStream(depotPath: string) {
	if (!depotPath.match(/^\/\/[\w-.][\/\w-.]*$/)) {
		throw new Error('Invalid stream path: ' + depotPath)
	}
	return depotPath as Stream
}

export class Graph {
	private streamNodes = new Map<Stream, Node>()
	private targetNames = new Map<TargetName, Node>()
	private edgesBySource = new Map<Node, Set<Edge>>()
	private edgesByTarget = new Map<Node, Set<Edge>>()
	private edgesByBot = new Map<string, Set<Edge>>();

	private undecoratedAliases = new Map<string, Node[]>()

	findOrCreateStreamNode(depotPath: string) {
		const stream = makeStream(depotPath)
		return setDefault(this.streamNodes, stream, new Node(stream))
	}

	findNodeForStream(depotPath: string) {
		const stream = makeStream(depotPath)
		return this.streamNodes.get(stream)
	}

	findNodeByAlias(alias: string, logger: ContextualLogger) {
		const nodes = this.undecoratedAliases.get(alias.toLowerCase())
		if (nodes) {
			if (nodes.length !== 1) {
				logger.info(`Alias '${alias}' is ambiguous`)
			}
			else {
				return nodes[0]
			}
		}
		return null
	}

	addEdge(edge: Edge) {
		setDefault(this.edgesBySource, edge.source, new Set).add(edge)
		setDefault(this.edgesByTarget, edge.target, new Set).add(edge)
		setDefault(this.edgesByBot, edge.bot, new Set).add(edge)
	}

	addNameForNode(node: Node, targetName: TargetName, alias?: string) {
		// @todo check name is not already assigned to another node
		this.targetNames.set(targetName, node)
		node.offerDebugName(targetName)

		if (alias) {
			setDefault(this.undecoratedAliases, alias.toLowerCase(), []).push(node)
		}
	}

	getNode(name: TargetName) {
		return this.targetNames.get(name)
	}

	getEdgesBySource(src: Node, ...flags: EdgeFlag[]) {
		const edges = this.edgesBySource.get(src)
		if (!edges)
			return new Set<Edge>()

		if (flags.length === 0)
			return edges

		return new Set<Edge>([...edges].filter(edge => {
			for (const flag of flags) {
				if (!edge.flags.has(flag))
					return false
			}
			return true
		}))
	}

	computeReachable(result: Set<Node>, src: Node, ...flags: EdgeFlag[]) {
		// if result starts off with stuff, those nodes will be ignored (i.e. not checked)

		for (const edge of this.getEdgesBySource(src, ...flags)) {
			if (!result.has(edge.target)) {
				result.add(edge.target)
				this.computeReachable(result, edge.target, ...flags)
			}
		}
	}

	findRouteBetween(src: Node, target: Node, flags?: EdgeFlag[], skip?: Set<Node>) {
		const seen = skip || new Set<Node>()
		seen.add(src)
		return this.findRouteBetweenImpl([], src, seen, target, flags || [])
	}


	dump() {
		const nodeNames = new Map<Node, TargetName[]>()
		for (const [name, node] of this.targetNames) {
			setDefault(nodeNames, node, []).push(name)
		}

// for now, just dumping out aliases

		const orderedNodes = [...nodeNames.keys()]
		sortBy(orderedNodes, node => node.debugName)

		const result: [string, string[]][] = []
		for (const node of orderedNodes) {
			const aliases = nodeNames.get(node)!
			aliases.sort()
			result.push([node.debugName, aliases])
		}

		return result
	}

	private findRouteBetweenImpl(routeSoFar: Edge[], src: Node, seen: Set<Node>, target: Node, flags: EdgeFlag[]): Edge[] | null {
		for (const edge of this.getEdgesBySource(src, ...flags)) {
			if (!seen.has(edge.target)) {
				const routePlus = [...routeSoFar, edge]
				if (edge.target === target)
					return routePlus

				seen.add(edge.target)
				const route = this.findRouteBetweenImpl(routePlus, edge.target, seen, target, flags)
				if (route)
					return route
			}
		}
		return null
	}

	// function called this to match NodeBot function
	computeImplicitTargets(
		source: Node,
		requestedTargets: Map<Node, MergeMode>
	): {status: Success, merges?: ComputeResult, unreachable?: Node[]} {

		const targetsToFind = new Set<Node>()
		const skipNodes = new Set<Node>()
		for (const [target, mergeMode] of requestedTargets.entries()) {
			targetsToFind.add(target)
			if (mergeMode === 'skip') {
				skipNodes.add(target)
			}
		}

		// initial edge to follow, then subsequent
		const merges = new Map<Edge, Edge[]>()

		// parallel flood the flowsTo graph to find all specified targets

		// branchPaths is an array of 3-tuples, representing the branches at the current boundary of the flood
		//	- first entry is the branch
		//	- second and third entries make up the path to get to that branch
		//		- second entry, if non-empty, has the path up to the most recent explicit target
		//		- third entry has any remaining implicit targets

		const branchPaths: [Node, Edge[], Edge[]][] = [[source, [], []]]

		const seen = new Set([source])
		while (targetsToFind.size !== 0 && branchPaths.length !== 0) {
			const [sourceNode, explicit, implicit] = branchPaths.shift()!

			// flood the graph one step to include all unseen direct flowsTo nodes of one branch
			let anyUnseen = false

			const flowsTo = this.edgesBySource.get(sourceNode) // here's where we will need to be able to filter by bot
			if (flowsTo && !skipNodes.has(sourceNode)) {
				for (const edge of flowsTo) {
					const node = edge.target
					if (seen.has(node))
						continue

					seen.add(node)
					anyUnseen = true

					if (targetsToFind.has(node)) {
						branchPaths.unshift([node, [...explicit, ...implicit, edge], []])
					}
					else {
						branchPaths.push([node, explicit, [...implicit, edge]])
					}
				}
			}

			// store the calculated merge data if we've exhausted a sub-tree of the graph
			if (!anyUnseen && explicit.length > 0) {
				const directEdge = explicit[0]
				targetsToFind.delete(directEdge.target)

				if (skipNodes.has(directEdge.target))
					continue

				const indirectTargets = setDefault(merges, directEdge, [])

				for (let index = 1; index < explicit.length; ++index) {
					const indirectEdge = explicit[index]
					if (indirectTargets.indexOf(indirectEdge) === -1) {
						indirectTargets.push(indirectEdge)
					}
					targetsToFind.delete(indirectEdge.target)
				}
			}
		}

		if (targetsToFind.size > 0) {
			return {status: 'failed', unreachable: [...targetsToFind]}
		}

		return {status: 'succeeded', merges}
	}
}

export function addBranchGraph(graph: Graph, branchGraph: BranchGraphInterface) {

	const botname = branchGraph.botname as BotName
	const branchNodes = new Map<Branch, Node>()

	// excluding subpath bots for now
	const streamBranches = branchGraph.branches.filter(x => x.stream && x.rootPath === x.stream + '/...')
	for (const branch of streamBranches) {
		if (!branch.bot) {
			throw new Error(`branch ${branch.name} not running!`) // fine, but try again later
		}
		const branchNode = graph.findOrCreateStreamNode(branch.stream!)
		branchNodes.set(branch, branchNode)
		graph.addNameForNode(branchNode, makeTargetName(botname, branch.name))

		for (const alias of branch.aliases) {
			graph.addNameForNode(branchNode, makeTargetName(botname, alias))
		}
	}

	for (const branch of streamBranches) {
		const sourceNode = branchNodes.get(branch)!
		for (const target of branch.flowsTo) {
			const flags = new Set<EdgeFlag>()
			if (branch.forceFlowTo.indexOf(target) !== -1) {
				flags.add('automatic')
			}

			if (branch.isDefaultBot) {
				flags.add('general')
			}

			graph.addEdge(new Edge(sourceNode,
				branchNodes.get(branchGraph.getBranch(target)!)!,
				flags, botname, branch.bot!
			))
		}
	}

	if (branchNodes.size > 1) {
		//testing syntax
		const nodeIter = branchNodes.values()

		const src = nodeIter.next().value
		const targets = new Map<Node, MergeMode>()
		targets.set(nodeIter.next().value, 'normal')

		graph.computeImplicitTargets(src, targets)
	}
}

export type MergeMode = 'safe' | 'normal' | 'null' | 'clobber' | 'skip'

class Test {
	graph = new Graph
	targets = new Map<Node, MergeMode>()

	static readonly BOT_NAME = 'TEST' as BotName

	constructor(nodeStrs: string[]) {

		for (let index = 0; index < nodeStrs.length; ++index) {
			const name = String.fromCharCode('a'.charCodeAt(0) + index)

			const fromNode = this.addDummyStream(name)

			for (const char of nodeStrs[index]) {
				const targetNode = this.addDummyStream(char)

				const flags = new Set<EdgeFlag>()
				if (char.toUpperCase() === char) {
					flags.add('automatic')
				}

				this.graph.addEdge(new Edge(fromNode, targetNode, flags, Test.BOT_NAME, {}))
			}
		}
	}

	computeTargets(source: string, targetsString: string) {
		const sourceNode = this.getNode(source)
		this.fillTargetsMap(targetsString)

		return this.graph.computeImplicitTargets(sourceNode, this.targets)
	}

	formatTestComputeTargetsResult(computed: ComputeResult) {
		const bits: string[] = []
		for (const [direct, indirect] of computed.entries()) {
			bits.push(this.formatNode(direct.target) + ': ' + indirect.map(x => this.formatNode(x.target)).join(', '))
		}
		return bits.join('; ')
	}

	getNode(name: string) {
		const node = this.graph.getNode(makeTargetName(Test.BOT_NAME, name))
		if (!node) throw 'arse!'
		return node
	}

	private fillTargetsMap(targets: string) {

		const targetBits = targets.split('-') // up to three section in target (1st) string: normal, skip, null

		for (const targetChar of targetBits[0]) {
			this.targets.set(this.getNode(targetChar), 'normal')
		}

		if (targetBits.length > 1) {
			for (const targetChar of targetBits[1]) {
				this.targets.set(this.getNode(targetChar), 'skip')
			}
		}

		if (targetBits.length > 2) {
			for (const targetChar of targetBits[2]) {
				this.targets.set(this.getNode(targetChar), 'null')
			}
		}
	}

	private formatNode(node: Node) {

		const mm = this.targets.get(node)
		const prefix = !mm || mm === 'normal' ? '' :
			mm === 'skip' ? '-' :
			mm === 'null' ? '!' : '@'

		return prefix + node.debugName[5]
	}

	private addDummyStream(name: string) {
		const targetName = makeTargetName(Test.BOT_NAME, name)
		let node = this.graph.getNode(targetName)
		if (!node) {
			node = this.graph.findOrCreateStreamNode('//Test/' + name.toUpperCase())
			this.graph.addNameForNode(node, targetName)
		}
		return node
	}

}

// async function inner() {
// 	throw new Error('test')
// }

// function proxy() {
// 	const promise = inner()
// 	promise.finally(() => console.log('here!'))
// 	return promise
// }

// async function outer() {
// 	try {
// 		await proxy()
// 	}
// 	catch (err) {
// 		console.log('caught!')
// 	}
// }


// function runUnhandledRejectionTest() {
// 	outer()
// }


export function runTests(parentLogger: ContextualLogger) {



	// runUnhandledRejectionTest()




	const unitTestLogger = parentLogger.createChild('Graph')
	const tests: [string[], string | null][] = [
		[['c-b', 'a', 'b', 'c', ''], null],
		[['c-b', 'a', 'B', 'c', ''], null],
		[['c-d', 'a', 'bd', 'c', '', 'c'], 'B:C'],
		[['c-b', 'a', 'bD', 'c', '', 'c'], 'D:C'],
		[['-c', 'a', 'Bd', 'C', '', ''], 'B:-C'],
		[['-c-b', 'a', 'B', 'C', ''], '!B:-C'],
		[['b', 'a', 'b', 'C', ''], 'B:'],
		[['f', 'b', 'bcd', 'a', 'a', 'Ae', 'Df', 'Eg', 'F'], 'A:DEF'],
		[['b', 'f', 'bcd', 'a', 'a', 'Ae', 'Df', 'Eg', 'F'], 'E:DAB'],
		[['a', 'g', 'bcd', 'a', 'a', 'Ae', 'Df', 'Eg', 'F'], 'F:EDA'],
		[['-a', 'g', 'bcd', 'a', 'a', 'Ae', 'Df', 'Eg', 'F'], 'F:ED-A'],
		[['de', 'a', 'b', 'c', 'de', '', ''], 'B:CED'],
		[['de', 'h', 'hc', 'hd', 'deFg', 'hbc', 'c', 'c', 'c', 'abd'], 'D:CE'],
		[['de', 'h', 'bd', 'hc', 'hd', 'eFg', 'hbc', 'c', 'c', 'c'], 'C:DE'],
		[['db', 'h', 'abd', 'hc', 'hd', 'deFg', 'hbc', 'c', 'c', 'c'], 'C:DEB'],
		[['cg', 'a', 'be', 'acd', 'b', 'b', 'aFg', 'e', 'e'], 'B:C;E:G'],
		[['cf', 'a', 'bE', 'acd', 'b', 'b', 'aFg', 'e', 'e'], 'B:C;E:F'],
		[['c-e', 'a', 'bE', 'acd', 'b', 'b', 'aFg', 'e', 'e'], 'B:C'],
		[['cf-b', 'a', 'bE', 'acd', 'b', 'b', 'aFg', 'e', 'e'], null],
		[['fg', 'h', 'abd', 'hC', 'hd', 'defg', 'hbc', 'c', 'c', 'c'], 'C:DGF'],
		[['dge', 'h', 'abd', 'hC', 'hd', 'defg', 'hbc', 'c', 'c', 'c'], 'C:DGE'],
		[['e', 'i', 'abcd', 'Ie', 'if', 'iG', 'ih', 'a', 'b', 'c', 'D'], 'D:GBE']
	]

	let ran = 0, success = 0, fail = 0
	for (const [testStr, expected] of tests) {
		const test = new Test(testStr.slice(2))
		const result = test.computeTargets(testStr[1], testStr[0])

		++ran
		let expectedOnFail

		const succeeded = result.status === 'succeeded'

		const formattedResult = succeeded ? test.formatTestComputeTargetsResult(result.merges!) : 'failed'
		if (expected) {
			if (!succeeded) {
				expectedOnFail = expected
				++fail
			}
			else {
				if (formattedResult.toUpperCase().replace(/\s|,/g, '') === expected)
					++success
				else {
					expectedOnFail = expected
					++fail
				}
			}
		}
		else if (succeeded) {
			expectedOnFail = 'fail'
		}
		if (expectedOnFail) {
			unitTestLogger.warn(`MISMATCH!!!    ${formattedResult} vs ${expectedOnFail}`)
		}
	}
	unitTestLogger.info(`Graph tests: ran ${ran} tests, ${success} matched, ${fail} failed`)
	return fail
}

type ChangeFlag = 'review' | 'manual' | 'null' | 'ignore' | 'disregardexcludedauthors' | 'roboshelf'
const ALLOWED_RAW_FLAGS = new Set(['null','ignore','deadend'])

const FLAGMAP: {[name: string]: ChangeFlag} = {
	ignore: 'ignore'
};

// should probably go in NodeBot, although specifically only dealing with string parsing

// gets tricky straight away deailing with commands for other bots
// will make target names here
function parseTargetsAndFlagsImpl(tokens: string[], logger: ContextualLogger, forcedMode?: MergeMode) {
	const flags = new Set<ChangeFlag>()
	const targets = new Map<string, MergeMode>()

	for (const token of tokens) {
		const tokenLower = token.toLowerCase()

		// check for 'ignore' and remap to #ignore flag
		if (ALLOWED_RAW_FLAGS.has(tokenLower))
		{
			flags.add(FLAGMAP[tokenLower]!);
			continue;
		}

		// see if this has a modifier
		const prefix = token.charAt(0)
		if (prefix === '#' || prefix === '$') { // $ provided as alternative, but not used as far as I know
			// set the flag and continue the loop
			const flagname = FLAGMAP[tokenLower.substr(1)]
			if (flagname) {
				flags.add(flagname)
			}
			else {
				logger.info(`Ignoring unknown flag "${token}"`)
			}
			continue
		}

		let targetName = token
		let mergeMode = forcedMode || 'normal'
		if (prefix === '!' || prefix === '-')
		{
			targetName = token.substr(1)
			mergeMode = prefix === '!' ? 'null' : 'skip'
		}

		const existingMergeMode = targets.get(targetName)
		if (existingMergeMode) {
			if (existingMergeMode !== mergeMode) {
				throw new Error('mismatch!') // todo add to list of errors instead
			}
		}

		targets.set(targetName, mergeMode)
	}

	return {status: 'succeeded', flags, targets}
}

export function parseTargetsAndFlags(graph: Graph, botname: BotName, tokens: string[], automaticTargets: Node[], logger: ContextualLogger, forcedMode?: MergeMode) {
	const result = parseTargetsAndFlagsImpl(tokens, logger, forcedMode)
	if (result.status !== 'succeeded') {
		throw new Error('to do!')
	}

	const targets = new Map<Node, MergeMode>()

	// make sure we can find a node for each target
	for (const [targetStr, mergeMode] of result.targets) {
		const node = graph.getNode(makeTargetName(botname, targetStr))
		if (!node) {
			throw new Error('to do!')
		}
		targets.set(node, mergeMode)
	}


// kind of odd to do this before computing implicit, but shouldn't do any harm
	for (const auto of automaticTargets) {
		if (!targets.has(auto)) {
			targets.set(auto, 'normal' || forcedMode)
		}
	}

	return {status: 'succeeded', flags: result.flags, targets}
}

type StringBotNamePair = [string, BotName | 'default']

function parseChange(cldesc: string) {

	// parse the description
	const descLines = cldesc.split('\n')

	let tokens: StringBotNamePair[] = []
	for (const line of descLines) {
		// trim end - keep any initial whitespace
		const comp = line.replace(/\s+$/, '')

		// check for control hashes
		const match = comp.match(/^(\s*)#([-\w[\]]+)[:\s]*(.*)$/)
		if (!match)
			continue

		const ws = match[1] 
		const command = match[2].toUpperCase()
		const value = match[3].trim()

		// #robomerge tags are required to be at the start of the line
		if (ws)
			// could do this by changing regex, but keeping this to match flow in NodeBot code
			continue

		let bot: BotName | 'default' | null = null
	 	if (command === 'ROBOMERGE') {

			// completely ignore bare ROBOMERGE tags if we're not the default bot (should not affect default flow)
			bot = 'default'
		}

		else  {
			// what sort of robomerge command is it
			const specificMatch = command.match(/^ROBOMERGE\[(.*)\]$/)
			if (specificMatch) {
				bot = specificMatch[1] as BotName
			}
		}

		if (bot) {
			const tokensForThisLine: StringBotNamePair[] = value.split(/[ ,]/).filter(Boolean).map(token => [token, bot] as StringBotNamePair)
			tokens = [...tokens, ...tokensForThisLine]
		}
	}

	return tokens
}

export class Trace {
	private readonly traceLogger: ContextualLogger
	private readonly p4: PerforceContext
	constructor(private graph: Graph, parentLogger: ContextualLogger)
	{
		this.traceLogger = parentLogger.createChild('Trace')
		this.p4 = new PerforceContext(this.traceLogger)
	}

	async traceRoute(cl: number, sourceNodeStr: string, targetNodeStr: string) {
		// const sourceBranch = this.resolveBranch(sourceBranchStr)
		const sourceNode = this.graph.findNodeForStream(sourceNodeStr)
		const targetNode = this.graph.findNodeForStream(targetNodeStr)

// should just dump source if source and target the same?

		if (!sourceNode)
			return 'UNKNOWN_SOURCE_BRANCH'

		if (!targetNode)
			return 'UNKNOWN_TARGET_BRANCH'

		if (sourceNode === targetNode)
			return 'SOURCE_AND_TARGET_THE_SAME'

		let change
		try {
			change = await this.p4.getChange(sourceNode.stream + '/...', cl)
		}
		catch (err) {
			return 'CHANGELIST_NOT_FOUND'
		}


		if (typeof change.desc !== 'string')
			return 'INVALID_CHANGELIST_DESCRIPTION'

		const tokensAndBots = parseChange(change.desc)

		const outEdges = this.graph.getEdgesBySource(sourceNode)
		if (outEdges.size === 0)
			return 'CHANGE_WILL_NOT_REACH_TARGET'

// will have to check if the source node is served by multiple bots, and if so, calculate targets for each of them
// for now, just pick one
const arbitraryEdge = outEdges.values().next().value
const botname = arbitraryEdge.bot
const isDefaultBot = arbitraryEdge.flags.has('general')

		const relevantTokens = tokensAndBots
			.filter(([_, bot]) => (bot === botname || isDefaultBot && bot === 'default'))
			.map(([token, _]) => token)

		const parseResult = parseTargetsAndFlags(this.graph, botname, relevantTokens, 
			[/*...graph.getEdgesBySource(sourceNode, 'automatic')*/], this.traceLogger)

		if (parseResult.status !== 'succeeded')
			return 'INVALID_CHANGELIST_DESCRIPTION'

		const skipNodes = new Set<Node>([...parseResult.targets.entries()]
			.filter(([_, mergeMode]) => mergeMode === 'skip')
			.map(([node, _]) => node)
		);

		// find a route (would ideally be able to cope with multiple valid routes)
		const route = this.graph.findRouteBetween(sourceNode, targetNode, [], skipNodes)
		if (!route)
			return 'CHANGE_WILL_NOT_REACH_TARGET'

		// - find last non-automatic edge, if any: its target needs to be somewhere in resultant merges
		let nonAutoEdge: Edge | null = null
		for (let index = route.length - 1; index >= 0; --index) {
			const edge = route[index]
			if (!edge.flags.has('automatic')) {
				nonAutoEdge = edge
				break
			}
		}

		// always compute targets in case it fails
		const computeResult = this.graph.computeImplicitTargets(sourceNode, parseResult.targets)

		if (computeResult.status !== 'succeeded')
			return 'INVALID_CHANGELIST_DESCRIPTION'


		if (nonAutoEdge) {

			let found = false

		outer:
			for (const [firstEdge, furtherEdges] of computeResult.merges!) {
				if (firstEdge.target === nonAutoEdge.target) {
					found = true
					break
				}

				for (const edge of furtherEdges) {
					if (edge.target === nonAutoEdge.target) {
						found = true
						break outer
					}
				}
			}

			if (!found) 
				return 'CHANGE_WILL_NOT_REACH_TARGET'
		}

		return route
	}
}
