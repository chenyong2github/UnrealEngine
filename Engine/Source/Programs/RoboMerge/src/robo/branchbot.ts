// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as util from 'util'

import {AlreadyIntegrated, Branch, BranchBotInterface, BranchMapInterface, Conflict,
	MergeAction, PauseInfo, PauseInfoMinimal, PendingChange, ChangeInfo, TargetInfo, Target} from './branch-interfaces'
import {TickJournal} from './tick-journal'
import {Mailer, Recipients} from '../common/mailer'
import {Context} from './settings'
import {Perforce, Change, ClientSpec, EditOwnerOpts, OpenedFileRecord, ResolveResult} from '../common/perforce'
import {convertIntegrateToEdit} from '../common/p4util'
import {_nextTick} from '../common/helper'
import {BotEventTriggers} from './events'
import {Conflicts} from './conflicts'
import {isUserAKnownBot} from './notifications'

/**********************************
 * Bot monitoring a single stream
 **********************************/

const FAILED_CHANGELIST_PAUSE_TIMEOUT_SECONDS = 15 * 60
const NAG_EMAIL_MIN_TIME_SECONDS = 60 * 60 * 1000
const NAG_EMAIL_MIN_TIME_DESCRIPTION = 'an hour'
const SYNTAX_ERROR_PAUSE_TIMEOUT_SECONDS = 10 * 60

const MAX_CHANGES_TO_PROCESS_BEFORE_YIELDING = 50 // when catching up, we seem to get through 10 changes a minute
const MAX_INTEGRATION_ERRORS_TO_ANALYZE = 10

const INTEGRATION_FAILURE_REGEX = /^(.*\/)(.*) - can't \w+ exclusive file already opened$/

export const PAUSE_TYPE_BRANCH_STOP = 'branch-stop'
export const PAUSE_TYPE_MANUAL_LOCK = 'manual-lock'

const FAILURE_KIND_INTEGRATION_ERROR = 'Integration error'
const FAILURE_KIND_EXCLUSIVE_CHECKOUT = 'Exclusive check-out'
const FAILURE_KIND_MERGE_CONFLICT = 'Merge conflict'

function matchPrefix(a: string, b: string) {
	const len = Math.min(a.length, b.length)
	for (let i = 0; i < len; ++i) {
		if (a.charAt(i) !== b.charAt(i)) {
			return i
		}
	}
	return len
}

// mapping of #ROBOMERGE: flags to canonical names
// use these with a pound like #ROBOMERGE: #stage
const FLAGMAP: {[name: string]: string} = {
	// force a codereview to be sent to the owner as commits flow down the automerge chain
	review: 'review',
	forcereview: 'review',
	cr: 'review',

	// don't merge, only shelf this change and review the owner
	manual: 'manual',
	nosubmit: 'manual',
	stage: 'manual',

	// expand any automerge targets as null merges
	deadend: 'deadend',
	'null': 'deadend',

	// ignore this commit, do not merge anywhere (special "#ROBOMERGE: ignore" syntax without pound also triggers this)
	ignore: 'ignore',

	// process this change even if the author is on the excluded list
	disregardexcludedauthors: 'disregardexcludedauthors',

	// applies only to convertIntegratesToEdits bots: merge to all targets
	roboshelf: 'roboshelf'
};

// #commands in commit messages we should neuter so they don't spam (use only UPPERCASE)
const NEUTER_COMMANDS = [
	"CODEREVIEW",
	"FYI",
	"QAREVIEW",
	"RN",
	"DESIGNCHANGE",
	"REVIEW",
];


function getIntegrationOwner(targetBranch: Branch, overriddenOwner?: string): string | null
function getIntegrationOwner(pending: PendingChange): string

function getIntegrationOwner(arg0: Branch | PendingChange, overriddenOwner?: string) {
	// order of priority for owner:

	//	1) resolver - need resolver to take priority, even over reconsider, since recon might be from branch with
	//					multiple targets, so instigator might not even know about target with resolver
	//	2) reconsider
	//	3) propagated/manually added tag
	//	4) author - return null here for that case

	const pending = (arg0 as PendingChange)

	const branch = pending.action ? pending.action.branch : (arg0 as Branch)
	const owner = pending.change ? pending.change.owner : overriddenOwner
	return branch!.resolver || owner || null
}

class PauseState {
	infoStr: string
	info: PauseInfo

	constructor() {
		Object.defineProperty(this, 'pauseTimeout', {enumerable: false, value: null, writable: true})

		this.unpause()
	}

	isManualPause() {
		return this.info.type === PAUSE_TYPE_BRANCH_STOP
	}

	pause(bot: BranchBot, pauseInfo: PauseInfo, pauseDurationSeconds?: number) {
		this.info = pauseInfo

		const now = new Date

		if (pauseDurationSeconds) {

			// quantise to units of total time paused rounded to half hours, so kind of doubling, up to a maximum of 4 hours
			// round down so it doesn't kick in too early, e.g. 
			// e.g. intervals:	25, 25, 30,		60,		120,	240
			//		total:		25, 50, 1h20,	2h20,	4h20,	8h20
			const minDurationMinutes = pauseDurationSeconds / 60
			const pausedSoFarHalfHours = (now.getTime() - this.info.startedAt.getTime()) / (30*60*1000)

			const actualPauseDurationMinutes = Math.min(4*60, Math.max(Math.floor(pausedSoFarHalfHours) * 30, minDurationMinutes))

			const ms = actualPauseDurationMinutes * 60 * 1000
			this.pauseTimeout = setTimeout(() => {
				bot.unpause('timeout');
			}, ms);

			// note: this also has the effect of updating the info argument passed in
			this.info.endsAt = new Date(now.getTime() + ms);
		}

		this.refreshInfoString()
	}

	unpause() {
		delete this.info
		this.infoStr = ''

		this.cancelAutoUnpause()
	}

	cancelAutoUnpause() {
		if (this.pauseTimeout) {
			clearTimeout(this.pauseTimeout)
			this.pauseTimeout = null
		}
	}

	acknowledge(acknowledger : string) : void {
		this.info.acknowledger = acknowledger
		this.info.acknowledgedAt = new Date
		this.refreshInfoString()
	}

	persist(context: Context) {
		context.set('pause', this)
	}

	restore(bot: BranchBot, context: Context) {
		const state = context.get('pause')
		if (!state || !state.info) {
			this.unpause()
			return
		}

		if (state.info.endsAt) {
			const endsAtDate = new Date(state.info.endsAt)
			const untilEndMs = endsAtDate.getTime() - Date.now()
			if (untilEndMs < 0) {
				// forget pause - we would have timed out by now
				util.log(`Unpaused ${bot.fullName} due to timeout.`)
				return
			}

			// reset unpause timer
			this.pauseTimeout = setTimeout(() => {
				bot.unpause('timeout')
			}, untilEndMs)
			state.info.endsAt = endsAtDate
		}

		this.info = state.info
		this.infoStr = state.infoStr
		if (this.info.startedAt) {
			this.info.startedAt = new Date(this.info.startedAt)
		}
	}

	applyStatus(status: any) {
		if (this.info) {
			status.pause_info = this.info
			status.pause_info_str = this.infoStr
		}
	}

	secondsSincePause() {
		return this.info ? (Date.now() - this.info.startedAt.getTime()) / 1000 : NaN
	}

	refreshInfoString() {
		const lines: string[] = []
		if (this.info.source) {
			lines.push('Source=' + this.info.source)
		}

		/* We want to list these three values, in order of importance:
		 * 1. Acknowledger -- this person has acknowledge the blockage and is taking ownership
		 * 2. Owner (if different from Author) - Usually when a branch has an explict conflict resolver or a reconsider instigator
		 * 3. Author - Original author of commit
		 */
		if (this.info.acknowledger) {
			let ackStr = `Acknowledger=${this.info.acknowledger}`
			if (this.info.acknowledgedAt) {
				ackStr += ` (${this.info.acknowledgedAt.toString()})`
			} else {
				// Realistically we should not get here since 'acknowledgedAt' is defined at the same time as 'acknowledger'
				ackStr += " (unknown acknowledge time, ping Robomerge devs)"
			}
			lines.push(ackStr)
		} else if (this.info.owner) {
			// Check if owner is different than author
			if (!this.info.author || this.info.owner !== this.info.author) {
				lines.push('Owner=' + this.info.owner)
			}
		} else if (this.info.author) {
			// Finally we'll list author if present
			lines.push('Author=' + this.info.author)
		} else {
			// If we have none of the three values, this is most likely an error. We can call that out here to investigate.
			lines.push('Unknown owner/author. Contact RoboMerge team.')
		}

		if (this.info.shelfCl) {
			lines.push('ShelfCl=' + this.info.shelfCl)
		}
		if (this.info.targetBranchName) {
			lines.push('Target=' + this.info.targetBranchName)
		}
		if (this.info.message) {
			lines.push(this.info.message)
		}

		this.infoStr = lines.length > 0 ? lines.join('\n') : 'PauseType=' + this.info.type
	}

	private pauseTimeout: NodeJS.Timer | null = null;
}


interface QueuedChange {
	cl: number
	additionalFlags: string[]
	who: string
}

interface ExclusiveFile {
	name: string
	user: string
}

export class BranchBot implements BranchBotInterface {
	readonly branch: Branch;
	readonly branchMap: BranchMapInterface;

	readonly botname: string;
	tickJournal?: TickJournal;

	verbose = false;
	isRunning = false;
	lastClProcessed: number;

	// pausing
	isActive = false;
	readonly pauseState = new PauseState

	private readonly settings: Context;
	private readonly queuedChanges: QueuedChange[];
	private lastAction: string;
	private actionStart: Date;

	private readonly mailer: Mailer;
	private readonly p4: Perforce;

	private readonly eventTriggers: BotEventTriggers
	private readonly conflicts: Conflicts

	private readonly externalRobomergeUrl: string

	constructor(dummy: string, branchDef: Branch)
	constructor(deps: any, branchDef: Branch, eventTriggers: BotEventTriggers, persistence: Context)

	constructor(deps: any, branchDef: Branch, eventTriggers?: BotEventTriggers, persistence?: Context) {

		this.branchMap = branchDef.parent!
		this.botname = this.branchMap.botname
		this.branch = branchDef

		this.externalRobomergeUrl = process.env["ROBO_EXTERNAL_URL"] || "https://robomerge";

		// hack for tests
		if (deps === '__TEST__') {
			return;
		}

		this.p4 = deps.p4
		this.mailer = deps.mailer
		this.settings = persistence!
		this.lastClProcessed = this.settings.getInt('lastCl', this.branch.config.initialCL || 0)
		this.queuedChanges = this.settings.get('queuedCls') || []
		this.isActive = false // is this bot currently merging a change
		this.lastAction = ''
		this.actionStart = new Date
		this.eventTriggers = eventTriggers!
		this.conflicts = new Conflicts(eventTriggers!, persistence!)

		this.eventTriggers.onChangeParsed((info: ChangeInfo) => this.onGlobalChange(info))
		this.pauseState.restore(this, this.settings)

		if (this.branch.workspace === null) {
			throw new Error(`Branch ${this.fullName} has no valid workspace specified`);
		}
	}

	private _log_action(action: string, noLog?: boolean) { // 96
		if (!noLog)
			util.log(action);
		this.lastAction = action;
		this.actionStart = new Date;
	}

	async start() { // 103
		if (this.isRunning)
			throw new Error("already running");

		// log and start
		this.isRunning = true;

		if (this.lastCl <= 0) {
			this._log_action("Getting starting CL");

			let change: Change;
			try {
				change = await this.p4.latestChange(this.branch.rootPath);
			}
			catch (err) {
				util.log(this.fullName + " Error while querying P4 for changes: " + err.toString());
				return;
				
			}

			if (!change)
				throw new Error(this.fullName + " Unable to query for changes in: "+this.branch.rootPath);

			// set the most recent change
			this.lastCl = change.change;

			// start ticking
			util.log(`Began monitoring ${this.fullName} at CL ${this.lastCl} (INITIAL)`);
		}
		else {
			// start ticking
			util.log(`Began monitoring ${this.fullName} at CL ${this.lastCl}`);
		}
	}

	private async getChanges() {
		let changes: Change[] = [];

		const changePaths = this.branch.pathsToMonitor || [this.branch.rootPath];
		this._log_action(`checking for changes in ${changePaths}`, !this.verbose);

		try {
			if (this.lastCl <= 0) {
				util.log("checking if we ever get here - don't we always init lastCl?");
				// so actually need to find latest changelist in any of the paths to monitor!
				for (const path of changePaths) {
					const change = await this.p4.latestChange(path);
					if (change) {
						changes.push(change);
					}
				}
			}
			else {
				for (const path of changePaths) {
					changes = changes.concat(await this.p4.changes(path, this.lastCl));
				}
			}
		}
		catch (err) {
			util.log(this.fullName + " Error while querying P4 for changes: " + err.toString());
			return [];
		}

		return changes;
	}

	private async tickAsync() {
		this.tickJournal = {
			merges: 0,
			conflicts: 0,
			integrationErrors: 0,
			syntaxErrors: 0,

			monitored: false
		}

		// allow processing manually queued changes
		if (this.queuedChanges.length > 0)
		{
			const fromQueue = this.queuedChanges.shift()!
			this._log_action(`Processing manually queued change ${fromQueue.cl} on ${this.fullName}, requested by ${fromQueue.who}`)

			const changePaths = this.branch.pathsToMonitor || [this.branch.rootPath]
			let change: Change | null = null
			let errors: any[] = []
			await Promise.all(changePaths.map(path => this.p4.getChange(path, fromQueue.cl)
				.then(
					(result: Change) => { change = result },
					(errorResult) => errors.push(errorResult))
			))

			if (!change) {
				// change is invalid, so persist its removal from the queue
				this._persistQueuedChanges()

				// @todo distinguish between Perforce errors and change not existing in branch


				util.log(`${this.fullName} - Error while querying P4 for change ${fromQueue.cl}: ${errors}`)
				// @todo email admin page user when users have to log in
				// any way to show error without blocking page?
				return
			}

			// prep the change
			change!.isManual = true

			// special case for RoboShelf at the moment. Don't want to be able add arbitrary flags - too much danger of
			// adding them to the wrong CL, which was the reason for Reconsider in the first place
			if (fromQueue.additionalFlags.indexOf('roboshelf') !== -1) {
				change!.forceRoboShelf = true
			}

			// process just this change
			await this._processCl(change!, fromQueue.who)
			this._persistQueuedChanges()
			return
		}

		// see if our flow is paused
		if (this.isPaused) {
			if (this.verbose) {
				util.log('this bot is paused ' + this.branch.rootPath)
			}

			this.sendNagEmails()
			await _nextTick()
			return
		}

		// process the list of changes from p4 changes
		await this._processListOfChanges(await this.getChanges(), MAX_CHANGES_TO_PROCESS_BEFORE_YIELDING)
	}

	tick(next: () => void) {
		this.conflicts.preBranchBotTick(this.lastCl)
		this.tickAsync().then(next);
	}

	get fullName() {
		return `${this.botname} ${this.branch.name}`;
	}

	get lastCl() { // 223
		return this.lastClProcessed;
	}

	set lastCl(value) {
		// make sure it's always increasing
		if (value < this.lastClProcessed) {
			return;
		}

		// set the cache and save it
		this.setLastCl(value);
	}

	get isPaused() {
		return !!this.pauseState.info
	}

	private setLastCl(value: number) {

		let prevValue = this.lastClProcessed
		this.lastClProcessed = value
		this.settings.set("lastCl", value)

		// this also resets queued changes
		this.queuedChanges.length = 0
		this._persistQueuedChanges()

		return prevValue
	}

	forceSetLastCl(value: number, culprit: string, reason: string) {
		const prevValue = this.setLastCl(value)

		if (prevValue !== value) {
			this.conflicts.onForcedLastCl({branch: this.branch, forcedCl: value, previousCl: prevValue, culprit, reason})
		}
		return prevValue
	}

	_persistQueuedChanges() {
		this.settings.set('queuedCls', this.queuedChanges);
	}

	reconsider(changenum: number, additionalFlags: string[], who: string) {
		// manually queue
		util.log(`Queueing reconsider of CL ${changenum} on ${this.fullName}, requested by ${who}`);
		this.queuedChanges.push({cl: changenum, additionalFlags, who});
		this._persistQueuedChanges();
	}

	pause(pauseInfoMin: PauseInfoMinimal, pauseDurationSeconds?: number) {
		if (this.isPaused) {
			return
		}

		const pauseInfo = pauseInfoMin as PauseInfo
		if (!pauseInfo.startedAt) {
			pauseInfo.startedAt = new Date;
		}

		// updates pauseInfo endsAt
		this.pauseState.pause(this, pauseInfo, pauseDurationSeconds)
		this.pauseState.persist(this.settings)

		const info = this.pauseState.infoStr.replace(/\n/g, ' / ')
		util.log(`Pausing ${this.fullName}. ${info}. Ends ${pauseInfo.endsAt || 'never'}`)
	}

	unpause(reason: string) { // 318
		if (!this.isPaused) {
			return
		}

		this.pauseState.unpause()
		this.pauseState.persist(this.settings)
		util.log(`Unpaused ${this.fullName} due to ${reason}.`)
	}

	acknowledge(acknowledger : string) {
		util.log(`${acknowledger} acknowledges blockage on ${this.fullName}`)
		this.pauseState.acknowledge(acknowledger)
		this.pauseState.persist(this.settings)
	}

	applyStatus(status: any) {
		status.last_cl = this.lastCl
		status.is_paused = this.isPaused
		status.is_active = this.isActive
		status.queue = this.queuedChanges

		if (status.is_active)
		{
			status.status_msg = this.lastAction
			status.status_since = this.actionStart.toISOString()
		}

		if (status.is_paused) {
			this.pauseState.applyStatus(status)
		}
	}

	getNumConflicts() {
		return this.conflicts.getConflicts().length
	}

	private onGlobalChange(info: ChangeInfo) {
		if (info.branch === this.branch) {
			return; // ignore our own changes
		}

		this.conflicts.onGlobalChange(info)

		// check for auto-unpause after a resolve
		if (this.isPaused) {
			const pauseInfo = this.pauseState.info!;
			// check for unpause conditions
			if (pauseInfo.type === 'branch-stop') {

				// (temporary work-around after changing pause info to store name rather than entire branch structure)
				const targetBranch: Branch | undefined = (pauseInfo as any).targetBranch
				const branchName = pauseInfo.targetBranchName || (targetBranch ? targetBranch.name : null)

				// see if this change matches our stop CL
				if (branchName && branchName.toUpperCase() === info.branch.upperName && pauseInfo.sourceCl === info.source_cl)
				{
					// unpause
					this.unpause(`finding CL ${info.source_cl} merged to ${info.branch.name} in CL ${info.cl}`);
				}
			}
		}
	}

	private async _processListOfChanges(allChanges: Change[], maxChangesToProcess: number) {
		// list of changes except reconsiders
		const changes: Change[] = []

		// process manual changes even if paused
		for (const change of allChanges) {
			if (change.isManual) {
				await this._processCl(change)
			}
			else {
				changes.push(change)
			}
		}

		if (this.isPaused) {
			return
		}

		// make sure the list is sorted in ascending order
		changes.sort((a, b) => a.change - b.change)

		const pauseAtCl = this.branch.config.pauseAtCl
		let doneCount = 0

		for (const change of changes) {
			if (pauseAtCl && change.change >= pauseAtCl) {
				this.pause({
					type: PAUSE_TYPE_MANUAL_LOCK,
					message: `Paused due to pauseAtCl:${pauseAtCl} in config`,
					owner: 'robomerge'
				})
				return
			}

			await this._processCl(change)

			// if integration failed, we will now be paused
			if (this.isPaused) {
				return
			}

			// otherwise update stored last CL
			this.lastCl = change.change

			// yield if necessary - might be better to make this time-based
			++doneCount;
			if (maxChangesToProcess > 0 && doneCount === maxChangesToProcess) {
				util.log(`${this.branch.name} (${this.botname}) yielding after ${doneCount} revisions`)
				return
			}
		}
	}

	private async _processCl(change: Change, optOwnerOverride?: string) {
		this._log_action(`Analyzing CL ${change.change} on ${this.fullName}`)

		// parse the change
		const info = this.parseChange(change)

		if (optOwnerOverride) {
			info.owner = optOwnerOverride
		}

		if (info.targets && !change.isManual) {
			const flags = info.targets[0].flags
			if (!flags.has('disregardexcludedauthors') && this.branch.excludeAuthors.indexOf(change.user) >= 0) {
				// skip this one
				util.log(`Skipping CL ${change.change} due to excluded author '${change.user}'`)
				if (!this.isPaused) {
					this.lastCl = change.change
				}
				return
			}
		}

		// check if we need to block a change containing assets
		if (info.targets) {
			let blockAssetActions: MergeAction[] = []
			for (const action of info.targets) {
				if (action.mergeMode === 'normal') {
					// temporary fix for aliases not working correctly:
					for (const blockTargetName of this.branch.blockAssetTargets) {
						if (this._getBranch(blockTargetName) === action.branch) {
							blockAssetActions.push(action)
							break
						}
					}
				}
			}

			if (blockAssetActions.length !== 0) {
				let changeContainsAssets = false
				const result = await this.p4.describe(change.change)
				for (const entry of result.entries) {
					const fileExtIndex = entry.depotFile.lastIndexOf('.')
					if (fileExtIndex !== -1) {
						const fileExt = entry.depotFile.substr(fileExtIndex + 1)
						if (fileExt === 'uasset' || fileExt === 'umap') {
							changeContainsAssets = true
							break
						}
					}
				}

				if (changeContainsAssets) {
					info.errors = info.errors || []
					for (const action of blockAssetActions) {
						info.errors.push(`CL ${change.change} contains assets, which is not allowed by flow ${this.branch.name}->${action.branch.name}`)
					}
				}
			}
		}

		// stop on errors and e-mail interested parties
		if (info.errors) {

			this.handleSyntaxError(change.desc!, info)
			return
		}

		this.conflicts.onChangeParsed(info)

		// if there are any targets, start a merge to them
		if (info.targets) {
			await this._mergeCl(info)
		}
		else {
			this._emailNoActionIfRequested(info, `Nothing to do for ${info.cl}`);
		}
	}

	private _sendEmail(recipients: Recipients, subject: string, intro: string, message: string) {
		this.mailer.sendEmail(recipients, subject, intro, message, this.botname, (user: string) => {
			this._log_action(`Getting email for user ${user}`, false);
			return this.p4.getEmail(user);
		});
	}

	private _emailNoActionIfRequested(info: ChangeInfo, _msg: string) {

// doesn't check for roboshelf - need to work out exact conditions where a roboshelf should email (e.g. explicit target)
		if (info.isManual) {
			// this._sendEmail(new Recipients(info.owner!), msg, `Just an FYI that RoboMerge (${this.botname}) did not perform an integration for this changelist`, info.description);
		}
	}

	private _sendError(recipients: Recipients, subject: string, message: string) { // 424
		// log to STDERR
		process.stderr.write(`${subject}\n${message}\n\n`);
		this._sendEmail(recipients, subject, 'RoboMerge needs your help:', message);
	}

	private static _parseTargetList(targetString: string) {
		return targetString.split(/[ ,]/).filter(Boolean)
	}

	//
	private parseChange(change: Change): ChangeInfo { // 447
		let source: string | null = null
		let source_cl = -1
		let owner: string | null = null
		let authorTag: string | null = null

		// parse the description
		const descFinal: string[] = []
		if (change.desc && typeof(change.desc.split) !== 'function') {
			util.log(`WARNING!!! Unrecognised description type: ${typeof(change.desc)}, CL#${change.change}`)
			change.desc = '<description not available>'
		}
		const descLines = change.desc ? change.desc.split('\n') : []
		let propagatingNullMerge = false

		// flag to check whether default targets have been overridden
		let useDefaultFlow = true

		let requestedTargetNames: string[] = []
		for (const line of descLines) {
			// trim end - keep any initial whitespace
			const comp = line.replace(/\s+$/, '')

			// check for control hashes
			const match = comp.match(/^(\s*)#([-\w[\]]+)[:\s]*(.*)$/)
			if (!match) {
				// strip beginning blanks
				if (descFinal.length > 0 || comp !== '') {
					if (comp.indexOf('[NULL MERGE]') !== -1) {
						propagatingNullMerge = true
					}
					descFinal.push(line)
				}
				continue
			}

			const ws = match[1] 
			const command = match[2].toUpperCase()
			const value = match[3].trim()

			// #robomerge tags are required to be at the start of the line
			if (ws) {

				if (command === 'ROBOMERGE' && value.match(/\bnone\b/i)) {
					// completely skip #robomerge nones that might come in from engine branch integrations, so we don't
					// get impaled on our own commit hook
					continue
				}
			}
			else {
			 	if (command === 'ROBOMERGE') {

					// completely ignore bare ROBOMERGE tags if we're not the default bot (should not affect default flow)
					if (this.branch.isDefaultBot) {
						useDefaultFlow = false
						requestedTargetNames = [...requestedTargetNames, ...BranchBot._parseTargetList(value)]
					}
					continue
				}

				if (command.startsWith('ROBOMERGE')) {
					// what sort of robomerge command is it
					if (command === `ROBOMERGE[${this.botname}]` || command === 'ROBOMERGE[ALL]') {
						useDefaultFlow = false

						requestedTargetNames = [...requestedTargetNames, ...BranchBot._parseTargetList(value)]
					}
					else if (command === 'ROBOMERGE-AUTHOR') {
						// tag intended to be purely for propagating
						// currently using it to set owner if not already set
						authorTag = value
					}
					else if (command === 'ROBOMERGE-OWNER') {
						owner = value.toLowerCase()
					}
					else if (command === 'ROBOMERGE-SOURCE') {
						source = `${value} via CL ${change.change}`
					}
					else if (command === `ROBOMERGE-BOT`) {
						// if matches description written in _mergeCl
						if (value.startsWith(this.botname + ' (')) {
							useDefaultFlow = false
						}
					}
					// don't propagate any ROBOMERGE tags we don't understand (usually "#ROBOMERGE[OTHERBOT]")
					continue
				}
			}

			// check for commands to neuter
			if (NEUTER_COMMANDS.indexOf(command) >= 0) {
				// neuter codereviews so they don't spam out again
				descFinal.push(`${ws}[${command}] ${value}`)
				continue
			}
			
			if (command.indexOf('REVIEW-') === 0) {
				// remove swarm review numbers entirely (The can't be neutered and generate emails)
				if (value !== '') {
					descFinal.push(value)
				}
				continue
			}

			// default behavior is to keep any hashes we don't recognize
			descFinal.push(line)
		}

// wtf
		// if (change.forceRoboShelf) {
		// 	targetNames.add('#' + FLAGMAP.roboshelf)
		// }

		// make the end description (without any robomerge tags or 'at' references)
		const description = descFinal.join('\n').replace(/@/g, '[at]').trim()

		// Author is always CL user
		const author = (change.user || 'robomerge').toLowerCase()

		// default source to current
		if (source === null) {
			source = `CL ${change.change} in ${this.branch.rootPath}`
			source_cl = change.change
		}
		else {
			let m = source.match(/^CL (\d+)/)
			let num = m ? (parseInt(m[1]) || 0) : 0
			source_cl = num > 0 ? num : change.change
		}

		const info: ChangeInfo = {
			branch: this.branch,
			cl: change.change,
			source_cl: source_cl,
			isManual: !!change.isManual,
			author, source, description, propagatingNullMerge
		}

		if (authorTag) {
			info.authorTag = authorTag
		}

		if (owner) {
			info.owner = owner
		}

		// compute targets if list is not empty
		const defaultTargets = useDefaultFlow ? this.branch.defaultFlow : this.branch.forceFlowTo
		if ((requestedTargetNames.length + defaultTargets.length) > 0) {
			this._computeTargets(info, requestedTargetNames, defaultTargets)
		}

		return info
	}

	private _getBranch(name: string) { // 567
		return this.branchMap.getBranch(name);
	}

	private _computeImplicitTargets(
		outErrors: string[],
		targets: Set<Branch>,
		skipBranches: Set<Branch>
	) {
		const merges = new Map<Branch, Set<Branch>>()

		// parallel flood the flowsTo graph to find all specified targets

		// branchPaths is an array of 3-tuples, representing the branches at the current boundary of the flood
		//	- first entry is the branch
		//	- second and third entries make up the path to get to that branch
		//		- second entry, if non-empty, has the path up to the most recent explicit target
		//		- third entry has any remaining implicit targets

		const branchPaths: [Branch, Branch[], Branch[]][] = [[this.branch, [], [this.branch]]]

		const seen = new Set([this.branch])

		while (targets.size !== 0 && branchPaths.length !== 0) {

			const [sourceBranch, explicit, implicit] = branchPaths.shift()!

			// flood the graph one step to include all unseen direct flowsTo nodes of one branch
			let anyUnseen = false
			if (sourceBranch.flowsTo && !skipBranches.has(sourceBranch)) {
				for (const branchName of sourceBranch.flowsTo) {
					const branch = this._getBranch(branchName)
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
				let indirectTargetSet = merges.get(directTarget)
				if (indirectTargetSet === undefined) {
					indirectTargetSet = new Set<Branch>()
					merges.set(directTarget, indirectTargetSet)
				}
				targets.delete(directTarget)
				for (let index = 2; index < explicit.length; ++index) {
					const indirectTarget = explicit[index]
					indirectTargetSet.add(indirectTarget)
					targets.delete(indirectTarget)
				}
			}
		}

		for (const unreachableTarget of targets) {
			outErrors.push(`Branch '${unreachableTarget.name}' is not reachable from '${this.fullName}'`)
		}

		return merges
	}

	// public so it can be called from unit tests
	/*private*/ _computeTargets(info: TargetInfo, requestedTargetNames: string[], defaultTargets: string[]) {
		const errors: string[] = []
		const flags = new Set<string>()

		// list of branch names and merge mode (default targets first, so merge mode gets overwritten if added explicitly)
		const requestedMerges: [string, string][] = defaultTargets.map(name => [name, 'normal'] as [string, string])

		// Parse prefixed branch names and flags
		for (const rawTargetName of requestedTargetNames) {
			const rawTargetLower = rawTargetName.toLowerCase()

			// check for 'ignore' and remap to #ignore flag
			if (rawTargetLower === 'ignore') {
				flags.add('ignore')
				continue
			}

			// see if this has a modifier
			const firstChar = rawTargetName.charAt(0)
			switch (firstChar) {
			default:
				requestedMerges.push([rawTargetName, 'normal'])
				break

			case '!':
			case '-':
				requestedMerges.push([rawTargetName.substr(1), firstChar === '!' ? 'null' : 'skip'])
				break

			case '$':
			case '#':
				// set the flag and continue the loop
				const flagname = FLAGMAP[rawTargetLower.substr(1)]
				if (flagname) {
					if (flagname === 'deadend') {
						for (let branchName of this.branch.forceFlowTo) {
							requestedMerges.push([branchName, 'null'])
						}
					}
					else {
						flags.add(flagname)
					}
				}
				else {
					util.log(`Ignoring unknown flag "${rawTargetName}" from ${info.author}`)
				}
				break
			}
		}

		// compute the targets map
		const mergeActions: MergeAction[] = []
		let allDownstream: Set<Branch> | null = null
		if (!flags.has('ignore')) {
			const skipBranches = new Set<Branch>()
			const targets = new Map<Branch, string>()

			// process parsed targets
			for (const [targetName, mergeMode] of requestedMerges) {
				// make sure the target exists
				const targetBranch = this._getBranch(targetName)
				if (!targetBranch) {
					errors.push(`Unable to find branch "${targetName}"`)
					continue
				}

				if (targetBranch === this.branch) {
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

			if (targets.size !== 0) {
				const merges = this._computeImplicitTargets(errors, new Set(targets.keys()), skipBranches)
				if (errors.length === 0) {
					allDownstream = new Set<Branch>()

					for (const [direct, indirectTargetSet] of merges.entries()) {
						const mergeMode = targets.get(direct) || 'normal'
						if (mergeMode === 'skip') {
							continue
						}
						allDownstream.add(direct)

						const furtherMerges: Target[] = []
						for (const branch of indirectTargetSet) {
							furtherMerges.push({branch, mergeMode: targets.get(branch) || 'normal'})
							allDownstream.add(branch)
						}

						mergeActions.push({branch: direct, mergeMode, furtherMerges, flags})
					}

					// add indirect forced branches to allDownstream
					for (const branch of [...allDownstream]) {
						this.branchMap._computeReachableFrom(allDownstream, 'forceFlowTo', branch)
					}
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

	private async _mergeCl(info: ChangeInfo) { // 779
		for (const target of info.targets!) {
			// make sure we come back in here afterwords
			this._log_action(`Merging CL ${info.cl} to ${target.branch.name} (${target.mergeMode})`)

			if (this.tickJournal) {
				++this.tickJournal.merges
			}

			// build our change description
			let description = ''
			if (target.mergeMode === 'null') {
				description += '[NULL MERGE]\n'
			}
			description += `${info.description}\n\n` // description has been trimmed

			// if the owner is specifically overridden (can be by reconsider, resolver or manual tag)
			const overriddenOwner = getIntegrationOwner(target.branch, info.owner);

			let authorTag = info.authorTag
			if (overriddenOwner) {
				description += `#ROBOMERGE-OWNER: ${overriddenOwner}\n`
				// put an author tag if the owner is overridden, in case the owner ends up committing in their own name
				authorTag = authorTag || info.author
			}

			if (authorTag) {
				description += `#ROBOMERGE-AUTHOR: ${authorTag}\n`
			}

			description += `#ROBOMERGE-SOURCE: ${info.source}\n`

			const srcName = this.branch.config.streamName || this.branch.name
			const dstName = target.branch.config.streamName || target.branch.name

			description += `#ROBOMERGE-BOT: ${this.botname} (${srcName} -> ${dstName})\n`

			if (target.flags.has('disregardexcludedauthors')) {
				description += '#ROBOMERGE[ALL]: #DISREGARDEXCLUDEDAUTHORS\n'
			}

			let flags = '' // not all flags propagate, build them piecemeal
			if (target.flags.has('review')) {
				flags += ' #REVIEW'
			}

			const furtherMergeCommands = target.furtherMerges.map(target => (
				target.mergeMode === 'skip' ? '-' :
				target.mergeMode === 'null' ? '!' : '') + target.branch.name)
			if (flags || target.furtherMerges.length !== 0) {
				description += `#ROBOMERGE[${this.botname}]: ${furtherMergeCommands.join(' ')}${flags}\n`
			}

			if (this.branch.convertIntegratesToEdits) {
				// just for visibility - not used by RoboMerge
				description += '#ROBOMERGE-EDIGRATE\n'
			}

			target.description = description

			// do the integration
			await this._integrate(info, target)
		}
	}

	private async _integrate(info: ChangeInfo, target: MergeAction) {
		let to_integrate = info.cl
		this._log_action(`Integrating CL ${to_integrate} to ${target.branch.name}`, false)

		// if required, add author review here so they're not in target.description, which is used for shelf description in case of conflict
		let desc = target.description! // target.description always ends in newline

		if (target.flags.has('review')) {
			const owner = getIntegrationOwner(target.branch, info.owner) || info.author
			desc += `#CodeReview: ${owner}\n`
		}

		// create a new CL
		const changenum = await this.p4.new_cl(target.branch.workspace, desc)

		// try to integrate
		let source = {
			changelist: to_integrate,
			path_from: this.branch.rootPath,
			branchspec: (this.branch.branchspec as any)[target.branch.upperName],
		}
		const [mode, results] = await this.p4.integrate(target.branch.workspace, source, changenum, target.branch.rootPath)

		if (mode === 'already_integrated' || mode === 'no_files') {
			const msg = `Change ${to_integrate} was not necessary in ${target.branch.name}`
			this._emailNoActionIfRequested(info, msg)

			const event: AlreadyIntegrated = {change: info, action: target}
			this.conflicts.onAlreadyIntegrated(event)

			// integration not necessary
			util.log(msg)
			await this.p4.deleteCl(target.branch.workspace, changenum)
		}
		else {
			const pending = {change: info, action: target, newCl: changenum}

			let exclCheckoutMessages: string[] | null = null
			if (mode === 'partial_integrate') {
				const errors = results as string[]
				const exclusiveFiles = await this.analyzeIntegrationError(pending, errors)

				if (exclusiveFiles.length > 0) {
					// will need to store the exclusive file if we want to @ people in Slack
					exclCheckoutMessages = exclusiveFiles.map(exc => `${exc.name} checked out by ${exc.user}`)
					if (errors.length > MAX_INTEGRATION_ERRORS_TO_ANALYZE) {
						exclCheckoutMessages.push(`... and up to ${MAX_INTEGRATION_ERRORS_TO_ANALYZE - errors.length} more`)
					}
				}
			}

			if (mode === 'integrated') {
				// resolve this CL
				await this._resolveChangelist(pending)
			}
			else {
				// fail this CL
				const reason = exclCheckoutMessages ? 'Exclusive check-out' : 'Integration error'
				await this.handleIntegrationError(reason, (exclCheckoutMessages || results).join('\n'), pending)
			}
		}
	}

	private async analyzeIntegrationError(pendingChange: PendingChange, errors: string[]) {
		if (errors.length > MAX_INTEGRATION_ERRORS_TO_ANALYZE) {
			util.log(`Integration error ${pendingChange.change.branch.name}->${pendingChange.action.branch.name}: ${errors.length} files, checking first ${MAX_INTEGRATION_ERRORS_TO_ANALYZE}`)
		}

		const openedRequests: [RegExpMatchArray, Promise<OpenedFileRecord[]>][] = []
		for (const err of errors.slice(0, MAX_INTEGRATION_ERRORS_TO_ANALYZE)) {
			const match = err.match(INTEGRATION_FAILURE_REGEX)
			if (match) {
				openedRequests.push([match, this.p4.opened(match[1] + match[2])])
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

	private _makeBranchStopPauseInfo(errstr: string, errlong: string, arg: ChangeInfo | PendingChange, startedAt: Date): PauseInfo { // 863
		const MAX_LEN = 700
		if (errlong.length > MAX_LEN) {
			errlong = errlong.substr(0,MAX_LEN) + "..."
		}

		const maybePendingChange = arg as PendingChange
		const info = maybePendingChange.change || (arg as ChangeInfo)
		const owner = (maybePendingChange.action ? getIntegrationOwner(maybePendingChange) : info.owner) || info.author

		const pauseInfo: PauseInfo = {
			startedAt, owner, type: PAUSE_TYPE_BRANCH_STOP,
			change: info.cl,
			sourceCl: info.source_cl,
			source: info.source,
			message: `${errstr}:\n${errlong}`
		}

		if (maybePendingChange.change) {
			pauseInfo.targetBranchName = maybePendingChange.action.branch.name
			if (maybePendingChange.newCl > 0) {
				pauseInfo.shelfCl = maybePendingChange.newCl
			}
		}

		if (info.author) {
			pauseInfo.author = info.author;
		}
		return pauseInfo;
	}

	private async _revertAndDelete(branch: Branch, cl: number) {
		this._log_action(`Reverting CL ${cl}`, false);
		await this.p4.revert(branch.workspace, cl);

		this._log_action(`Deleting CL ${cl}`, false);
		await this.p4.deleteCl(branch.workspace, cl);
	}

	private handleSyntaxError(originalCommitMessage: string, change: ChangeInfo) {
		const shortMessage = `Syntax error in CL ${change.cl}`
		let message = `CL ${change.cl} in ${this.branch.rootPath} contained one or more RoboMerge syntax errors (see below).\n\n`
		for (const err of change.errors!) {
			message += `* ${err}\n`
		}

		const now = new Date
		if (!this.conflicts.find(null, change.source_cl)) {
			const owner : string = change.owner || change.author
			const ownerEmail = this.p4.getEmail(owner)
			this.conflicts.onBlockage({change, kind: 'Syntax error', description: message, owner, ownerEmail, time: now})

			// the following should be done as event handler

			const recipients = new Recipients(owner)

			message += '\nFull CL description:\n\n'
			message += originalCommitMessage
			this._sendError(recipients, shortMessage, message)
		}

		// stop the branch
		if (!change.isManual) {
			// dig out info to show in the pause message
			const pauseInfo = this._makeBranchStopPauseInfo(shortMessage, message, change, new Date)
			this.pause(pauseInfo, SYNTAX_ERROR_PAUSE_TIMEOUT_SECONDS)
		}
	}

	private async handleIntegrationError(kind: string, description: string, pending: PendingChange) {
		const targetBranch = pending.action.branch
		const logMessage = `Integration error while merging CL ${pending.change.cl} to ${targetBranch.name}`

		await this._revertAndDelete(targetBranch, pending.newCl)
		pending.newCl = -1

		let sendEmail = true
		if (pending.change.isManual) {
			util.log(logMessage + ` (reconsider triggered by ${pending.change.owner}):\n${description}.`)
		}
		else {

			// see if we've already sent an email for this one
			const existingBlockage = this.conflicts.find(pending)
			const blockageOccurred = existingBlockage ? existingBlockage.time : new Date
			if (existingBlockage && (existingBlockage.kind === FAILURE_KIND_INTEGRATION_ERROR ||
									existingBlockage.kind === FAILURE_KIND_EXCLUSIVE_CHECKOUT)) {
				util.log(logMessage + ' (already notified)')
				sendEmail = false
			}
			else {
				const owner = getIntegrationOwner(pending) || pending.change.author
				const ownerEmail = this.p4.getEmail(owner)
				const issue: Conflict = {change: pending.change, action: pending.action, kind, description,
											owner, ownerEmail, time: blockageOccurred}

				this.conflicts.onBlockage(issue)
			}

			const pauseInfo = this._makeBranchStopPauseInfo(kind, description, pending, blockageOccurred)
			this.pause(pauseInfo, FAILED_CHANGELIST_PAUSE_TIMEOUT_SECONDS)
		}

		if (sendEmail) {
			// todo - send email!
		}
	}

	private async handleConflict(resolveResult: ResolveResult, pending: PendingChange) {
		const description = resolveResult.getConflictsText()

		const targetBranch = pending.action.branch
		const logMessage = `Merge conflict while merging CL ${pending.change.cl} to ${targetBranch.name}`

		const owner = getIntegrationOwner(pending) || pending.change.author
		const shelfMsg = `Merge conflict.\n${owner}, please merge this change by hand.\nMore info at ${this.externalRobomergeUrl}#${this.botname}\n\n` + description

		if (pending.change.isManual) {
			await this._shelveChangelist(pending, shelfMsg)
			util.log(logMessage + `. Shelved CL ${pending.newCl} for ${owner} to resolve manually (from reconsider).`)
			return
		}

		// see if we've already sent a CR for this one
		const existingBlockage = this.conflicts.find(pending)

		const blockageOccurred = existingBlockage ? existingBlockage.time : new Date
		if (existingBlockage && existingBlockage.autoShelfCl) {

			util.log(logMessage + ` (already shelved CL ${existingBlockage.autoShelfCl}).`)

			await this._revertAndDelete(targetBranch, pending.newCl)
			pending.newCl = existingBlockage.autoShelfCl
		}
		else {
			if (this.tickJournal) {
				++this.tickJournal.conflicts;
			}

			const ownerEmail = this.p4.getEmail(owner)
			const conflict: Conflict = {change: pending.change, action: pending.action, shelfCl: pending.newCl,
										kind: FAILURE_KIND_MERGE_CONFLICT, description, owner, ownerEmail, time: blockageOccurred}

			if (existingBlockage) {
				this.conflicts.updateBlockage(conflict)
			}
			else {
				this.conflicts.onBlockage(conflict);
			}

			await this._shelveChangelist(pending, shelfMsg)
		// should maybe keep a record if shelving failed and retry with back-off?

			util.log(logMessage + `. Shelved CL ${pending.newCl} for ${owner} to resolve manually.`)
		}

		// pause this bot for a while or until manually unpaused
		const pauseInfo = this._makeBranchStopPauseInfo(FAILURE_KIND_MERGE_CONFLICT, description, pending, blockageOccurred)
		this.pause(pauseInfo, FAILED_CHANGELIST_PAUSE_TIMEOUT_SECONDS)
	}

	private async _roboShelve(result: ResolveResult, toShelve: PendingChange) {

		let errorStr: string | null = null
		let errorDetails: string | null = null

		if (result.hasConflict()) {
			// @todo Improve resolve result
			errorStr = 'Unable to RoboShelve due to conflicts',
			errorDetails = result.getConflictsText()
		}
		else {
			util.log(`Converting integrations in CL ${toShelve.change.cl} into edits`)
			try {
				await convertIntegrateToEdit(this.p4, toShelve.action.branch.workspace!, toShelve.newCl)
			}
			catch (err) {
				errorStr = 'Conversion from integrates to edits failed'
				errorDetails = err.toString()
			}
		}

		if (errorStr) {
			// Failed!

			if (toShelve.change.isManual) {
				// send email in parallel
				this._sendEmail(new Recipients(toShelve.change.owner!), 'RoboShelf error', errorStr, errorDetails!);
			}
			else {
				const info = toShelve.change
				const owner = getIntegrationOwner(toShelve) || toShelve.change.author
				const ownerEmail = this.p4.getEmail(owner)
				const conflict: Conflict = {change: info, kind: 'RoboShelf error', description: errorStr, owner, ownerEmail,
												time: new Date, action: toShelve.action, shelfCl: toShelve.newCl}
				this.eventTriggers.reportBlockage(conflict)
				this.pause(this._makeBranchStopPauseInfo(errorStr, errorDetails!, info, new Date))
			}
			await this._revertAndDelete(toShelve.action.branch, toShelve.newCl)
		}
		else {

			// see if target branch is force flow - using that as a trigger for auto-committing at the moment

			let isForcedTarget = false
			const targetBranchName = toShelve.action.branch.upperName
			for (const forced of this.branch.forceFlowTo) {
				if (forced.toUpperCase() === targetBranchName) {
					isForcedTarget = true
					break
				}
			}
			
			if (isForcedTarget) {
				await this._submitChangelist(toShelve)
			}
			else {
				await this._shelveChangelist(toShelve, 'RoboShelf has prepared this shelf for your consideration')
			}
		}
	}

	private async _resolveChangelist(pending: PendingChange) {
		const targetBranch = pending.action.branch
		// do a resolve with P4
		this._log_action(`Resolving CL ${pending.change.cl} against ${targetBranch.name}`, false)
		const result = await this.p4.resolve(targetBranch.workspace, pending.newCl, pending.action.mergeMode)

		if (this.branch.convertIntegratesToEdits && await this._roboShelve(result, pending)) {
			return
		}

		if (pending.action.flags.has('manual')) {
			// the user requested manual merge
			await this._shelveChangelist(pending)
			return
		}

		if (result.hasConflict()) {
			// if we had conflicts, fail the changelist
			await this.handleConflict(result, pending)
			return
		}

		// No conflicts - let's do this!
		await this._submitChangelist(pending)
	}

	private async _shelveChangelist(pending: PendingChange, reason?: string) {
		const changenum = pending.newCl
		const owner = getIntegrationOwner(pending) || pending.change.author
		this._log_action(`Shelving CL ${changenum} (change owned by ${owner})`, false)

		// code review any recipients
		const recipients = new Recipients(owner)
		recipients.addCc(...this.branch.notify)

		// make final edits to the desc
		let final_desc = `#CodeReview: ${recipients.toCommaSeparatedString()}\n`
		if (reason) {
			final_desc += `\n${reason}\n`
		}
		final_desc += "\n--------------------------------------\n"
		final_desc += pending.action.description

		const targetBranch = pending.action.branch

		// abort shelve if this is a buildmachine change
		let failed = false
		if (isUserAKnownBot(owner)) {
			// revert the changes locally
			this._log_action(`Reverting shelf due to '${owner}' being a known bot`)
			await this.p4.revert(targetBranch.workspace, changenum)
			failed = true
		}
		else {
			// edit the CL description
			await this.p4.editDescription(targetBranch.workspace, changenum, final_desc)

			// shelve the files as we see them (should trigger a codereview email)
			if (!await this.p4.shelve(targetBranch.workspace, changenum)) {
				failed = true
			}
		}

		if (failed) {
			// abort abort!
			// delete the cl
			this._log_action(`Deleting CL ${changenum}`, false)
			await this.p4.deleteCl(targetBranch.workspace, changenum)

			pending.newCl = -1

			this._sendError(recipients, `Error merging ${pending.change.source_cl} to ${targetBranch.name}`, final_desc)
			return
		}

		// revert the changes locally
		this._log_action(`Reverting CL ${changenum} locally. (conflict owner: ${owner})`, false)
		await this.p4.revert(targetBranch.workspace, changenum)

		// use p4.find_workspaces to find a workspace (owned by the user) for this change if this is a stream branch
		const workspaces: ClientSpec[] = await this.p4.find_workspaces(owner)

		// figure out what workspace to put it in
		const branch_stream = targetBranch.stream ? targetBranch.stream.toLowerCase() : null
		let target_workspace: string | null = null
		if (workspaces.length > 0) {
			// default to the first workspace
			target_workspace = workspaces[0].client

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
							target_workspace = def.client
						}
					}
				}
			}
		}

		// log if we couldn't find a workspace
		const opts: EditOwnerOpts = {}

		if (target_workspace) {
			opts.newWorkspace = target_workspace
		}
		else {
			util.log(`Unable to find appropriate workspace for ${owner}` + (branch_stream || targetBranch.name))
		}
	
		// edit the owner to the author so they can resolve and submit themselves
		await this.p4.editOwner(targetBranch.workspace, changenum, owner, opts)
	}

	private sendNagEmails() {
		let naggedSomeone = false
		const now = Date.now()
		for (const conflict of this.conflicts.getConflicts()) {
			if (!conflict.nagged) {
				const ageSeconds = (now - conflict.time.getTime()) / 1000
				if (ageSeconds > NAG_EMAIL_MIN_TIME_SECONDS) {
					conflict.nagged = true
					naggedSomeone = true

					// send a one-time only additional nag email just to owner
					util.log(`Sending nag email to ${conflict.owner}`)
					this._sendError(new Recipients(conflict.owner),
						`${this.botname} RoboMerge blocked for more than ${NAG_EMAIL_MIN_TIME_DESCRIPTION}`,
						`Please check #robomerge-${this.botname.toLowerCase()} on Slack for more details`
					)
				}
			}
		}

		if (naggedSomeone) {
			this.conflicts.persist()
		}
	}

	private async _submitChangelist(pending: PendingChange) {
		const info = pending.change
		const target = pending.action
		const changenum = pending.newCl

		// try to submit
		this._log_action(`Submitting CL ${changenum} by ${info.author}`, false)
		const finalCl = await this.p4.submit(target.branch.workspace, changenum)
		if (finalCl === 0) {
			// we need to resolve again
			util.log(`Detected concurrent changes. Re-resolving CL ${changenum}`)
			await this._resolveChangelist(pending)
			return
		}

		this.eventTriggers.reportCommit({change: info, action: target, newCl: finalCl})

		// log that this was done
		const log_str = (target.description || '').trim();
		util.log(`Submitted CL ${finalCl} to ${target.branch.name} (from ${this.fullName})\n    ${log_str.replace(/\n/g, "\n    ")}`);

		if (!isUserAKnownBot(info.author)) {

			// change owner, so users can edit change descriptions later for reconsideration
			util.log(`Setting owner of CL ${finalCl} to author of change: ${info.author}`);
			try {
				await this.p4.editOwner(target.branch.workspace, finalCl, info.author, {changeSubmitted: true})
			}
			catch (err) {
				util.log('ERROR changing owner: ' + err.toString());
			}
		}
	}
}
