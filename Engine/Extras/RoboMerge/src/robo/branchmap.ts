// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import {Branch, BranchMapInterface} from './branch-interfaces'
import {BotConfig, BranchDefs, BranchOptions, BranchMapDefinition, IntegrationMethod} from './branchdefs'

export class BranchMap implements BranchMapInterface {

	branches: Branch[]
	botname: string

	filename: string
	config: BotConfig

	disabled = new Set<string>()

	reloadAsyncListeners = new Set<Function>()

	constructor(botname: string, branchesForUnitTests?: any) {
		this.botname = botname.toUpperCase()

// hack for tests
if (botname === '__TEST__') {
	this.branches = branchesForUnitTests as Branch[]
	this.names = new Map

	// console.log(data)
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

	_initFromBranchMapsObjectInternal(branchMap: BranchMapDefinition | null) {
		this.branches = []
		this.names = new Map()

		if (!branchMap) {
			return
		}

		// make branches
		for (let def of branchMap.branches) {
			// skip disabled entries
			if (def.disabled) {
				this.disabled.add(def.name.toUpperCase())
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
		if (branchMap.branchspecs) {
			for (let def of branchMap.branchspecs) {
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

				// add a forward and reverse entry
				(fromBranch.branchspec as any)[toBranch.upperName] = {name: name, reverse: false},
				(toBranch.branchspec as any)[fromBranch.upperName] = {name: name, reverse: true}
			}
		}

		// log final branches
		this.finish()
	}

	private static _getBoolConfig(fromOptions?: boolean, fromConfig?: boolean): boolean {
		return !!(fromOptions === undefined ? fromConfig : fromOptions)
	}

	// 116
	add(name: string, options: BranchOptions) {
		BranchDefs.checkName(name)

		// make a branch data entry
		let nameUpper = name.toUpperCase()
		let branch: Branch = {
			name: name,
			parent: this,
			workspace: options.workspace || null,
			branchspec: {},
			upperName: nameUpper,
			aliases: [nameUpper],
			config: options,
			rootPath: options.rootPath || "",
			pathsToMonitor: options.pathsToMonitor || null,
			badgeProject: options.badgeProject || null,
			isDefaultBot: BranchMap._getBoolConfig(options.isDefaultBot, this.config.isDefaultBot),
			excludeAuthors: options.excludeAuthors || this.config.excludeAuthors || [],
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

			branch.rootPath = computedStreamRoot + (options.streamSubpath || '/...')
			branch.stream = computedStreamRoot
		}
		else {
			if (!branch.rootPath.startsWith('//') || !branch.rootPath.endsWith('/...')) {
				throw new Error(`Branch rootPath not in '//<something>/...'' format: ${branch.rootPath}`)
			}
		}

		if (branch.pathsToMonitor && computedStreamRoot) {
			for (let n = 0; n < branch.pathsToMonitor.length; ++n) {
				const path = branch.pathsToMonitor[n]
				if (path[0] !== '/') {
					// relative, assume relative to stream root (must be absolute if rootPath overridden)
					branch.pathsToMonitor[n] = `${computedStreamRoot}/${path}`
				}
			}
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
		const forceFlow = options.forceAll ? options.flowsTo : (options.forceFlowTo || [])
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

	// 214
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

	finish() { // 245
		this._checkFlow()
		this._computeReachable()
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

	// public for use by branchbot - maybe move to a utility
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
}











//  _______        _       
// |__   __|      | |      
//    | | ___  ___| |_ ___ 
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/

// for unit tests!
import {BranchBot} from './branchbot'
import {ChangeInfo} from './branch-interfaces'


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
	const branchMap = new BranchMap('__TEST__', makeBranchDataForComputeTargetTest(flows))
	const info: ChangeInfo = {owner: 'owner', author: 'author', branch: {} as Branch, cl: 0, source_cl: 0, isManual: false, source: '', description: '', propagatingNullMerge: false}

	const selfBranch = branchMap.getBranch(self)!
	new BranchBot('__TEST__', selfBranch)._computeTargets(info, targets, selfBranch.forceFlowTo)
	const result = info.errors || (info.targets ?
		info.targets.map(action => computeTargetTestActionToShortString(action) + ': ' + action.furtherMerges.map(x => computeTargetTestActionToShortString(x)).join(', '))
		: ['no targets'])
	return result
}

function runComputeTargetTestStr(def: any) {
	const targetBits = def[0].split('-')
	let targets = [...targetBits[0]]
	if (targetBits.length > 1) {
		targets = [...targets, ...[...targetBits[1]].map(x => '-' + x)]
	}
	if (targetBits.length > 2) {
		targets = [...targets, ...[...targetBits[2]].map(x => '!' + x)]
	}
	return runComputeTargetTest(targets, def[1], ...def.slice(2))
}


export function runTests() {
	const tests = [
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
			console.log(`MISMATCH!!!    ${out} vs ${expectedOnFail}`)
		}
	}
	console.log(`Target tests: ran ${ran} tests, ${success} matched, ${fail} failed`)
	return fail
}
