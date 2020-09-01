// Copyright Epic Games, Inc. All Rights Reserved.

import { setDefault } from '../common/helper';
import { ContextualLogger } from '../common/logger';
import { BranchSpec } from '../common/perforce';
import { Branch, BranchGraphInterface, TargetInfo } from './branch-interfaces';
import { BotConfig, BranchDefs, BranchGraphDefinition, EdgeOptions, EdgeProperties, IntegrationMethod, NodeOptions } from './branchdefs';
// temp, until I move computeTargets in here
import { NodeBot } from './nodebot';

type ConfigBlendMode = 'override' | 'accumulate'

export class BranchGraph implements BranchGraphInterface {

	branches: Branch[]

// should have separate type for processed edge info, but this will do for now
	edges: EdgeProperties[]


	botname: string

	filename: string
	config: BotConfig

	disabled = new Set<string>()

	constructor(botname: string, branchesForUnitTests?: any) {
		this.botname = botname.toUpperCase()

// hack for tests
if (botname === '__TEST__') {
	this.branches = branchesForUnitTests as Branch[]
	this.names = new Map

	for (var branch of branchesForUnitTests) {
		branch.parent = this
		branch.upperName = branch.name.toUpperCase()

		this.names.set(branch.upperName, branch)
	}

	this._computeReachable()
}


	}

	getBranch(name: string) {
		return this.names.get(name.toUpperCase())
	}
	getBranchNames() {
		return [...this.names.keys()].join(',')
	}

	private propagateBotPropertiesToNodes() {
		const propertiesToPropagate: [keyof BotConfig, keyof NodeOptions, ConfigBlendMode | null][] = [
			['excludeAuthors', 'excludeAuthors', 'override']
		]

		for (const [nameInBot, nameInNode, blendMode] of propertiesToPropagate) {
			const botProp = this.config[nameInBot]
			if (!botProp)
				continue

			for (const branch of this.branches) {
				const nodeProp = branch.config[nameInNode]

				if (nodeProp && nodeProp !== botProp) {
					if (blendMode === 'override') {
						// allow node setting to stay
						continue
					}
					throw new Error(`Bot and node properties conflict: ${nameInNode} '${botProp}' vs. '${nodeProp}'`)
				}

				(branch.config as any)[nameInNode] = botProp
			}
		}
	}

	private propagateSourcePropertiesToEdges() {
		// also flag for applying reverse entry too? would apply to branchspecs, not so simple for resolver
		const propertiesToPropagate: [keyof NodeOptions, keyof EdgeOptions, ConfigBlendMode | null][] = [
			['additionalSlackChannelForBlockages', 'additionalSlackChannel', null],
			['lastGoodCLPath', 'lastGoodCLPath', null],
			['disallowSkip', 'disallowSkip', null],
			['incognitoMode', 'incognitoMode', null],
			['excludeAuthors', 'excludeAuthors', 'override']
		]

		// propagate source node settings to edges
		for (const branch of this.branches) {
			for (const [nameInNode, nameInEdge, blendMode] of propertiesToPropagate) {
				const nodeProp = branch.config[nameInNode]
				if (!nodeProp)
					continue

				for (const target of branch.flowsTo) {
					const targetBranch = this.getBranch(target)
					if (!targetBranch) {
						// should have checked this already
						throw new Error('unknown target branch!')
					}

					const edgeProps = setDefault(branch.edgeProperties, targetBranch.upperName, {})

					const existing = edgeProps[nameInEdge]
					if (existing && existing !== nodeProp) {
						if (blendMode === 'override') {
							// allow edge setting to stay
							continue
						}
						throw new Error(`Node and edge properties conflict: ${nameInNode} '${nodeProp}' vs. '${existing}'`)
					}

					// typescript seems to misunderstand this, hence any cast
					(edgeProps as any)[nameInEdge] = nodeProp
				}
			}
		}
	}

	_initFromBranchDefInternal(branchGraph: BranchGraphDefinition | null) {
		this.branches = []
		this.names = new Map()

		if (!branchGraph) {
			return
		}

		// make branches
		for (let def of branchGraph.branches) {
			// skip disabled entries
			if (def.disabled) {
				this.disabled.add(def.name!.toUpperCase())
				continue
			}

			// add the entry
			if (def.name !== undefined) {
				// add a branch entry
				this.add(def.name, def)
			}
			else {
				throw new Error(`Unable to parse branch definition: ${JSON.stringify(def)}`)
			}
		}

		// set up branchspecs to use
		if (branchGraph.branchspecs) {
			for (let def of branchGraph.branchspecs) {
				// get the name
				let name = def.name
				if (!name) throw new Error("unnamed branch spec")

				// load the referenced branches
				let fromBranch = this.getBranch(def.from)
				let toBranch = this.getBranch(def.to)
				if (!fromBranch || !toBranch)
				{
					if (this.disabled.has(def.from.toUpperCase()) || this.disabled.has(def.to.toUpperCase()))
						continue
					if (!fromBranch) throw new Error("From-Branch "+def.from+" not found. Referenced by branchspec "+name)
					if (!toBranch) throw new Error("To-Branch "+def.to+" not found. Referenced by branchspec "+name)
				}

				// add forward and reverse entries
				if (!fromBranch.config.ignoreBranchspecs) {
					fromBranch.branchspec.set(toBranch.upperName, {name: name, reverse: false})
				}

				if (!toBranch.config.ignoreBranchspecs) {
					toBranch.branchspec.set(fromBranch.upperName, {name: name, reverse: true})
				}
			}
		}

		// process additional edge properties
		if (branchGraph.edges) {
			for (const edge of branchGraph.edges) {
				const source = this.getBranch(edge.from)
				const targetName = this.getBranch(edge.to)!.upperName
				if (source!.flowsTo.indexOf(targetName) < 0) {
					throw new Error(`No edge matching property ${edge.from} -> ${edge.to}`)
				}

				source!.edgeProperties.set(targetName, edge)
			}
		}

		this.edges = branchGraph.edges || []
		this.propagateBotPropertiesToNodes()
		this.propagateSourcePropertiesToEdges()

		// log final branches
		this.finish()
	}

	private static _getBoolConfig(fromOptions?: boolean, fromConfig?: boolean): boolean {
		return !!(fromOptions === undefined ? fromConfig : fromOptions)
	}

	private add(name: string, options: NodeOptions) {
		BranchDefs.checkName(name)

		// make a branch data entry
		let nameUpper = name.toUpperCase()
		let branch: Branch = {
			name: name,
			parent: this,
			workspace: options.workspace || null,
			branchspec: new Map<string, BranchSpec>(),
			edgeProperties: new Map(),
			upperName: nameUpper,
			aliases: [nameUpper],
			config: options,
			depot: "", // will compute
			rootPath: options.rootPath || "",
			badgeProject: options.badgeProject || null,
			isDefaultBot: BranchGraph._getBoolConfig(options.isDefaultBot, this.config.isDefaultBot),
			maxFilesPerIntegration: options.maxFilesPerIntegration || this.config.maxFilesPerIntegration,
			emailOnBlockage: BranchGraph._getBoolConfig(options.emailOnBlockage, this.config.emailOnBlockage),
			flowsTo: options.flowsTo || [],
			notify: (options.notify || []).concat(this.config.globalNotify),
			forceFlowTo: [], // will compute
			defaultFlow: options.defaultFlow || [],
			blockAssetTargets: new Set<string>(),
			enabled: !options.disabled,
			whitelist: options.whitelist || [],
			resolver: options.resolver || null,
			convertIntegratesToEdits: (options.integrationMethod || this.config.defaultIntegrationMethod) === IntegrationMethod.CONVERT_TO_EDIT,
			visibility: options.visibility || this.config.visibility,
			get isMonitored() { return !!(this.bot && this.bot.isRunning) }
		}

		// non-enumerable properties so they don't get logged
		Object.defineProperty(branch, 'bot', { enumerable: false, value: null, writable: true })
		Object.defineProperty(branch, 'parent', { enumerable: false, value: this, writable: false })

		// lower-case notify names so we can more easily check for uniqueness
		branch.notify.map(str => { return str.toLowerCase() })

		const depot = options.streamDepot || this.config.defaultStreamDepot
		const streamName = options.streamName || name
		const computedStreamRoot = depot && streamName ? `//${depot}/${streamName}` : null

		// compute root path if not specified
		if (branch.rootPath === "") {
			if (!computedStreamRoot) {
				throw new Error(`Missing rootPath and no streamDepot+streamName defined for branch ${name}.`)
			}

			branch.depot = depot!
			branch.rootPath = computedStreamRoot + (options.streamSubpath || '/...')
			branch.stream = computedStreamRoot
		}
		else {
			if (!branch.rootPath.startsWith('//') || !branch.rootPath.endsWith('/...')) {
				throw new Error(`Branch rootPath not in '//<something>/...'' format: ${branch.rootPath}`)
			}

			const depotMatch = branch.rootPath.match(/\/\/([^/]+)\//)
			if (!depotMatch || !depotMatch[1]) {
				throw new Error(`Cannot find depotname in ${branch.rootPath}`)
			}

			branch.depot = depotMatch[1]
		}

		// make sure flowsTo and defaultFlow are all upper case
		for (let i=0; i<branch.flowsTo.length; ++i) {
			branch.flowsTo[i] = branch.flowsTo[i].toUpperCase()
		}

		if (options.blockAssetFlow) {
			for (const flow of options.blockAssetFlow) {
				const flowUpper = flow.toUpperCase()
				if (branch.flowsTo.indexOf(flowUpper) === -1) {
					throw new Error(`Block asset flow '${flow}' not in flowsTo list`)
				}
				branch.blockAssetTargets.add(flowUpper)
			}
		}

		for (let i=0; i<branch.defaultFlow.length; ++i) {
			branch.defaultFlow[i] = branch.defaultFlow[i].toUpperCase()
		}

		// apply forced merge targets
		const forceFlow = (options.forceAll ? options.flowsTo : options.forceFlowTo) || []
		for (const target of forceFlow) {
			const upperTarget = target.toUpperCase()

			// make sure anything in force is in flowsTo
			if (branch.flowsTo.indexOf(upperTarget) < 0) {
				throw new Error(`Cannot force ${branch.name} to flow to ${upperTarget} because it is not in the flowsTo list`)
			}
			branch.forceFlowTo.push(upperTarget)

			// make sure anything in forceFlowTo also exists in defaultFlow
			if (branch.defaultFlow.indexOf(upperTarget) < 0) {
				branch.defaultFlow.push(upperTarget)
			}
		}

		// add to branches
		this.branches.push(branch)

		// add to names
		this.names.set(name.toUpperCase(), branch)

// alias problem: we're storing flows by name provided, which may be an alias
//	- works, because above just checks names (e.g. flowsTo and forceFlow)
//		and then just uses those to look up branch
//	- doesn't work for checking names: alias won't match branch.upperName
//	real solution: pre-process all branches, set up aliases, finalise flows

// side node, don't really need separate branchdefs file, although it was good
// temporarily. Probably worth rewriting entirely, given current knowledge.

		// automatically alias streamName if specified
		if (options.streamName && !this.config.noStreamAliases) {
			this.alias(name, options.streamName)
		}

		// set up aliases
		if (options.aliases) {
			for (let alias of options.aliases) {
				this.alias(name, alias)
			}
		}
	}

	alias(name: string, alias: string) {
		// check the branch name
		let branch = this.getBranch(name)
		if (!branch) {
			throw new Error("Unable to alias '"+name+"' because it is not a branch")
		}

		// make sure the alias isn't taken
		let upperAlias = alias.toUpperCase()
		let existing = this.names.get(upperAlias)
		if (existing) {
			if (existing === branch) {
				return // idempotent
			}
			throw new Error("Alias "+alias+" is already mapped to "+existing.name)
		}

		// check that alias is a valid name
		BranchDefs.checkName(alias)

		// alias as upper case
		if (branch.aliases.indexOf(upperAlias) < 0) {
			branch.aliases.push(upperAlias)
		}
		this.names.set(upperAlias, branch)
	}

	finish() {
		this._checkFlow()
		this._computeReachable()
		this._computeBranchesMappingToCommonStreams()
	}

	/** returns null if unique or non-stream, otherwise list including branch itself */
	getBranchesMonitoringSameStreamAs(branch: Branch) {
		return this.branchesWithCommonStreams.get(branch) || null
	}

	resolveBranch(candidate: string) {
		candidate = candidate.trim()
		if (!candidate.startsWith('//')) {
			return this.getBranch(candidate)
		}

		for (const branch of this.branches) {
			if (branch.stream === candidate) {
				return branch
			}
		}
		return null
	}

	computeImplicitTargets(errors: string[], source: Branch, targets: Branch[]) {

		// temp: hijack test code until we refactor NodeBot
		return NodeBot.computeImplicitTargets(source, source.parent, errors, new Set(targets), new Set()) /* not supporting skipping yet - should check can divert route this way */

	}

	private _fixFlow(list: string[], branchName: string) {
		const output = []
		for (const ref of list) {
			const refBranch = this.getBranch(ref)
			if (!refBranch) {
				if (this.disabled.has(ref.toUpperCase()))
					continue
				throw new Error(`Branch ${branchName} flows to invalid branch/alias ${ref}`)
			}
			output.push(refBranch.upperName)
		}
		return output
	}

	private _checkFlow() {
		for (const branch of this.branches) {
			branch.defaultFlow = this._fixFlow(branch.defaultFlow, branch.name)
			branch.flowsTo = this._fixFlow(branch.flowsTo, branch.name)
			branch.forceFlowTo = this._fixFlow(branch.forceFlowTo, branch.name)
		}
	}

	private _computeBranchesMappingToCommonStreams() {
		const streamToBranches = new Map<string, Branch[]>()
		for (const branch of this.branches) {
			if (branch.stream) {
				setDefault(streamToBranches, branch.stream, []).push(branch)
			}
		}

		this.branchesWithCommonStreams.clear()
		for (const branches of streamToBranches.values()) {
			if (branches.length > 1) {
				for (const branch of branches) {
					this.branchesWithCommonStreams.set(branch, branches)
				}
			}
		}
	}

	private _computeReachable() {
		for (const branch of this.branches) {
			// compute reachable
			const reach = this._computeReachableFrom(new Set([branch]), 'flowsTo', branch)
			reach.delete(branch.upperName) // loops are allowed but we don't want them in the flow list
			branch.reachable = Array.from(reach).sort()

			// compute forced downstream
			const forced = this._computeReachableFrom(new Set([branch]), 'forceFlowTo', branch)
			branch.forcedDownstream = Array.from(forced).sort()
			if (forced.has(branch.upperName)) {
				throw new Error(`Branch ${branch.name} has a forced flow loop to itself. ` + JSON.stringify(branch.forcedDownstream))
			}
		}
	}

	// public for use by NodeBot - maybe move to a utility
	public _computeReachableFrom(visited: Set<Branch>, flowKey: string, branch: Branch) {
		// start with flows to
		const directFlow: Set<string> = (branch as any)[flowKey]
		const reachable = new Set(directFlow)

		// expand reach list
		for (const target of directFlow) {
			const targetBranch = this.getBranch(target)!
			if (visited.has(targetBranch)) {
				continue
			}
			visited.add(targetBranch)

			// what is reachable from here
			const reach = this._computeReachableFrom(visited, flowKey, targetBranch)
			for (const upperName of reach) {
				reachable.add(upperName)
			}
		}

		return reachable
	}

	private names = new Map<string, Branch>()
	private branchesWithCommonStreams = new Map<Branch, Branch[]>()
}



//  _______        _       
// |__   __|      | |      
//    | | ___  ___| |_ ___ 
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/

// for unit tests!


function makeBranchDataForComputeTargetTest(nodeStrs: any) {
	return nodeStrs.map((str: string, index: number) => ({
		name: String.fromCharCode('A'.charCodeAt(0) + index),
		flowsTo: [...str.toUpperCase()],
		forceFlowTo: [...str].filter(x => x.toUpperCase() === x).map(x => x.toUpperCase()),
	}))
	.map((branch: any) => ({
		name: branch.name, flowsTo: branch.flowsTo, forceFlowTo: branch.forceFlowTo,
		nameUpper: branch.name.toUpperCase(),
		defaultFlow: [], whitelist: [], rootPath: '/x/y/z'
	}))
}

function computeTargetTestActionToShortString(arg: any) {
	const mergeMode = arg.mergeMode
	const br = arg.targetName || arg.branch.name
	return mergeMode === 'normal' ? br :
		mergeMode === 'skip' ? '-' + br :
		mergeMode === 'null' ? '!' + br : '@' + br
}

function runComputeTargetTest(targets: string[], self: string, ...flows: string[]) {
	const branchGraph = new BranchGraph('__TEST__', makeBranchDataForComputeTargetTest(flows))
	const selfBranch = branchGraph.getBranch(self)!
	const results: TargetInfo = {author: 'author', forceStompChanges: false, sendNoShelfEmail: false}
	NodeBot.computeTargets(selfBranch, selfBranch.parent, results, targets, selfBranch.forceFlowTo, new ContextualLogger('runComputeTargetTest'))
	const result = results.errors || (results.targets ?
		results.targets.map(action => computeTargetTestActionToShortString(action) + ': ' + action.furtherMerges.map(x => computeTargetTestActionToShortString(x)).join(', '))
		: ['no targets'])
	return result
}

function runComputeTargetTestStr(def: string[]) {
	const targetBits = def[0].split('-') // up to three section in target (1st) string: normal, skip, null


	let targets = [...targetBits[0]]
	if (targetBits.length > 1) {
		targets = [...targets, ...[...targetBits[1]].map(x => '-' + x)]
	}
	if (targetBits.length > 2) {
		targets = [...targets, ...[...targetBits[2]].map(x => '!' + x)]
	}
	return runComputeTargetTest(targets, def[1], ...def.slice(2))
}


export function runTests(parentLogger: ContextualLogger) {
	const unitTestLogger = parentLogger.createChild('BranchGraph')
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
		[['de', 'h', 'hc', 'hd', 'deFg', 'hbc', 'c', 'c', 'c', 'abd'], 'D:CE'],
		[['de', 'h', 'bd', 'hc', 'hd', 'eFg', 'hbc', 'c', 'c', 'c'], 'C:DE'],
		[['db', 'h', 'abd', 'hc', 'hd', 'deFg', 'hbc', 'c', 'c', 'c'], 'C:DEB'],
		[['cg', 'a', 'be', 'acd', 'b', 'b', 'aFg', 'e', 'e'], 'B:C;E:G'],
		[['cf', 'a', 'bE', 'acd', 'b', 'b', 'aFg', 'e', 'e'], 'E:F;B:C'],
		[['c-e', 'a', 'bE', 'acd', 'b', 'b', 'aFg', 'e', 'e'], 'B:C'],
		[['cf-b', 'a', 'bE', 'acd', 'b', 'b', 'aFg', 'e', 'e'], null],
		[['fg', 'h', 'abd', 'hC', 'hd', 'defg', 'hbc', 'c', 'c', 'c'], 'C:DGF'],
		[['dge', 'h', 'abd', 'hC', 'hd', 'defg', 'hbc', 'c', 'c', 'c'], 'C:DGE'],
		[['e', 'i', 'abcd', 'Ie', 'if', 'iG', 'ih', 'a', 'b', 'c', 'D'], 'D:GBE']
	]

	let ran = 0, success = 0, fail = 0
	for (const [test, expected] of tests) {
		const result = runComputeTargetTestStr(test)
		++ran
		let expectedOnFail
		if (expected) {
			if (result.join(';').replace(/\s|,/g, '') === expected)
				++success
			else {
				expectedOnFail = expected
				++fail
			}
		}
		const out = result.join('; ')
		if (expectedOnFail) {
			unitTestLogger.error(`MISMATCH!!!    ${out} vs ${expectedOnFail}`)
		}
	}
	unitTestLogger.info(`Target tests: ran ${ran} tests, ${success} matched, ${fail} failed`)
	return fail
}
