// Copyright Epic Games, Inc. All Rights Reserved.

import { Branch, MergeAction, MergeMode } from './branch-interfaces';
import { BranchGraphInterface, PendingChange, Target, TargetInfo } from './branch-interfaces';
import { BotName, Edge, GraphAPI, Node, makeTargetName } from '../new/graph';
import { ContextualLogger } from '../common/logger';

// const NEW_STUFF = false

export function getIntegrationOwner(targetBranch: Branch, overriddenOwner?: string): string | null
export function getIntegrationOwner(pending: PendingChange): string
export function getIntegrationOwner(arg0: Branch | PendingChange, overriddenOwner?: string) {
	// order of priority for owner:

	//  1)	a) Change flagged 'manual', i.e. will create a shelf
	//			- the 'manual' flag itself is problematic, really ought to be a per target thing somehow
	//		b) shelf/stomp request
	//		c) edge reconsider
	//	2) resolver - need resolver to take priority, even over node reconsider, since recon might be from branch with
	//					multiple targets, so instigator might not even know about target with resolver
	//	3) node reconsider
	//	4) propagated/manually added tag
	//	5) author - return null here for that case

	let targetBranch: Branch | null = null
	let pending: PendingChange | null = null
	if ((arg0 as PendingChange).action) {
		pending = arg0 as PendingChange
	}
	else {
		targetBranch = arg0 as Branch
	}


	// Manual requester or edge reconsider
	if (pending && (
			pending.action.flags.has('manual') ||
			pending.change.forceCreateAShelf ||
			pending.change.userRequest == 'edge-reconsider'
		)) {
		return pending.change.owner
	}

	const branch = pending ? pending.action.branch : targetBranch
	const owner = pending ? pending.change.owner : overriddenOwner

	return branch!.resolver || owner || null
}

export function getNodeBotFullName(botname: string, branchName: string) {
	return `${botname} ${branchName}`
}

export function getNodeBotFullNameForLogging(botname: string, branchName: string) {
	return `${botname}:${branchName}`
}

// #commands in commit messages we should neuter so they don't spam (use only UPPERCASE)
const NEUTER_COMMANDS = [
	"CODEREVIEW",
	"FYI",
	"QAREVIEW",
	"RN",
	"DESIGNCHANGE",
	"REVIEW",
]

type ChangeFlag = 'review' | 'manual' | 'null' | 'ignore' | 'disregardexcludedauthors'

// mapping of #ROBOMERGE: flags to canonical names
// use these with a pound like #ROBOMERGE: #stage
const FLAGMAP: {[name: string]: ChangeFlag} = {
	// force a codereview to be sent to the owner as commits flow down the automerge chain
	review: 'review',
	forcereview: 'review',
	cr: 'review',

	// don't merge, only shelf this change and review the owner
	manual: 'manual',
	nosubmit: 'manual',
	stage: 'manual',

	// expand any automerge targets as null merges
	'null': 'null',

	// ignore this commit, do not merge anywhere (special "#ROBOMERGE: ignore" syntax without pound also triggers this)
	ignore: 'ignore',
	deadend: 'ignore',

	// process this change even if the author is on the excluded list
	disregardexcludedauthors: 'disregardexcludedauthors',
}

const ALLOWED_RAW_FLAGS = ['null','ignore','deadend']



function parseTargetList(targetString: string) {
	return targetString.split(/[ ,]/).filter(Boolean)
}

export class DescriptionParser {
	source = ''
	owner: string | null = null
	authorTag: string | null = null

	descFinal: string[] = []

	propagatingNullMerge = false
	hasOkForGithubTag = false
	useDefaultFlow = true

	// arguments: string[] = []

	// per target bot arguments
	arguments: string[] = []
	otherBotArguments: [string, string][] = []

	expandedMacros: string[] = []
	expandedMacroLines: string[] = []

	errors: string[] = []

	constructor(
		private isDefaultBot: boolean,
		private graphBotName: string,
		private cl: number,
		private aliasUpper: string,
		private macros: {[name: string]: string[]}

	) {
	}

	/** override anything parsed from robomerge tags */
	override(other: DescriptionParser) {

		this.useDefaultFlow = other.useDefaultFlow
		this.arguments = other.arguments
		this.expandedMacros = other.expandedMacros
		this.otherBotArguments = other.otherBotArguments

		this.errors = other.errors
	}

	parseLine(line: string) {

		// trim end - keep any initial whitespace
		const comp = line.replace(/\s+$/, '')

		// check for control hashes
		const match = comp.match(/^(\s*)#([-\w[\]]+)[:\s]*(.*)$/)
		if (!match) {
			// strip beginning blanks
			if (this.descFinal.length > 0 || comp !== '') {
				if (comp.indexOf('[NULL MERGE]') !== -1) {
					this.propagatingNullMerge = true
				}
				this.descFinal.push(line)
			}
			return
		}

		const ws = match[1] 
		const command = match[2].toUpperCase()
		const value = match[3].trim()

		// #robomerge tags are required to be at the start of the line
		if (ws && command === 'ROBOMERGE' && value.match(/\bnone\b/i)) {
			// completely skip #robomerge nones that might come in from GraphBot branch integrations, so we don't
			// get impaled on our own commit hook
			return
		}

		// check for any non-robomerge # tags we care about
		if (ws || !command.startsWith('ROBOMERGE')) {
			// check for commands to neuter
			if (NEUTER_COMMANDS.indexOf(command) >= 0) {
				// neuter codereviews so they don't spam out again
				this.descFinal.push(`${ws}[${command}] ${value}`)
			}
			
			else if (command.indexOf('REVIEW-') === 0) {
				// remove swarm review numbers entirely (they can't be neutered and generate emails)
				if (value !== '') {
					this.descFinal.push(value)
				}
			}
			else if (command === 'OKFORGITHUB') {
				this.hasOkForGithubTag = true
				this.descFinal.push(line)
				}
			else {

				// default behavior is to keep any hashes we don't recognize
				this.descFinal.push(line)
			}
			return
		}

		// Handle commands
		if (command === 'ROBOMERGE') {

			// ignore bare ROBOMERGE tags if we're not the default bot (should not affect default flow)
			if (this.isDefaultBot) {
				this.useDefaultFlow = false
				this.processTargetList(value)
			}
			return
		}
		
		// Look for a specified bot and other defined tags
		const specificBotMatch = command.match(/ROBOMERGE\[([-_a-z0-9]+)\]/i)

		if (specificBotMatch) {
			const specificBot = specificBotMatch[1].toUpperCase()
			if (specificBot === this.graphBotName ||
				specificBot === this.aliasUpper ||
				specificBot === 'ALL') {
				this.useDefaultFlow = false

// @todo 'ALL' with targets should be an error (but don't know at this point ...)
				this.processTargetList(value)
			}
			else {
				// keep a record of commands to forward to other bots - processed later
				this.otherBotArguments = [
					...this.otherBotArguments,
					...(parseTargetList(value).map(arg => [specificBot, arg]) as [string, string][])
				]
			}
		}
		else if (command === 'ROBOMERGE-AUTHOR') {
			// tag intended to be purely for propagating
			// currently using it to set owner if not already set
			this.authorTag = value
		}
		else if (command === 'ROBOMERGE-OWNER') {
			this.owner = value.toLowerCase()
		}
		else if (command === 'ROBOMERGE-SOURCE') {
			this.source = `${value} via CL ${this.cl}`
		}
		else if (command === 'ROBOMERGE-BOT') {
			// if matches description written in _mergeCl
			if (value.startsWith(this.graphBotName + ' (')) {
				this.useDefaultFlow = false
			}
		}
		else if (command !== 'ROBOMERGE-EDIGRATE' &&
				 command !== 'ROBOMERGE-COMMAND' && 
				 command !== 'ROBOMERGE-CONFLICT') {
			// add syntax error for unknown command
			this.errors.push(`Unknown command '${command}`)
		}
	}

	private processTargetList(targetString: string) {
		const targetNames = parseTargetList(targetString)

		for (const name of targetNames) {
			const macroLines = this.macros[name.toLowerCase()]
			if (macroLines) {
				this.expandedMacros.push(name)
				this.expandedMacroLines = [...this.expandedMacroLines, ...macroLines]
			}
			else {
				this.arguments.push(name)
			}
		}
	}
}

export function parseDescriptionLines(
	lines: string[],
	isDefaultBot: boolean, 
	graphBotName: string, 
	cl: number, 
	aliasUpper: string,
	macros: {[name: string]: string[]},
	logger: ContextualLogger
	) {
	const lineParser = new DescriptionParser(isDefaultBot, graphBotName, cl, aliasUpper, macros)
	let safety = 5
	while (lines.length > 0) {
		for (const line of lines) {
			lineParser.parseLine(line)
		}
		lines = lineParser.expandedMacroLines
		lineParser.expandedMacroLines = []

		if (--safety === 0) {
			logger.warn('Got stuck in a loop expanding macros in CL#' + cl)
			break
		}
	}
	return lineParser
}

function isFlag(arg: string) {
	return ALLOWED_RAW_FLAGS.indexOf(arg.toLowerCase()) >= 0 || '$#'.indexOf(arg[0]) >= 0
}


/**

Algorithm:
	For each command
		* find all routes (skip some non-viable ones, like initial skip)
		* if none, error, listing reasons for discounting non-viable routes
		* otherwise add complete command list (including flags) to the further merges list of each target branch
			that is the first step of a route

	So we have a set of branches that are the first hop for routes relevant to a particular other bot
 */

type OtherBotInfo = {
	firstHopBranches: Set<Branch>
	commands: string[]
	aliasOrName: string
}

export function processOtherBotTargets(
	parsedLines: DescriptionParser,
	sourceBranch: Branch,
	ubergraph: GraphAPI,
	actions: MergeAction[],
	errors: string[]
	) {

	const ensureNode = (bot: BotName, branch: Branch) => {
		const node = ubergraph.getNode(makeTargetName(bot, branch.upperName))

		if (!node) {
			throw new Error(`Node look-up in ubergraph failed for '${bot}:${branch.upperName}'`)
		}
		return node
	}

	const otherBotInfo = new Map<BotName, OtherBotInfo>()
	const botInfo = (bg: BranchGraphInterface) => {
		const key = bg.botname as BotName
		let info = otherBotInfo.get(key)
		if (!info) {
			info = {
				firstHopBranches: new Set<Branch>(),
				commands: [],
				aliasOrName: bg.config.alias || bg.botname
			}
			otherBotInfo.set(key, info)
		}
		return info
	}

	const sourceBotName = sourceBranch.parent.botname as BotName

	// calculate skip set first time we need it
	let skipNodes: Node[] | null = null

	for (const [bot, arg] of parsedLines.otherBotArguments) {
		const targetBranchGraph = ubergraph.getBranchGraph(bot.toUpperCase() as BotName)
		if (!targetBranchGraph) {
			errors.push(`Bot '${bot}' not found in ubergraph`)
			continue
		}

		const targetBotName = targetBranchGraph.botname as BotName

		if (isFlag(arg)) {
			// no further checking for flags
			botInfo(targetBranchGraph).commands.push(arg)
			continue
		}

		const target = '!-'.indexOf(arg[0]) < 0 ? arg : arg.substr(1)
		const branch = targetBranchGraph.getBranch(target)
		if (!branch) {
			if (!targetBranchGraph.config.macros[target.toLowerCase()] &&
				targetBranchGraph.config.branchNamesToIgnore.indexOf(target.toUpperCase()) < 0) {
				errors.push(`Branch '${target}' not found in ${bot}`)
			}
			continue
		}

		const sourceNode = ensureNode(sourceBotName, sourceBranch)
		const targetNode = ensureNode(targetBotName, branch)

		if (sourceNode === targetNode) {
			// ignore accidental self reference: not sure if this can happen legitimately (maybe try making stricter later)
			continue
		}

		if (!skipNodes) {
			// would ideally add all non-forced edges that aren't explicit targets
			skipNodes = actions
				.filter(action => action.mergeMode === 'skip')
				.map(action => ensureNode(sourceBotName, action.branch))
		}

		const routes = ubergraph.findAllRoutesBetween(sourceNode, targetNode, [], new Set(skipNodes))
		if (routes.length === 0)
		{
			errors.push(`No route between '${sourceNode.debugName}' and '${targetNode.debugName}'`)
			continue
		}

		for (const route of routes) {
			if (route.length === 0) {
				throw new Error('empty route!')
			}

			// if first edge is outside of bot, ignore (the other bot will pick this up)
			if (route[0].bot !== sourceBotName) {
				continue
			}

			// check if route uses the target bot
			// mostly this just means at least the last edge should belong to the target bot
			let relevant = false
			for (const edge of route) {
				if (edge.bot === targetBotName) {
					relevant = true
					break
				}
			}

			if (!relevant) {
				continue
			}

			const firstBranchOnRoute = sourceBranch.parent.getBranch(route[0].targetName)
			if (!firstBranchOnRoute) {
				throw new Error(`Can't find branch ${route[0].target.debugName} (${route[0].targetName}) from ubergraph`)
			}

			botInfo(targetBranchGraph).firstHopBranches.add(firstBranchOnRoute)
		}
		botInfo(targetBranchGraph).commands.push(arg)
	}

	if (errors.length > 0 || otherBotInfo.size === 0) {
		return
	}

	// no more errors after this point: add or do not add.
	// (we allow some non-viable routes for simplicity: if we want to catch them, do so above)

	// always use bot aliases here if available, on the assumption that they are the more obfuscated/less
	// likely to trip up commit hooks

	for (const info of otherBotInfo.values()) {
		for (const firstHop of info.firstHopBranches) {
			for (const action of actions) {
				if (action.branch === firstHop) {
					// slight hack here: mergemode is always normal and branchName is all the original commands/flags unaltered
					action.furtherMerges.push({branchName: info.commands.join(' '), mergeMode: 'normal', otherBot: info.aliasOrName})
				}
			}
		}
	}
}

class RequestedIntegrations {

	readonly flags = new Set<ChangeFlag>()
	integrations: [string, MergeMode][] = []

	parse(commandArguments: string[], cl: number, forceStomp: boolean, logger: ContextualLogger) {
		// If forceStompChanges, add the target as a stomp if it isn't a flagged target
		if (forceStomp) {
			this.integrations = commandArguments
				.filter(arg => '!-$#'.indexOf(arg[0]) < 0)
				.map(arg => [arg, 'clobber'])
			return
		}

		for (const arg of commandArguments) {
			const argLower = arg.toLowerCase()

			// check for 'ignore' and remap to #ignore flag
			if (ALLOWED_RAW_FLAGS.indexOf(argLower) >= 0)
			{
				this.flags.add(FLAGMAP[argLower]);
				continue;
			}

			const firstChar = arg[0]

			// see if this has a modifier
			switch (firstChar)
			{
			default:
				this.integrations.push([arg, 'normal'])
				break
			
			case '!':
				this.integrations.push([arg.substr(1), 'null'])
				break;

			case '-':
				this.integrations.push([arg.substr(1), 'skip'])
				break

			case '$':
			case '#':
				// set the flag and continue the loop
				const flagname = FLAGMAP[argLower.substr(1)]
				if (flagname) {
					this.flags.add(flagname)
				}
				else {
					logger.warn(`Ignoring unknown flag "${arg}" in CL#${cl}`)
				}
				break
			}
		}

	}
}

type ComputeTargetsResult =
	{ computeResult:
	  { merges: Map<Branch, Branch[]> | null
	  , flags: Set<ChangeFlag>
	  , targets: Map<Branch, MergeMode>
	  } | null
	, errors: string[] 
	}

// public so it can be called from unit tests
export function computeTargetsImpl(
	sourceBranch: Branch, 
	ubergraph: GraphAPI,
	cl: number,
	forceStomp: boolean,
	allowIgnore: boolean,
	commandArguments: string[],
	defaultTargets: string[], 
	logger: ContextualLogger
): ComputeTargetsResult {

	// list of branch names and merge mode (default targets first, so merge mode gets overwritten if added explicitly)

	const ri = new RequestedIntegrations
	ri.parse(commandArguments, cl, forceStomp, logger)

	if (ri.flags.has('ignore')) {
		return { computeResult: null, errors: allowIgnore ? [] : ['deadend (ignore) disabled for this stream'] }
	}

	if (ri.flags.has('null')) {
		for (const branchName of sourceBranch.forceFlowTo) {
			ri.integrations.push([branchName, 'null'])
		}
	}

	const defaultMergeMode: MergeMode = forceStomp ? 'clobber' : 'normal'

	const branchGraph = sourceBranch.parent
	const requestedMerges = [
		...defaultTargets.map(name => [name, defaultMergeMode] as [string, MergeMode]),
		...ri.integrations.filter(([name, _]) => branchGraph.config.branchNamesToIgnore.indexOf(name.toUpperCase()) < 0)
	]

	// compute the targets map
	const skipBranches = new Set<Branch>()
	const targets = new Map<Branch, MergeMode>()
	const errors: string[] = []

	// process parsed targets
	for (const [targetName, mergeMode] of requestedMerges) {
		// make sure the target exists
		const targetBranch = branchGraph.getBranch(targetName)
		if (!targetBranch) {
			errors.push(`Unable to find branch "${targetName}"`)
			continue
		}

		if (targetBranch === sourceBranch) {
			// ignore merge to self to prevent indirect target loops
			continue
		}

		if (mergeMode === 'skip') {
			skipBranches.add(targetBranch)
		}

// note: this allows multiple mentions of same branch, with flag of last mention taking priority. Could be an error instead
		targets.set(targetBranch, mergeMode)
	}

	const botname = sourceBranch.parent.botname as BotName

	const sourceNode = ubergraph.getNode(makeTargetName(botname, sourceBranch.upperName))
	if (!sourceNode) {
		throw new Error(`Source node ${sourceBranch.upperName} not found in ubergraph`)
	}

// note: this allows multiple mentions of same branch, with flag of last mention taking priority. Could be an error instead
	const allIntegrations = new Map<string, MergeMode>(requestedMerges)

	const requestedTargets = new Map<Node, MergeMode>()
	for (const [targetName, mergeMode] of allIntegrations) {
		const node = ubergraph.getNode(makeTargetName(botname, targetName))
		if (node) {
			if (node !== sourceNode) {
				requestedTargets.set(node, mergeMode)
			}
		}
		else {
			errors.push(`Target node ${targetName} not found in ubergraph`)
		}
	}

	if (errors.length > 0) {
		return { computeResult: null, errors }
	}

	const {status, integrations, unreachable} = ubergraph.computeImplicitTargets(sourceNode, requestedTargets, [botname])

	// send errors if we had any
	if (status !== 'succeeded') {
		const sourceNodeDebugName = sourceNode.debugName
		const errors = unreachable ? unreachable.map((node: Node) => 
					`Branch '${node.debugName}' is not reachable from '${sourceNodeDebugName}'`) : ['need more info!']

		return { computeResult: null, errors }
	}

	// report targets
	if (!integrations || integrations.size === 0) {
		return {computeResult: null, errors: []}
	}

	const merges = new Map<Branch, Branch[]>()
	for (const [initialEdge, furtherEdges] of integrations) {
		const edgeTargetBranch = (e: Edge) => {
			const branch = branchGraph.getBranch(e.targetName)
			if (!branch) {
				throw new Error('Unknown branch ' + e.targetName)
			}
			return branch
		}

		merges.set(edgeTargetBranch(initialEdge), furtherEdges.map(edgeTargetBranch))
	}

	return {computeResult: {merges, targets, flags: ri.flags}, errors}
}


export function computeTargets(
	sourceBranch: Branch,
	ubergraph: GraphAPI,
	cl: number,
	info: TargetInfo,
	commandArguments: string[],
	defaultTargets: string[],
	logger: ContextualLogger,
	allowIgnore: boolean,

	optTargetBranch?: Branch | null
) {


	const { computeResult, errors } = computeTargetsImpl(sourceBranch, ubergraph, cl, info.forceStompChanges, allowIgnore, commandArguments, defaultTargets, logger)

	if (errors.length > 0) {
		info.errors = errors
		return
	}

	if (!computeResult) {
		return
	}

	let { merges, flags, targets } = computeResult
	if (!merges) {
		// shouldn't get here - there will have been errors
		return
	}

	// Now that all targets from original changelist description are computed, compare to requested target branch, if applicable
	if (optTargetBranch) {
		// Ensure optTargetBranch is a (possibly indirect) target of this change
		if (!merges.has(optTargetBranch)) {
			errors.push(`Requested target branch ${optTargetBranch.name} not a valid target for this changelist`)
		}
		// Ensure we provide informative error around requesting to remerge when it should be null or skipped (though we shouldn't get in this state)
		else {
			const targetMergeMode = targets.get(optTargetBranch) || 'normal'
			if (targetMergeMode === 'null' || targetMergeMode === 'skip') {
				errors.push(`Invalid request to merge to ${optTargetBranch.name}: Changelist originally specified '${targetMergeMode}' merge for this branch`)
			}
			else {
				// Success!
				// Override targets with ONLY optTargetBranch
				merges = new Map([[optTargetBranch, merges.get(optTargetBranch)!]])
			}
		}
	}

	// compute final merge modes of all targets
	const mergeActions: MergeAction[] = []
	const allDownstream = new Set<Branch>()

	for (const [direct, indirectTargets] of merges.entries()) {
		const mergeMode : MergeMode = targets.get(direct) || 'normal'
		if (mergeMode === 'skip') {
			continue
		}
		allDownstream.add(direct)

		const furtherMerges: Target[] = []
		for (const branch of indirectTargets) {
			furtherMerges.push({branchName: branch.name, mergeMode: targets.get(branch) || 'normal'})
			allDownstream.add(branch)
		}

		mergeActions.push({branch: direct, mergeMode, furtherMerges, flags})
	}

	const branchGraph = sourceBranch.parent

	// add indirect forced branches to allDownstream
	for (const branch of [...allDownstream]) {
		branchGraph._computeReachableFrom(allDownstream, 'forceFlowTo', branch)
	}

	// send errors if we had any
	if (errors.length > 0) {
		info.errors = errors
	}

	// report targets
	if (mergeActions.length > 0) {
		info.targets = mergeActions
	}

	// provide info on all targets that should eventually be merged too
	if (allDownstream && allDownstream.size !== 0) {
		info.allDownstream = [...allDownstream]
	}
}