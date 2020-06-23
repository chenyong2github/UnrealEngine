// Copyright Epic Games, Inc. All Rights Reserved.

const jsonlint: any = require('jsonlint')

const RESERVED_BRANCH_NAMES = ['NONE', 'DEFAULT', 'IGNORE', ''];

export interface BotConfig {
	defaultStreamDepot: string | null
	defaultIntegrationMethod: string | null
	isDefaultBot: boolean
	noStreamAliases: boolean
	globalNotify: string[]
	checkIntervalSecs: number
	excludeAuthors: string[]
	visibility: string[] | string
	slackChannel: string
	pauseAtCl: number | null
}

export interface BranchBase {
	name: string

	rootPath: string
	isDefaultBot: boolean
	excludeAuthors: string[] // if present, completely overrides BotConfig

	notify: string[]
	flowsTo: string[]
	forceFlowTo: string[]
	defaultFlow: string[]
	whitelist: string[]
	resolver: string | null
	aliases: string[]
	badgeProject: string | null

	pathsToMonitor: string[] | null
}

export class IntegrationMethod {
	static NORMAL = 'normal'
	static CONVERT_TO_EDIT = 'convert-to-edit'

	static all() {
		const cls: any = IntegrationMethod
		return Object.getOwnPropertyNames(cls)

			.filter(x => typeof cls[x] === 'string' && cls[x] !== cls.name)
			.map(x => cls[x])
	}
}

// probably not right to extend BranchBase, because so much is optional

export interface BranchOptions extends BranchBase {
	disabled?: boolean
	integrationMethod?: string
	forceAll?: boolean
	visibility?: string[] | string
	blockAssetFlow?: string[]

	streamDepot?: string
	streamName?: string
	streamSubpath?: string
	workspace?: (string | null)

	initialCL?: number
	pauseAtCl?: number
}

export interface BranchSpecDefinition {
	from: string
	to: string
	name: string
}

export interface BranchMapDefinition {
	isDefaultBot?: boolean
	globalNotify?: string[]
	branches: BranchOptions[]
	branchspecs?: BranchSpecDefinition[] 
}

// for now, just validate branch file data, somewhat duplicating BranchMap code
// eventually, switch branch map to using a separate class defined here for the branch definitions


// return branchMap and errors?

interface ParseResult {
	branchMapDef: BranchMapDefinition | null
	config: BotConfig
}

export class BranchDefs {
	static checkName(name: string) {
		if (!name.match(/^[-a-zA-Z0-9_\.]+$/))
			return `Names must be alphanumeric, dash, underscore or dot: '${name}'`

		for (const reserved of RESERVED_BRANCH_NAMES) {
			if (name.toUpperCase() === reserved) {
				return `'${name}' is a reserved branch name`
			}
		}
		return undefined
	}

	private static checkValidIntegrationMethod(outErrors: string[], method: string, branchName: string) {
		if (IntegrationMethod.all().indexOf(method.toLowerCase()) === -1) {
			outErrors.push(`Unknown integrationMethod '${method}' in '${branchName}'`)
		}
	}

	static parseAndValidate(outErrors: string[], branchSpecsText: string): ParseResult {
		const defaultConfigForWholeBot: BotConfig = {
			defaultStreamDepot: null,
			defaultIntegrationMethod: null,
			isDefaultBot: false,
			noStreamAliases: false,
			globalNotify: [],
			checkIntervalSecs: 30.0,
			excludeAuthors: [],
			visibility: ['fte'],
			slackChannel: '',
			pauseAtCl: null
		}

		let branchMap
		try {
			branchMap = <BranchMapDefinition>jsonlint.parse(branchSpecsText)
		}
		catch (err) {
			outErrors.push(err)
			return {branchMapDef: null, config: defaultConfigForWholeBot}
		}

		// copy config values
		for (let key of Object.keys(defaultConfigForWholeBot)) {
			let value = (<any>branchMap)[key]
			if (value !== undefined)
				(<any>defaultConfigForWholeBot)[key] = value
		}

		if (defaultConfigForWholeBot.defaultIntegrationMethod) {
			BranchDefs.checkValidIntegrationMethod(outErrors, defaultConfigForWholeBot.defaultIntegrationMethod, 'config')
		}

		const names = new Map<string, string>()

		const branchesFromJSON = branchMap.branches
		branchMap.branches = []

		// Check for duplicate branch names
		for (const def of branchesFromJSON) {
			if (!def.name) {
				outErrors.push(`Unable to parse branch definition: ${JSON.stringify(def)}`)
				continue
			}

			branchMap.branches.push(def)

			const nameError = BranchDefs.checkName(def.name)
			if (nameError) {
				outErrors.push(nameError)
				continue
			}

			const upperName = def.name.toUpperCase()
			if (names.has(upperName)) {
				outErrors.push(`Duplicate branch name '${upperName}'`)
			}
			else {
				names.set(upperName, upperName)
			}
		}

		// Check for duplicate aliases
		const addAlias = (upperBranchName: string, upperAlias: string) => {
			if (!upperAlias) {
				outErrors.push(`Empty alias for '${upperBranchName}'`)
				return
			}

			const nameError = BranchDefs.checkName(upperAlias)
			if (nameError) {
				outErrors.push(nameError)
				return
			}

			const existing = names.get(upperAlias)
			if (existing && existing !== upperBranchName) {
				outErrors.push(`Duplicate alias '${upperAlias}' for '${existing}' and '${upperBranchName}'`)
			}
			else {
				names.set(upperAlias, upperBranchName)
			}
		}

		for (const def of branchMap.branches) {

			const upperName = def.name.toUpperCase()
			if (def.aliases) {
				for (const alias of def.aliases) {
					addAlias(upperName, alias.toUpperCase())
				}
			}

			if (def.streamName && !defaultConfigForWholeBot.noStreamAliases) {
				addAlias(upperName, def.streamName.toUpperCase())
			}

			if (def.integrationMethod) {
				BranchDefs.checkValidIntegrationMethod(outErrors, def.integrationMethod, def.name)
			}
		}

		// Check flow
		for (const def of branchMap.branches) {
			const flowsTo = new Set()
			if (def.flowsTo) {
				if (!Array.isArray(def.flowsTo)) {
					outErrors.push(`'${def.name}'.flowsTo is not an array`)
				}
				else for (const to of def.flowsTo) {
					const branchName = names.get(to.toUpperCase())
					if (branchName) {
						flowsTo.add(branchName)
					}
					else {
						outErrors.push(`'${def.name}' flows to unknown branch/alias '${to}'`)				
					}
				}
			}

			if (def.forceFlowTo) {
				if (!Array.isArray(def.forceFlowTo)) {
					outErrors.push(`'${def.name}'.forceFlowTo is not an array`)
				}
				else for (const to of def.forceFlowTo) {
					const branchName = names.get(to.toUpperCase())
					if (!branchName) {
						outErrors.push(`'${def.name}' force flows to unknown branch/alias '${to}'`)				
					}
					else if (!flowsTo.has(branchName)) {
						outErrors.push(`'${def.name}' force flows but does not flow to '${to}'`)				
					}
				}
			}
		}

		// Check branchspecs for valid branches
		if (branchMap.branchspecs) {
			for (const spec of branchMap.branchspecs) {
				if (!spec.from || !spec.to) {
					outErrors.push(`Invalid branchspec ${spec.name} (requires both to and from fields)`)
				}

				if (!names.has(spec.from.toUpperCase())) {
					outErrors.push(`From-Branch ${spec.from} not found in branchspec ${spec.name}`)
				}

				if (!names.has(spec.to.toUpperCase())) {
					outErrors.push(`To-Branch ${spec.to} not found in branchspec ${spec.name}`)
				}
			}
		}


		return {branchMapDef: outErrors.length === 0 ? branchMap : null, config: defaultConfigForWholeBot}

		// not currently checking force flow loops
	}
}
