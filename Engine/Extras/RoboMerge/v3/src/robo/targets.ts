// Copyright Epic Games, Inc. All Rights Reserved.

import { Branch, BranchGraphInterface, MergeAction, MergeMode } from './branch-interfaces';
import { PendingChange, Target, TargetInfo } from './branch-interfaces';
import { ContextualLogger } from '../common/logger';
import { setDefault } from '../common/helper';

export function getIntegrationOwner(targetBranch: Branch, overriddenOwner?: string): string | null
export function getIntegrationOwner(pending: PendingChange): string
export function getIntegrationOwner(arg0: Branch | PendingChange, overriddenOwner?: string) {
	// order of priority for owner:

	//  1) manual requester - if this change was manually requested, use the requestor (now set as the change owner)
	//	2) resolver - need resolver to take priority, even over reconsider, since recon might be from branch with
	//					multiple targets, so instigator might not even know about target with resolver
	//	3) reconsider
	//	4) propagated/manually added tag
	//	5) author - return null here for that case

	const pending = (arg0 as PendingChange)

	const branch = pending.action ? pending.action.branch : (arg0 as Branch)
	const owner = pending.change ? pending.change.owner : overriddenOwner

	// Manual requester
	if ( (pending.action && pending.action.flags.has('manual')) ||
		(pending.change && pending.change.forceCreateAShelf)
	) {
		return owner
	}
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
	source: string | null = null
	owner: string | null = null
	authorTag: string | null = null

	descFinal: string[] = []

	propagatingNullMerge = false
	hasOkForGithubTag = false
	useDefaultFlow = true
	arguments: string[] = []

	expandedMacros: string[] = []
	expandedMacroLines: string[] = []

	constructor(
	private isDefaultBot: boolean, 
	private graphBotName: string, 
	private cl: number, 
	private aliasUpper: string,
	private macros: {[name: string]: string[]}

	) {
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
		
		if (ws || !command.startsWith('ROBOMERGE')) {
			// check for commands to neuter
			if (NEUTER_COMMANDS.indexOf(command) >= 0) {
				// neuter codereviews so they don't spam out again
				this.descFinal.push(`${ws}[${command}] ${value}`)
			}
			
			else if (command.indexOf('REVIEW-') === 0) {
				// remove swarm review numbers entirely (The can't be neutered and generate emails)
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

			// completely ignore bare ROBOMERGE tags if we're not the default bot (should not affect default flow)
			if (this.isDefaultBot) {
				this.useDefaultFlow = false
				for (const target of parseTargetList(value)) {
					const macroLines = this.macros[target.toLowerCase()]
					if (macroLines) {
						this.expandedMacroLines = [...this.expandedMacroLines, ...macroLines]
					}
					else {
						this.arguments.push(target)
					}
				}
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

				this.arguments = [...this.arguments, ...parseTargetList(value)]
			}
			else {
				// allow commands to other bots to propagate!
				this.descFinal.push(line)
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
				 command !== 'ROBOMERGE-COMMAND') {
			// add unknown command as a branch name to force a syntax error
			this.arguments = [command]
		}
	}
}

export function parse(
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

export function computeImplicitTargets(
		sourceBranch: Branch,
		branchGraph: BranchGraphInterface,
		outErrors: string[],
		targets: Set<Branch>,
		skipBranches: Set<Branch>
	) {
	const merges = new Map<Branch, Branch[]>()

	// parallel flood the flowsTo graph to find all specified targets

	// branchPaths is an array of 3-tuples, representing the branches at the current boundary of the flood
	//	- first entry is the branch
	//	- second and third entries make up the path to get to that branch
	//		- second entry, if non-empty, has the path up to the most recent explicit target
	//		- third entry has any remaining implicit targets

	const branchPaths: [Branch, Branch[], Branch[]][] = [[sourceBranch, [], [sourceBranch]]]

	const seen = new Set([sourceBranch])

	while (targets.size !== 0 && branchPaths.length !== 0) {

		const [sourceBranch, explicit, implicit] = branchPaths.shift()!

		// flood the graph one step to include all unseen direct flowsTo nodes of one branch
		let anyUnseen = false
		if (sourceBranch.flowsTo && !skipBranches.has(sourceBranch)) {
			for (const branchName of sourceBranch.flowsTo) {
				const branch = branchGraph.getBranch(branchName)
				if (!branch) {
					// shouldn't happen: branch map takes care of validating flow graph
					throw "unknown branch in flowsTo"
				}
				if (seen.has(branch))
					continue
				seen.add(branch)
				anyUnseen = true
				if (targets.has(branch)) {
					branchPaths.unshift([branch, [...explicit, ...implicit, branch], []])
				}
				else {
					branchPaths.push([branch, explicit, [...implicit, branch]])
				}
			}
		}

		// store the calculated merge data if we've exhausted a sub-tree of the graph
		if (!anyUnseen && explicit.length > 1) {
			const directTarget = explicit[1]

			const indirectTargets = setDefault(merges, directTarget, [])
			targets.delete(directTarget)
			for (let index = 2; index < explicit.length; ++index) {
				const indirectTarget = explicit[index]
				if (indirectTargets.indexOf(indirectTarget) === -1) {
					indirectTargets.push(indirectTarget)
				}
				targets.delete(indirectTarget)
			}
		}
	}

	if (targets.size > 0) {
		const branchesMonitoringSameStream = branchGraph.getBranchesMonitoringSameStreamAs(sourceBranch)

	targetLoop:
		for (const unreachableTarget of targets) {

			// don't error if target reachable by another bot monitoring this stream
			if (branchesMonitoringSameStream) {
				for (const other of branchesMonitoringSameStream) {
					if (other.reachable!.indexOf(unreachableTarget.upperName) !== -1) {
						continue targetLoop
					}
				}
			}

			outErrors.push(`Branch '${unreachableTarget.name}' is not reachable from '${getNodeBotFullName(branchGraph.botname, sourceBranch.name)}'`)
		}
	}

	return merges
}

// public so it can be called from unit tests
export function computeTargets(sourceBranch: Branch, branchGraph: BranchGraphInterface, info: TargetInfo, requestedTargetNames: string[],
	defaultTargets: string[], logger: ContextualLogger, optTargetBranch?: Branch | null) {
	const errors: string[] = []
	const flags = new Set<string>()

	// list of branch names and merge mode (default targets first, so merge mode gets overwritten if added explicitly)
	const defaultMergeMode: MergeMode = info.forceStompChanges ? 'clobber' : 'normal'
	const requestedMerges: [string, MergeMode][] = defaultTargets.map(name => [name, defaultMergeMode] as [string, MergeMode])

	// Parse prefixed branch names and flags
	for (const rawTargetName of requestedTargetNames) {
		const rawTargetLower = rawTargetName.toLowerCase()

		// check for 'ignore' and remap to #ignore flag
		if (ALLOWED_RAW_FLAGS.indexOf(rawTargetLower) >= 0)
		{
			flags.add(FLAGMAP[rawTargetLower]);
			continue;
		}

		// If forceStompChanges, add the target as a stomp if it isn't a flagged target
		if (info.forceStompChanges && ['!', '-', '$', '#'].indexOf(rawTargetName.charAt(0)) === -1) {
			requestedMerges.push([rawTargetName, 'clobber'])
			continue
		}

		// see if this has a modifier
		switch (rawTargetName.charAt(0))
		{
		default:
			requestedMerges.push([rawTargetName, 'normal'])
			break
		
		case '!':
			requestedMerges.push([rawTargetName.substr(1), 'null'])
			break;

		case '-':
			requestedMerges.push([rawTargetName.substr(1), 'skip'])
			break

		case '$':
		case '#':
			// set the flag and continue the loop
			const flagname = FLAGMAP[rawTargetLower.substr(1)]
			if (flagname) {
				flags.add(flagname)
			}
			else {
				logger.warn(`Ignoring unknown flag "${rawTargetName}" from ${info.author}`)
			}
			break
		}
	}

	if (flags.has('null')) {
		for (const branchName of sourceBranch.forceFlowTo) {
			requestedMerges.push([branchName, 'null'])
		}
	}

	// compute the targets map
	const mergeActions: MergeAction[] = []
	let allDownstream: Set<Branch> | null = null
	if (!flags.has('ignore')) {
		const skipBranches = new Set<Branch>()
		const targets = new Map<Branch, MergeMode>()

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

			if (targetBranch.whitelist.length !== 0) {
				const owner = getIntegrationOwner(targetBranch, info.owner) || info.author
				if (targetBranch.whitelist.indexOf(owner) < 0) {
					errors.push(`${owner} is not on the whitelist to merge to "${targetBranch.name}"`)
					continue
				}
			}

			if (mergeMode === 'skip') {
				skipBranches.add(targetBranch)
			}

			// note: this allows multiple mentions of same branch, with flag of last mention taking priority. Could be an error instead
			targets.set(targetBranch, mergeMode)
		}

		let merges: Map<Branch, Branch[]> | null = null
		if (targets.size > 0) {
			merges = computeImplicitTargets(sourceBranch, branchGraph, errors, new Set(targets.keys()), skipBranches)
		}

		// Now that all targets from original changelist description are computed, compare to requested target branch, if applicable
		if (optTargetBranch) {
			// Ensure optTargetBranch is a (possibly indirect) target of this change
			if (!merges || !merges.has(optTargetBranch)) {
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

		if (merges && merges.size > 0 && errors.length === 0) {
			// compute final merge modes of all targets
			allDownstream = new Set<Branch>()

			for (const [direct, indirectTargets] of merges.entries()) {
				const mergeMode : MergeMode = targets.get(direct) || 'normal'
				if (mergeMode === 'skip') {
					continue
				}
				allDownstream.add(direct)

				const furtherMerges: Target[] = []
				for (const branch of indirectTargets) {
					furtherMerges.push({branch, mergeMode: targets.get(branch) || 'normal'})
					allDownstream.add(branch)
				}

				mergeActions.push({branch: direct, mergeMode, furtherMerges, flags})
			}

			// add indirect forced branches to allDownstream
			for (const branch of [...allDownstream]) {
				branchGraph._computeReachableFrom(allDownstream, 'forceFlowTo', branch)
			}
		}
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
