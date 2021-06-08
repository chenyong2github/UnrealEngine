// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from "../common/logger";
import { Recipients } from "../common/mailer";
import { Change, ChangelistStatus, ClientSpec, ConflictedResolveNFile, EditOwnerOpts, IntegrationSource }  from "../common/perforce";
import { ResolveResult, IntegrationTarget, isExecP4Error, OpenedFileRecord, PerforceContext, EXCLUSIVE_CHECKOUT_REGEX } from "../common/perforce";
import { convertIntegrateToEdit } from "../common/p4util";
import { VersionReader } from "../common/version";
import { IPCControls } from "./bot-interfaces";
import { PauseState } from "./state-interfaces";
import { BlockagePauseInfo, ReconsiderArgs } from "./status-types";
import { AlreadyIntegrated, Branch, ChangeInfo, ConflictingFile, Failure, MergeAction, MergeMode, PendingChange } from "./branch-interfaces";
import { PersistentConflict } from "./conflict-interfaces";
import { BotEventTriggers } from "./events";
import { NodeBot } from "./nodebot";
import { isUserAKnownBot, postMessageToChannel } from "./notifications";
import { PerforceStatefulBot } from "./perforce-stateful-bot";
import { Context } from "./settings";
import { SlackMessageStyles } from "./slack";
import { EdgeOptions } from "./branchdefs";
import { getIntegrationOwner } from "./targets";
import { Gate } from "./gate";

function matchPrefix(a: string, b: string) {
	const len = Math.min(a.length, b.length)
	for (let i = 0; i < len; ++i) {
		if (a.charAt(i) !== b.charAt(i)) {
			return i
		}
	}
	return len
}

const FAILED_CHANGELIST_PAUSE_TIMEOUT_SECONDS = 15 * 60
const MAX_INTEGRATION_ERRORS_TO_ANALYZE = 5
const DEPOT_FILE_REGEX = /^(.*[\\\/])(.*)/
const JIRA_REGEX = /^\s*#jira\s+(.*)/i

const MAX_CONFLICTS_TO_LIST = 5

export type ResolveResultDetail = 'quick' | 'detailed'

export type EdgeIntegrationResult = string[] | Change

class EdgeBotImpl extends PerforceStatefulBot {
	readonly graphBotName: string
	readonly branch: Branch
	readonly sourceBranch: Branch
	private readonly edgeBotLogger: ContextualLogger

	// These are the quality control gates, usually driven by CI systems
	// and inside //GamePlugins/Main/Programs/Robomerge/gates/
	private gate: Gate

	// would like to encapsulate this in EdgeBot, but resolution is currently done by node bot
	private currentIntegrationStartTimestamp = -1

	/**
	 * Returns true if bot is currently merging a change
	 */
	private activity = ''
	private activeCount = 0 // ideally we'd only do one thing at a time, but verifyStomp runs in parallel
	get isActive() { return this.activeCount > 0 }

	constructor(private sourceNode: NodeBot, targetBranch: Branch, defaultLastCl: number, 
		private options: EdgeOptions,
		private readonly eventTriggers: BotEventTriggers, 
		private createEdgeBlockageInfo: (failure: Failure, pending: PendingChange) => BlockagePauseInfo, 
		settings: Context,
		matchingNodeConflict?: PersistentConflict) 
	{
		super(settings, options.initialCL || defaultLastCl)

		this.sourceBranch = sourceNode.branch
		this.branch = targetBranch
		this.graphBotName = sourceNode.graphBotName
		this.edgeBotLogger = new ContextualLogger(this.fullNameForLogging)

		this.p4 = new PerforceContext(this.edgeBotLogger)
		this.eventTriggers.onChangeParsed((info: ChangeInfo) => this.onGlobalChange(info))
		this.gate = new Gate(
			{ from: this.sourceBranch
			, to: this.branch
			, pauseCIS: !!this.options.pauseCISUnlessAtGate
			, edgeLastCl: this.lastCl
			, eventTriggers: this.eventTriggers
			},
			{options, p4: this.p4, logger: this.edgeBotLogger.createChild('gate')},
			this.settings
		)

		// Ensure edge's pause state isn't active but the parent node has a conflict assigned
		if (!this.pauseState.isBlocked() && matchingNodeConflict && matchingNodeConflict.cl < this.lastCl) {
			// If this is the case, we somehow lost our pause info. Back up our lastCl to ensure we hit it again
			// (We don't actually need to provide a culprit or reason)
			this.forceSetLastClWithContext(matchingNodeConflict.cl - 1, this.fullName, "Setting edge back to encounter conflict again")
		}
		
		this.start()
	}

	get targetBranch() {
		return this.branch
	}
	get fullName() {
		return `${this.sourceNode.fullName} -> ${this.targetBranch.name}`
	}
	get fullNameForLogging() {
		return `${this.sourceNode.fullNameForLogging}=>${this.targetBranch.name}`
	}
	get displayName() {
		return `${this.sourceNode.displayName} -> ${this.targetBranch.name}`
	}
	get incognitoMode() {
		return !!this.options.incognitoMode
	}
	get disallowSkip() {
		return !!this.options.disallowSkip
	}
	get doHackyOkForGithubThing() {
		return !!this.options.doHackyOkForGithubThing
	}
	get isTerminal() {
		return !!this.options.terminal
	}
	get excludedAuthors() {
		return this.options.excludeAuthors || []
	}
	protected get logger() {
		return this.edgeBotLogger
	}

	async setBotToLatestCl(): Promise<void> {
		this.gate.numChangesRemaining = 0
		return PerforceStatefulBot.setBotToLatestClInBranch(this, this.sourceBranch)
	}

	protected _forceSetLastCl_NoReset(value: number) {
		this.gate.numChangesRemaining = 0
		return super._forceSetLastCl_NoReset(value)
	}

	/**
	 return value ignored
	 want to separate out edge bot's tick from Bot hierarchy
	 (truthy tick not really part of bot interface)
	 */
	async tick() {
		// Update the Last Good Change from the gate file in Perforce
		this.gate.tick()
		return true
	}

	// isAvailable override to take account of gates
	get isAvailable() {
		// if paused on a gate, normally this.lastGoodCL === this.lastCl
		// (while C=this.lastGoodCL was being integrated, this.lastCl was still set to the previous CL. When
		// the integration completes, this.lastCl gets set to C and isAvailable becomes false)
		return super.isAvailable && this.gate.isGateOpen()
	}

	preIntegrate(cl: number) {
		const newLastCl = this.gate.preIntegrate(cl)
		if (newLastCl) {
			// do not call updateLastCl, since this request came from the gate object
			this.lastCl = newLastCl
		}

	}

	updateLastCl(changesFetched: Change[], changeIndex: number, targetCl?: number) {
		this.gate.updateLastCl(changesFetched, changeIndex, targetCl)
		super._forceSetLastCl_NoReset(this.gate.lastCl)
	}

	private async getPerforceRequestResultFromCL(changelist: number, path?: string, changelistStatus?: ChangelistStatus) : Promise<EdgeIntegrationResult> {
		const result = await this._getChange(changelist, path, changelistStatus)

		if (!result.changes || !result.changes[0]) {
			// swallowing actual error! should probably fix
			return [`Failed to get ${changelistStatus ? changelistStatus : 'submitted'} CL ${changelist} (errors = ${result.errors}`]
		}

		return result.changes[0]
	}
	
	private analyzeConflict(unresolved: ConflictedResolveNFile[]) {
		const results: ConflictingFile[] = []

		for (const file of unresolved) {
			const match = file.fromFile.match(DEPOT_FILE_REGEX)
			if (match) {
				if (file.resolveType.toLowerCase() === "branch") {
					results.push({name: match[2], kind: "branch"})
				}
				else if (file.resolveType.toLowerCase() === "delete") {
					results.push({name: match[2], kind: "delete"})
				}
				else if (file.resolveType.toLowerCase() === "content") {
					results.push({name: match[2], kind: "merge"})
				} else {
					// We really shouldn't get this kind, but it's better to display unknown than skip displaying the file
					results.push({name: match[2], kind: "unknown"})
				}
			}
		}
		return results
	}

	private async analyzeIntegrationError(errors: string[]) {
		if (errors.length > MAX_INTEGRATION_ERRORS_TO_ANALYZE) {
			this.edgeBotLogger.error(`Integration error: ${errors.length} files, checking first ${MAX_INTEGRATION_ERRORS_TO_ANALYZE}`)
		}
		
		const openedRequests: [RegExpMatchArray, Promise<OpenedFileRecord[]>][] = []
		for (const err of errors.slice(0, MAX_INTEGRATION_ERRORS_TO_ANALYZE)) {
			const match = err.match(EXCLUSIVE_CHECKOUT_REGEX)
			if (match) {
				console.log(match)
				openedRequests.push([match, this.p4.opened(null, match[1] + match[2])])
			}
		}

		const results: ExclusiveFile[] = []
		for (const [match, req] of openedRequests) {
			const recs = await req
			if (recs.length > 0) {
				// should only be one, since we're looking for exclusive check-out errors
				results.push({name: match[2], user: recs[0].user})
			}
		}

		return results
	}

	private async handleIntegrationError(failure: Failure, pending: PendingChange) {
		// Revert on Integration Error
		if (pending.newCl > 0) {
			await this.revertAndDelete(pending.newCl)
			pending.newCl = -1
		}
		
		// Pause Edge
		const pauseInfo = this.createEdgeBlockageInfo(failure, pending)
		this.block(pauseInfo, FAILED_CHANGELIST_PAUSE_TIMEOUT_SECONDS)
	}

	/** Handle any failures after a successful integration, e.g. conflicts or submit errors */
	private async handlePostIntegrationFailure(failure: Failure, pending: PendingChange) {
		const logMessage = `Post-integration failure while integrating CL ${pending.change.cl} to ${this.branch.name}`

		const owner = getIntegrationOwner(pending) || pending.change.author

		if (pending.change.userRequest) {
			const shelfMsg = `${owner}, please merge this change by hand.\nMore info at ${this.sourceNode.getBotUrl()}\n\n` + failure.description
			await this.shelveChangelist(pending, shelfMsg)
			this.edgeBotLogger.info(`${logMessage}. Shelved CL ${pending.newCl} for ${owner} to resolve manually (from reconsider).`)
			return
		}

		this.edgeBotLogger.info(`${logMessage}. Reverting ${pending.newCl}.`)
		await this.revertAndDelete(pending.newCl)

		let pauseDurationSeconds = FAILED_CHANGELIST_PAUSE_TIMEOUT_SECONDS
		// if we have a target, make sure pause duration is at least 2x duration of failed integration
		if (this.currentIntegrationStartTimestamp > 0) {
			const integrationDurationSeconds = (Date.now() - this.currentIntegrationStartTimestamp) / 1000
			if (pauseDurationSeconds! < integrationDurationSeconds * 2) {
				this.logger.info(`Increasing initial pause duration, since failed integration took ${integrationDurationSeconds}s`)
				pauseDurationSeconds = integrationDurationSeconds * 2
			}
		}

		// pause this bot for a while or until manually unpaused
		this.block(this.createEdgeBlockageInfo(failure, pending), pauseDurationSeconds)
	}

	performMerge(info: ChangeInfo, target: MergeAction, convertIntegratesToEdits: boolean): Promise<EdgeIntegrationResult> {
		// make sure we come back in here afterwords
		this._log_action(`Merging CL ${info.cl} via ${this.fullName} (${target.mergeMode})`)

		// build our change description
		let description = ''
		if (target.mergeMode === 'null') {
			description += '[NULL MERGE]\n'
		}

		// sanitise description for incognito mode
		if (this.incognitoMode) {
			for (const line of info.description.split('\n')) {

				// strip non UE- Jira tags
				const jiraTagMatch = line.match(JIRA_REGEX)
				if (jiraTagMatch && !jiraTagMatch[1].toLowerCase().startsWith('ue-')) {
					continue
				}

				// further sanitisation can go here

				description += line + '\n'
			}
		}
		else {
			description += info.description
		}
		description += '\n\n' // description has been trimmed

		// if the owner is specifically overridden (can be by reconsider, resolver or manual tag)
		const overriddenOwner = getIntegrationOwner(this.branch, info.owner)

		let authorTag = info.authorTag
		if (overriddenOwner) {
			description += `#ROBOMERGE-OWNER: ${overriddenOwner}\n`
			// put an author tag if the owner is overridden, in case the owner ends up committing in their own name
			authorTag = authorTag || info.author
		}

		if (authorTag) {
			description += `#ROBOMERGE-AUTHOR: ${authorTag}\n`
		}

		if (this.doHackyOkForGithubThing && !info.hasOkForGithubTag) {
			description += '#okforgithub ignore\n'
		}

		if (this.isTerminal) {
			description += '#robomerge #ignore\n'
		}

		if (info.overriddenCommand) {
			// probably no need to remove #s, but feels safer
			description += `#ROBOMERGE-COMMAND: ${info.overriddenCommand.replace(/#/g, '_')}\n`
		}
		if (info.macros.length > 0) {
			description += `#ROBOMERGE-COMMAND: ${info.macros.join(', ')}\n`	
		}
		let source = info.source
		if (this.incognitoMode) {
			// e.g. #ROBOMERGE-SOURCE: CL 12740994 in //Fortnite/Release-12.30/... via CL 12741005 via CL 12741374
			// to 						CL 12740994 via CL 12741005 via CL 12741374
			const viaIndex = source.indexOf(' via ')
			source = 'CL ' + info.source_cl + (viaIndex === -1 ? '' : source.substr(viaIndex))
		}

		description += `#ROBOMERGE-SOURCE: ${source}\n`

		const srcName = this.sourceNode.branch.config.streamName || this.sourceNode.branch.name
		const dstName = this.branch.config.streamName || this.branch.name

		// Add Robomerge bot and version information 
		if (!this.incognitoMode) {
			description += `#ROBOMERGE-BOT: ${this.graphBotName} (${srcName} -> ${dstName}) (v${VersionReader.getShortVersion()})\n`
		} else {
			description += `#ROBOMERGE-BOT: (v${VersionReader.getShortVersion()})\n`
		}

		if (info.forceStompChanges) {
			description += `#ROBOMERGE-CONFLICT stomped\n`
		}
		else if (info.forceCreateAShelf) {
			description += `#ROBOMERGE-CONFLICT from-shelf\n`
		}


		if (target.flags.has('disregardexcludedauthors')) {
			description += '#ROBOMERGE[ALL]: #DISREGARDEXCLUDEDAUTHORS\n'
		}

		let flags = '' // not all flags propagate, build them piecemeal
		if (target.flags.has('review')) {
			flags += ' #REVIEW'
		}

		const thisBotMergeCommands = target.furtherMerges
			.filter(target => !target.otherBot)
			.map(target => (
				target.mergeMode === 'skip' ? '-' :
				target.mergeMode === 'null' ? '!' : '') + target.branchName)

		if (flags || thisBotMergeCommands.length !== 0) {
			const thisBotname = 
				(this.incognitoMode && this.sourceNode.branchGraph.config.alias) ||
				this.graphBotName

			description += `#ROBOMERGE[${thisBotname}]: ${thisBotMergeCommands.join(' ')}${flags}\n`
		}

		for (const other of target.furtherMerges) {
			// slight hack here: mergemode is always normal and branchName is all the original commands/flags unaltered
			if (other.otherBot) {
				description += `#ROBOMERGE[${other.otherBot}]: ${other.branchName}\n`
			}
		}

		if (convertIntegratesToEdits) {
			// just for visibility - not used by RoboMerge
			description += '#ROBOMERGE-EDIGRATE\n'
		}

		target.description = description

		// do the integration
		return this.integrate(info, target)
	}

	public resetIntegrationTimestamp() {
		this.currentIntegrationStartTimestamp = -1
	}

	private async integrate(info: ChangeInfo, target: MergeAction) : Promise<EdgeIntegrationResult> {
		let to_integrate = info.cl
		this._log_action(`Integrating CL ${to_integrate} to ${this.targetBranch.name}`)

		// if required, add author review here so they're not in target.description, which is used for shelf description in case of conflict
		let desc = target.description! // target.description always ends in newline

		if (target.flags.has('review')) {
			const owner = getIntegrationOwner(this.targetBranch, info.owner) || info.author
			desc += `#CodeReview: ${owner}\n`
		}

		// create a new CL
		const changenum = await this.p4.new_cl(this.targetBranch.workspace, desc)

		// try to integrate
		const branchSpecToTarget = this.sourceBranch.branchspec.get(this.targetBranch.upperName)
		let source : IntegrationSource = {
			branchspec: branchSpecToTarget,
			changelist: to_integrate,
			depot: this.sourceBranch.depot,
			path_from: this.sourceBranch.rootPath,
			stream: this.sourceBranch.stream
		}
		let integTarget : IntegrationTarget = {
			depot: this.targetBranch.depot,
			path_to: this.targetBranch.rootPath,
			stream: this.targetBranch.stream
		}

		this.currentIntegrationStartTimestamp = Date.now()
		const [mode, results] = await this.p4.integrate(this.targetBranch.workspace, source, changenum, integTarget)

		const pending: PendingChange = {change: info, action: target, newCl: changenum}

		// note: treating 'integration_already_in_progress' as an error
		switch (mode) {
			case 'integrated':
				// resolve this CL
				return await this._resolveChangelist(pending)

			case 'already_integrated':
			case 'no_files': {
				const msg = `Change ${to_integrate} was not necessary in ${this.targetBranch.name}`
				const event: AlreadyIntegrated = {change: info, action: target}
				this.sourceNode.onAlreadyIntegrated(msg, event)

				// integration not necessary
				this.edgeBotLogger.info(msg)
				await this.p4.deleteCl(this.targetBranch.workspace, changenum)
				return [ msg ]
			}
		}

		const errors = results as string[]
		const exclusiveFiles = await this.analyzeIntegrationError(errors)

		const description = errors.join('\n')
		let failure: Failure | null = null

		if (exclusiveFiles.length > 0) {
			// will need to store the exclusive file if we want to @ people in Slack
			const exclCheckoutMessages = exclusiveFiles.map(exc => `${exc.name} checked out by ${exc.user}`)
			if (errors.length > MAX_INTEGRATION_ERRORS_TO_ANALYZE) {
				exclCheckoutMessages.push(`... and up to ${errors.length - MAX_INTEGRATION_ERRORS_TO_ANALYZE} more`)
			}
			failure = { kind: 'Exclusive check-out', description, summary: exclCheckoutMessages.join('\n') }
		}
		else {
			failure  = { kind: 'Integration error', description }
		}
		
		await this.handleIntegrationError(failure, pending)
		// Send to source node to facilitate notification handling
		await this.sourceNode.handleMergeFailure(failure, pending)
		return errors
	}

	private async revertAndDelete(cl: number) {
		this._log_action(`Reverting CL ${cl}`);
		await this.p4.revert(this.targetBranch.workspace, cl);

		this._log_action(`Deleting CL ${cl}`);
		await this.p4.deleteCl(this.targetBranch.workspace, cl);
	}

	async revertPendingCLWithShelf(client: string, change: number, userContext: string) {
		this._log_action(` ${userContext} - Deleting shelf from and reverting ${change}`)
		try {
			this.edgeBotLogger.info(await this.p4.delete_shelved(client, change))
			await this.revertAndDelete(change)
		}
		catch (err) {
			this.edgeBotLogger.printException(err, `${userContext} - Error reverting ${change}`)
		}
	}

	private performResolve(changelist: number, mergeMode: MergeMode, detail: ResolveResultDetail) {
		return this.p4.resolve(this.targetBranch.workspace, changelist, mergeMode, detail === 'quick')
	}

// code restored that was deleted in CL#8057353 (from nodebot.ts) - still needs work
	private async roboShelve(result: ResolveResult, toShelve: PendingChange) : Promise<EdgeIntegrationResult> {
		const targetBranch = toShelve.action.branch
		const targetEdge = this
		let errorStr: string | null = null
		let errorDetails: string | null = null

		if (result.hasConflict()) {
			// @todo Improve resolve result
			errorStr = 'Unable to RoboShelve due to conflicts',
			errorDetails = result.getConflictsText()
		}
		else {
			this.edgeBotLogger.info(`Converting integrations in CL ${toShelve.change.cl} into edits`)
			try {
				await convertIntegrateToEdit(this.p4, targetBranch.workspace!, toShelve.newCl)
			}
			catch (err) {
				errorStr = 'Conversion from integrates to edits failed'
				errorDetails = err.toString()
			}
		}

		if (errorStr) {
			// Failed!

			if (toShelve.change.userRequest) {
				// send email in parallel
				// this._sendGenericEmail(new Recipients(toShelve.change.owner!), 'RoboShelf error', errorStr, errorDetails!);
				await targetEdge.revertAndDelete(toShelve.newCl)
			}
			else {

				const failure: Failure = {kind: 'Conversion to edits failure', description: errorStr}
				// should probably have custom handling, but try this for tests 
				this.handleIntegrationError(failure, toShelve)

				await this.sourceNode.handleMergeFailure(failure, toShelve)
			}
			
			return [`${errorStr}: ${errorDetails}`]
		}
		else {

			await targetEdge.submitChangelist(toShelve)
			return this.getPerforceRequestResultFromCL(toShelve.newCl)

// should shelve if manual or non auto target?
			// await targetEdge.shelveChangelist(toShelve, 'RoboShelf has prepared this shelf for your consideration')
			// return this.getPerforceRequestResultFromCL(toShelve.newCl, this.getChangePaths(targetBranch), "shelved")

		}
	}

	/**
	 * Perform a resolve of the change described by pending
	 * @param {PendingChange} pending Pending change to resolve and submit
	 * @param {number} [submitRetries = 3] number of times 'targetEdge.submitChangelist' can fail gracefully and come back to this method
	 */
	async _resolveChangelist(pending: PendingChange, submitRetries  = 3) : Promise<EdgeIntegrationResult> {
		// do a resolve with P4
		this._log_action(`Resolving CL ${pending.change.cl} against ${this.targetBranch.name}`)
		let detail: ResolveResultDetail = 'detailed'
		const result = await this.performResolve(pending.newCl, pending.action.mergeMode, detail)
		if (pending.action.flags.has('manual') || pending.change.forceCreateAShelf) {
			// the user requested manual merge
			await this.shelveChangelist(pending)

			if (!pending.change.sendNoShelfEmail) {
				this.sourceNode.emailShelfRequester(pending)
			}
			
			return this.getPerforceRequestResultFromCL(pending.newCl, pending.action.branch.rootPath, "shelved")
		}

		let failure: Failure | null = null
		if (result.hasConflict()) {
			failure = {
				kind: 'Merge conflict', description: result.getConflictsText()
			}

			const conflicts = this.analyzeConflict(result.getConflicts())
			if (conflicts.length > 0) {
				failure.summary = conflicts
					.slice(0, MAX_CONFLICTS_TO_LIST)
					.map(({name, kind}) => `${name} (${kind} conflict)`)
					.join('\n')

				if (conflicts.length > MAX_CONFLICTS_TO_LIST) {
					failure.summary += `\n... and ${conflicts.length - MAX_CONFLICTS_TO_LIST} more`
				}
			}
		}

		if (!failure) {
			if (this.sourceBranch.convertIntegratesToEdits) {
				return await this.roboShelve(result, pending)
			}


			// No conflicts - let's do this!
			const error = await this.submitChangelist(pending, submitRetries)

			if (typeof(error) === 'string') {
				failure = { kind: 'Commit failure', description: error }
			}
		}

		if (failure) {
			await this.handlePostIntegrationFailure(failure, pending)
			await this.sourceNode.handleMergeFailure(failure, pending, true)

			return [`${failure.kind}: ${failure.description}`]
		}

		return this.getPerforceRequestResultFromCL(pending.newCl)
	}

	/**
	 * Attempt to submit changelist as described by incoming PendingChange.
	 * @param pending Change to submit
	 * @param {Number} [resolveRetries=3] Number of times to catch non-fatal errors and attempt the resolve -> submit chain again.
	 * @todo In a perfect world, parameter 'resolveRetries' wouldn't exist and an orchastration method would handle this in a loop.
	 */
	async submitChangelist(pending: PendingChange, resolveRetries = 3): Promise<void|string> {
		const info = pending.change
		const target = pending.action
		const changenum = pending.newCl

		// try to submit
		this._log_action(`Submitting CL ${changenum} by ${info.author}`)
		const result = await this.p4.submit(this.targetBranch.workspace, changenum)
		if (typeof(result) === "string") {
			// an error occurred while submitting
			return result;
		}
		const finalCl = result;
		if (finalCl === 0) {
			if (resolveRetries >= 1) {
				// we need to resolve again
				this.edgeBotLogger.info(`Detected concurrent changes. Re-resolving CL ${changenum}`)
				await this._resolveChangelist(pending, --resolveRetries)
				return
			}
			else {
				const msg = `Hit maximum number of retries when trying to submit changelists for pending change ${pending.change.cl}`
				this.edgeBotLogger.warn(msg)
				return msg
			}
		}

		this.eventTriggers.reportCommit({change: info, action: target, newCl: finalCl})

		const integrationDurationSeconds = Math.round((Date.now() - this.currentIntegrationStartTimestamp) / 1000);
		this.edgeBotLogger.info(`Integration time: ${integrationDurationSeconds}s`)
		this.resetIntegrationTimestamp()

		// log that this was done
		const log_str = (target.description || '').trim();
		this.edgeBotLogger.info(`Submitted CL ${finalCl} to ${this.targetBranch.name}\n    ${log_str.replace(/\n/g, "\n    ")}`);

		if (!isUserAKnownBot(info.author)) {
			// change owner, so users can edit change descriptions later for reconsideration
			this.edgeBotLogger.info(`Setting owner of CL ${finalCl} to author of change: ${info.author}`);
			try {
				await this.p4.editOwner(this.targetBranch.workspace, finalCl, info.author, {changeSubmitted: true})
			}
			catch (reason) {
				let errPreface = 'Error changing owner'
				let exception = reason

				if (isExecP4Error(reason)) {
					const [err, output] = reason
					exception = err

					const userNonexistenceMatches = output.match(/User (.+) doesn't exist/)
					if (userNonexistenceMatches) {
						let msg = `Couldn't change changelist owner for change ${finalCl}: Perforce reports user ${userNonexistenceMatches[0]} doesn't exist`
						this.edgeBotLogger.warn(msg + `:\n${output}`)
						msg += `. User will *remain* as Robomerge:\n\`\`\`${output}\`\`\``
						postMessageToChannel(msg, this.sourceNode.branchGraph.config.slackChannel, SlackMessageStyles.WARNING)
						if (this.targetBranch.config.additionalSlackChannelForBlockages) {
							postMessageToChannel(msg, this.targetBranch.config.additionalSlackChannelForBlockages, SlackMessageStyles.WARNING)
						}
						return
					}
					else if (output.match(/You don't have permission for this operation/)) {
						errPreface += ' (This error is expected in non-prod instances.)'
					}
				} 

				this.edgeBotLogger.printException(exception, errPreface);
			}
		}
	}

	private async shelveChangelist(pending: PendingChange, reason?: string) {
		const changenum = pending.newCl
		const owner = getIntegrationOwner(pending) || pending.change.author
		this._log_action(`Shelving CL ${changenum} (change owned by ${owner})`)

		// code review any recipients
		const recipients = new Recipients(owner)
		recipients.addCc(...this.sourceBranch.notify)

		// make final edits to the desc
		let final_desc = ""
		if (reason) {
			final_desc += `\n${reason}\n`
		}
		final_desc += pending.action.description

		if (pending.change.additionalDescriptionText) {
			final_desc += pending.change.additionalDescriptionText
		}

		// abort shelve if this is a buildmachine / robomerge change (unless we are forcing the shelf)
		let failed = false
		if (isUserAKnownBot(owner) && !pending.change.forceCreateAShelf) {
			// revert the changes locally
			this._log_action(`Reverting shelf due to '${owner}' being a known bot`)
			await this.p4.revert(this.targetBranch.workspace, changenum)
			failed = true
		}
		else {
			// edit the CL description
			await this.p4.editDescription(this.targetBranch.workspace, changenum, final_desc)

			// shelve the files as we see them (should trigger a codereview email)
			if (!await this.p4.shelve(this.targetBranch.workspace, changenum)) {
				failed = true
			}
		}

		if (failed) {
			// abort abort!
			// delete the cl
			this._log_action(`Deleting CL ${changenum}`)
			await this.p4.deleteCl(this.targetBranch.workspace, changenum)

			pending.newCl = -1

			this.sourceNode._sendError(recipients, `Error merging ${pending.change.source_cl} to ${this.targetBranch.name}`, final_desc)
			return
		}

		// revert the changes locally
		this._log_action(`Reverting CL ${changenum} locally. (conflict owner: ${owner})`)
		await this.p4.revert(this.targetBranch.workspace, changenum)

		// figure out what workspace to put it in
		const branch_stream = this.targetBranch.stream ? this.targetBranch.stream.toLowerCase() : null
		let targetWorkspace: string | null = null
		// Check for specified workspace from createShelf operation
		if (pending.change.targetWorkspaceForShelf) {
			targetWorkspace = pending.change.targetWorkspaceForShelf
		}
		// Find a suitable workspace from one of the owner's workspaces
		else {
			// use p4.find_workspaces to find a workspace (owned by the user) for this change if this is a stream branch
			const workspaces: ClientSpec[] = await this.p4.find_workspaces(owner)

			if (workspaces.length > 0) {
				// default to the first workspace
				targetWorkspace = workspaces[0].client

				// if this is a stream branch, do some better match-up
				if (branch_stream) {
					// find the stream with the closest match
					let target_match = 0
					for (let def of workspaces) {
						let stream = def.Stream
						if (stream) {
							let matchlen = matchPrefix(stream.toLowerCase(), branch_stream)
							if (matchlen > target_match) {
								target_match = matchlen
								targetWorkspace = def.client
							}
						}
					}
				}
				this.edgeBotLogger.info(`Chose workspace ${targetWorkspace} (stream? ${branch_stream ? 'yes' : 'no'})`)
				pending.change.targetWorkspaceForShelf = targetWorkspace
			}
		}

		// log if we couldn't find a workspace
		const opts: EditOwnerOpts = {}

		if (targetWorkspace) {
			this.edgeBotLogger.info(`Moving CL ${changenum} to workspace '${targetWorkspace}'`)
			opts.newWorkspace = targetWorkspace
		}
		else {
			this.edgeBotLogger.warn(`Unable to find appropriate workspace for ${owner}` + (branch_stream || this.targetBranch.name))
		}

		// edit the owner to the author so they can resolve and submit themselves
		await this.p4.editOwner(this.targetBranch.workspace, changenum, owner, opts)
	}

	onGlobalChange(info: ChangeInfo) {
		if (info.branch !== this.targetBranch) {
			return; // only be concerned with our target branch
		}

		if (this.pauseState.isBlocked()) {
			// see if this change matches our stop CL
			if (info.source_cl === this.pauseState.blockagePauseInfo!.sourceCl) {
				this.unblock(`finding CL ${info.source_cl} merged to ${info.branch.name} in CL ${info.cl}`)
			}
		}
	}

	setActivity(func?: string) {
		if (func) {
			if (this.isActive) {
				console.log(`Bot already doing ${this.activity}, but told to ${func}`)
			}
			this.activity = func
			++this.activeCount
		}
		else {
			if (this.activeCount < 0) {
				throw new Error('wtf')
			}
			else if (this.activeCount === 1) {
				this.activity = ''
			}
			--this.activeCount			
		}
	}

	createStatus() {
		const status: { [key: string]: any } = {}

		status.name = this.fullName
		status.display_name = this.displayName

		status.target = this.targetBranch.name
		if (this.targetBranch.stream) {
			status.targetStream = this.targetBranch.stream
		}

		status.rootPath = this.targetBranch.rootPath

		status.last_cl = this.lastCl
		status.num_changes_remaining = this.gate.numChangesRemaining

		status.is_active = this.isActive
		status.is_available = this.isAvailable
		status.is_blocked = this.isBlocked
		status.is_paused = this.isManuallyPaused

		this.gate.applyStatus(status)

		if (this.lastBlockage > 0) {
			status.lastBlockage = this.lastBlockage
		}

		if (status.is_active)
		{
			status.status_msg = this.lastAction
			status.status_since = this.actionStart.toISOString()
		}

		if (!this.pauseState.isAvailable()) {
			this.pauseState.applyStatus(status)
		}

		if (this.targetBranch.resolver) {
			status.resolver = this.targetBranch.resolver
		}
		status.skipAllowed = !this.options.disallowSkip
		status.incognitoMode = this.options.incognitoMode
		status.excludeAuthors = this.options.excludeAuthors

		return status
	}

	public forceSetLastClWithContext(value: number, culprit: string, reason: string) {
		const prevValue = this.forceSetLastCl(value)

		// trigger events
		this.sourceNode.onForcedLastCl(this.displayName, value, prevValue, culprit, reason)
		
		return prevValue
	}

	reconsider(instigator: string, changeCl: number, additionalArgs?: Partial<ReconsiderArgs>) {
		this.sourceNode.reconsider(instigator, changeCl, {targetBranchName: this.targetBranch.name, ...(additionalArgs || {})})
	}

	acknowledgeConflict(acknowledger: string, changeCl: number, pauseState: PauseState, blockageInfo: BlockagePauseInfo) {
		return this.sourceNode.acknowledgeConflict(acknowledger, changeCl, pauseState, blockageInfo)
	}

	unacknowledgeConflict(changeCl: number, pauseState: PauseState, blockageInfo: BlockagePauseInfo) {
		return this.sourceNode.unacknowledgeConflict(changeCl, pauseState, blockageInfo)
	}
}

abstract class EdgeBotEntryPoints implements IPCControls {
	createStatus: EdgeBotImpl["createStatus"]
	block: EdgeBotImpl["block"]
	unblock: EdgeBotImpl["unblock"]
	pause: EdgeBotImpl["pause"]
	unpause: EdgeBotImpl["unpause"]
	reconsider: EdgeBotImpl["reconsider"]
	acknowledge: EdgeBotImpl["acknowledge"]
	unacknowledge: EdgeBotImpl["unacknowledge"]
	forceSetLastClWithContext: EdgeBotImpl["forceSetLastClWithContext"]
	resetIntegrationTimestamp: EdgeBotImpl["resetIntegrationTimestamp"]

	// async methods
	revertPendingCLWithShelf: EdgeBotImpl["revertPendingCLWithShelf"] 
	performMerge: EdgeBotImpl["performMerge"]
}

/* This wrapper class allows us to control what is accessing the EdgeBotImpl internals, and
 *	allows us to monitor when an edge is active */
export class EdgeBot extends EdgeBotEntryPoints {
	private impl: EdgeBotImpl
	displayName: string
	fullName: string
	fullNameForLogging: string

	constructor(sourceNode: NodeBot, targetBranch: Branch, defaultLastCl: number,
		options: EdgeOptions,
		eventTriggers: BotEventTriggers, 
		createEdgeBlockageInfo: (failure: Failure, pending: PendingChange) => BlockagePauseInfo, 
		settings: Context,
		matchingNodeConflict?: PersistentConflict
	) {
		super()
		this.impl = new EdgeBotImpl(sourceNode, targetBranch, defaultLastCl, options, eventTriggers,
									createEdgeBlockageInfo, settings, matchingNodeConflict)

		this.displayName = this.impl.displayName
		this.fullName = this.impl.fullName
		this.fullNameForLogging = this.impl.fullNameForLogging

		this.createStatus = () => this.impl.createStatus()
		this.block = this.proxy("block")
		this.unblock = this.proxy("unblock")
		this.pause = this.proxy("pause")
		this.unpause = this.proxy("unpause")
		this.reconsider = this.proxy("reconsider")
		this.acknowledge = this.proxy("acknowledge")
		this.unacknowledge = this.proxy("unacknowledge")
		this.forceSetLastClWithContext = this.proxy("forceSetLastClWithContext")
		this.resetIntegrationTimestamp = this.proxy("resetIntegrationTimestamp")

		this.revertPendingCLWithShelf = this.proxyAsync("revertPendingCLWithShelf")
		this.performMerge = this.proxyAsync("performMerge")

		if (options.forcePause) {
			this.impl.pause('Pause forced in branchspec.json', 'branchspec')
		}
	}

	// Could extend this by offering _log_action() calls
	proxy<FuncName extends keyof EdgeBotEntryPoints>(func: FuncName) {
		return (...args: Parameters<EdgeBotEntryPoints[FuncName]>) => {
			this.impl.setActivity(func)
			try {
				return this.impl[func].apply(this.impl, args)
			}
			finally {
				this.impl.setActivity()
			}
		}
	}

	proxyAsync<FuncName extends keyof EdgeBotEntryPoints>(func: FuncName) {
		return async (...args: Parameters<EdgeBotEntryPoints[FuncName]>) => {
			this.impl.setActivity(func)
			try {
				return await this.impl[func].apply(this.impl, args)
			}
			finally {
				this.impl.setActivity()
			}
		}
	}

	tick() {
		return this.impl.tick()
	}
	
	preIntegrate(cl: number) {
		this.impl.preIntegrate(cl)
	}

	updateLastCl(changesFetched: Change[], changeIndex: number, targetCl?: number) {
		this.impl.updateLastCl(changesFetched, changeIndex, targetCl)
	}

	/* Mirrored Variables */
	get disallowSkip() { return this.impl.disallowSkip }
	get incognitoMode() { return this.impl.incognitoMode }
	get excludedAuthors() { return this.impl.excludedAuthors }
	get isActive() { return this.impl.isActive }
	get isAvailable() { return this.impl.isAvailable }
	get isBlocked() { return this.impl.isBlocked }

	get lastCl() {
		return this.impl.lastCl
	}
	set lastCl(value: number) {
		this.impl.lastCl = value
	}

	get pauseState() {
		return this.impl.pauseState
	}
	get targetBranch() {
		return this.impl.targetBranch
	}

	get ipcControls(): IPCControls {
		return {
			block: this.block,
			unblock: this.unblock,
			pause: this.pause,
			unpause: this.unpause,
			reconsider: this.reconsider,
			acknowledge: this.acknowledge,
			unacknowledge: this.unacknowledge,
			forceSetLastClWithContext: this.forceSetLastClWithContext
		}
	}
}

interface ExclusiveFile {
	name: string
	user: string
}