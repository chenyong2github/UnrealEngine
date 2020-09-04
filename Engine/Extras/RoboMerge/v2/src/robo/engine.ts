// Copyright Epic Games, Inc. All Rights Reserved.

import * as util from 'util'

import {EngineInterface} from './engine-interface'
import {BranchMap} from './branchmap'
import {BranchBot} from './branchbot'
import {Blockage, Bot} from './branch-interfaces'
import {TickJournal} from './tick-journal'
import {AutoBranchUpdater} from './autobranchupdater'
import {Settings} from './settings'
import {Analytics} from '../common/analytics'
import {BotConfig, BranchDefs, BranchMapDefinition} from './branchdefs'
import {BotEventTriggers, BotEventHandler} from './events'
import {PersistentConflict} from './conflict-interfaces'
import {bindBotNotifications, BotNotifications} from './notifications'
import {bindBadgeHandler} from './badges'

export class Engine implements EngineInterface, BotEventHandler {
	static dataDirectory: string

	branchMap: BranchMap

	filename: string

	reloadAsyncListeners = new Set<Function>()

	autoUpdater: AutoBranchUpdater | null


	// separate off into class that only exists while bots are running?
	private eventTriggers?: BotEventTriggers

	constructor(botname: string, private deps: any, analytics: Analytics) {
		if (!Engine.dataDirectory) {
			throw new Error('Data directory must be set before creating a BranchMap')
		}

		this.branchMap = new BranchMap(botname)
		this.filename = botname + '.branchmap.json'
		this.analytics = analytics

		const branchSettingsPath = `${Engine.dataDirectory}/${this.filename}`

		util.log(`Loading branch map from ${branchSettingsPath}`)
		const fileText = require('fs').readFileSync(branchSettingsPath, 'utf8')

		const validationErrors: string[] = []
		const result = BranchDefs.parseAndValidate(validationErrors, fileText)
		if (!result.branchMapDef) {
			throw new Error(validationErrors.length === 0 ? 'Failed to parse' : validationErrors.join('\n'))
		}

		this.branchMap.config = result.config

		let error: string | null = null
		try {
			this.branchMap._initFromBranchMapsObjectInternal(result.branchMapDef)
		}
		catch (exc) {
			error = exc.toString();
		}

		if (!result.branchMapDef) {
			error = validationErrors.length === 0 ? 'Failed to parse' : validationErrors.join('\n');
		}

		// start empty bot on error - can be fixed up by branch definition check-in
		if (error) {
			util.log(`Problem starting up bot ${botname}: ${error}`);
		}

		this.settings = new Settings(botname, this.branchMap)
	}

	initBots() {
		this.eventTriggers = new BotEventTriggers(this.branchMap.botname, this.branchMap.config)
		this.eventTriggers.registerHandler(this)

		// bind handlers to bot events
		// doing it here ensures that we're using the most up-to-date config, e.g. after a branch spec reload
		bindBotNotifications(this.eventTriggers, this.settings.getContext('notifications'))
		bindBadgeHandler(this.eventTriggers, this.branchMap, this.deps.p4)

		let hasConflicts = false
		for (const branch of this.branchMap.branches) {
			if (branch.enabled) {
				const persistence = this.settings.getContext(branch.upperName)
				branch.bot = new BranchBot(this.deps, branch, this.eventTriggers, persistence)

				if (branch.bot.getNumConflicts() > 0) {
					hasConflicts = true
				}

			}
		}

		// report initial conflict status
		this.eventTriggers.reportConflictStatus(hasConflicts)
	}

	runbots() {
		this._runningBots = true
		const botlist: Bot[] = []
		for (const branch of this.branchMap.branches) {
			if (branch.bot)
				botlist.push(branch.bot)
		}

		if (this.autoUpdater) {
			botlist.push(this.autoUpdater)
		}

		if (botlist.length > 0) {

			let idx = -1
			let waitTime = Math.ceil(1000 * this.branchMap.config.checkIntervalSecs) / botlist.length

			const activity = new Map<string, TickJournal>()
			let runNext = () => {
				this.timeout = null

				if (++idx === botlist.length) {
					this.analytics.reportActivity(this.branchMap.botname, activity)
					this.analytics.reportMemoryUsage('main', process.memoryUsage().heapUsed)
					activity.clear()
					idx = 0
				}
				let bot = botlist[idx]

				if (bot.isRunning) {
					bot.isActive = true
					bot.tick(() => {
						if (bot.tickJournal) {
							const branchBot = <BranchBot>bot
							bot.tickJournal.monitored = branchBot.branch.isMonitored
							activity.set(branchBot.branch.upperName, bot.tickJournal)
						}

						bot.isActive = false

						if (this._shutdownCb) {
							this._shutdownCb()
							this._runningBots = false
							delete this.eventTriggers
							this._shutdownCb = null
						}
						else {
							// delay between bots
							this.timeout = setTimeout(runNext, waitTime)
						}
					})
				}
				else {
					// start all bots and go to the next one
					let error = null
					bot.start()
					.then(() => {
						process.nextTick(runNext)
					})
					.catch((err) => {
						error = err
					})

					if (error)
						throw error
				}
			}
			process.nextTick(runNext)
		}
		
	}

	stop(callback: Function) { // 322
		if (this._shutdownCb)
			throw new Error("already shutting down")

		// set a shutdown callback
		this._shutdownCb = () => {
			util.log(`Stopped monitoring ${this.branchMap.botname}`)

			for (const branch of this.branchMap.branches) {
				if (branch.bot) {
					// clear pause timer
					(branch.bot as BranchBot).pauseState.cancelAutoUnpause()
				}
			}

			callback()
		}

		// cancel the timeout if we're between checks
		if (this.timeout) {
			clearTimeout(this.timeout)
			this.timeout = null
			process.nextTick(this._shutdownCb)
		}
	}

	ensureStopping() {
		const wasAlreadyStopping = !!this._shutdownCb
		if (!wasAlreadyStopping) {
			this.stop(() => {})
		}
		return wasAlreadyStopping
	}

	onBlockage(_: Blockage) {
		// send a red badge if this is the only conflict, i.e. was green before
		if (this.getNumBlockages() === 1) {
			this.eventTriggers!.reportConflictStatus(true)
		}
	}

	onBranchUnblocked(conflict: PersistentConflict)
	{
		const numBlockages = this.getNumBlockages()
		util.log(`${this.branchMap.botname}: ${conflict.blockedBranchName} unblocked! ${numBlockages} blockages remaining`)
		if (numBlockages === 0) {
			this.eventTriggers!.reportConflictStatus(false)
		}
	}

	sendTestMessage(username: string) {
		console.log(`Sending test DM to ${username}`)

		//new BotNotifications(events.botname, events.botConfig.slackChannel, persistence)
		let botNotify = new BotNotifications(this.branchMap.botname, this.branchMap.config.slackChannel, 
			this.settings.getContext('notifications'))

		return botNotify.sendTestMessage(username)
	}

	private getNumBlockages() {
		let blockageCount = 0
		for (const branch of this.branchMap.branches) {
			if (branch.bot) {
				blockageCount += branch.bot.getNumConflicts()
			}
		}

		return blockageCount
	}

	async reinitFromBranchMapsObject(config: BotConfig, branchMaps: BranchMapDefinition) {
		if (this._runningBots)
			throw new Error("Can't re-init branch specs while running")

		this.branchMap.config = config
		this.branchMap._initFromBranchMapsObjectInternal(branchMaps)

		// inform listeners of reload (allows main.js to init workspaces)
		for (const listener of this.reloadAsyncListeners) {
			await listener(this)
		}
	}

	readonly settings: Settings
	private _runningBots = false
	private timeout: NodeJS.Timer | null = null
	private _shutdownCb: Function | null = null
	private analytics: Analytics
}
