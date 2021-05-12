// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from "../common/logger";
import { Change, PerforceContext } from "../common/perforce";
import { GateEventContext, GateInfo } from "./branch-interfaces"
import { DAYS_OF_THE_WEEK, EdgeOptions } from "./branchdefs"
import { BotEventTriggers } from "./events"
import { Context } from "./settings"


const GATE_INFO_KEY = 'gate-info'

export type GateContext = {
	options: EdgeOptions
	p4: PerforceContext | null // only allowed to be null for unit tests
	logger: ContextualLogger
}

class DummyEventTriggers {

	// pretend GateEventContext 
	from: any = ''
	to: any = ''
	pauseCIS = false

	constructor(public edgeLastCl: number) {
	}

	static Inner = class {
		beginCalls = 0
		endCalls = 0
		reportBeginIntegratingToGate(_arg: any) {
			++this.beginCalls
		}

		reportEndIntegratingToGate(_arg: any) {
			++this.endCalls
		}
	}

	get beginCalls() { return this.eventTriggers.beginCalls }
	get endCalls() { return this.eventTriggers.endCalls }

	// named to match EventTriggersAndStuff.context
	eventTriggers = new DummyEventTriggers.Inner
}

async function getRequestedGateCl(context: GateContext): Promise<GateInfo | null> {

	const lastGoodCLPath = context.options.lastGoodCLPath
	if (!lastGoodCLPath) {
		return null
	}

	if (typeof(lastGoodCLPath) === 'number') {
		return {cl: lastGoodCLPath}
	}

	let clString = null
	try {
		clString = await context.p4!.print(lastGoodCLPath)
	}
	catch (err) {
		context.logger.printException(err, `Error reading last good CL from ${lastGoodCLPath}`)
	}
	if (!clString) {
		return null
	}

	let clInfo: any;
	try {
		clInfo = JSON.parse(clString)
	}
	catch (err) {
		context.logger.printException(err, `Error parsing last good CL from ${lastGoodCLPath}`)
	}

	if (!clInfo.Change) {
		context.logger.warn(`No last good CL found in ${lastGoodCLPath}`)
		return null
	}

	const cl = typeof(clInfo.Change) === 'string' ? parseInt(clInfo.Change) : clInfo.Change
	if (!Number.isInteger(cl) || cl < 0) {
		context.logger.warn(`No last good CL found in ${lastGoodCLPath}`)
		return null
	}

	const result: GateInfo = {cl}
	// new goodCL: update link and date
	if (clInfo.Url) {
		result.link = clInfo.Url
	}
	try {
		const description = await context.p4!.describe(cl)
		if (description.date) {
			result.date = description.date
		}
	}
	catch (_err) {
	}

	return result
}

type EventTriggersAndStuff = GateEventContext & {
	eventTriggers: BotEventTriggers
}

export class Gate {
	constructor(	private eventContext: EventTriggersAndStuff | DummyEventTriggers,
					private context: GateContext, private persistence?: Context) {
		this.loadFromPersistence()
	}

	private processGateChange(newGate: GateInfo) {
		// degrees of freedom:
		// 	order of lastCl/gate.cl/newGate.cl (L/G/N)
		// 	empty queue (!Q)

		// 	first block is N <= G || (G <= L && !Q)

		// 	so remainder if N > G && G > L

		if (this.currentGateInfo) {
			// check for special case of new gate being before (or equal to) current catch-up gate
			//	replace and wipe queue

			if (newGate.cl <= this.currentGateInfo.cl) {
				// report if we were catching up and the replacement gate means we're now caught up
				if (this.lastCl < this.currentGateInfo.cl && this.lastCl >= newGate.cl) {

					this.reportCaughtUp()
				}
				this.currentGateInfo = newGate
				this.queuedGates.length = 0
				return
			}

			// case where we're already caught up and just need to replace the gate
			if (this.currentGateInfo.cl <= this.lastCl) {
				if (this.queuedGates.length > 0) {
					throw new Error('invalid gate state!')
				}
				this.currentGateInfo = null
				this.queuedGates.push(newGate)
				return
			}
		}

		// now we know either waiting for window or new gate is after catch-up gate, update the queue

		// e.g.
		//	- queued gates at CLs [5, 8, 15]
		//	- new gate at CL 7
		//	- chopIndex = 1
		//	- set length to 1, queue becomes [5]
		//	- push CL 7 so queue becomes [5, 7]
		const chopIndex = this.queuedGates.findIndex(queued => queued.cl >= newGate.cl)
		if (chopIndex >= 0) {
			this.queuedGates.length = chopIndex
		}
		this.queuedGates.push(newGate)
	}

	getMostRecentGate() {
		return this.queuedGates.length > 0 ? this.queuedGates[this.queuedGates.length - 1] : this.currentGateInfo
	}

	async tick() {
		if (this.lastCl < 0) {
			// do nothing until cl has been set
			return
		}

		const gateInfo = await getRequestedGateCl(this.context)
		if (!gateInfo) {
			if (this.currentGateInfo) {
				// we were waiting for a gate, so unpause and clear gate info
				if (this.isGateOpen()) {
					this.reportCaughtUp()
				}

				this.currentGateInfo = null
				this.queuedGates.length = 0
				this.persist()
			}

			return
		}

		let dirty = false
		const mostRecentGate = this.getMostRecentGate()

		if (!mostRecentGate) {
			// ooh look, our first gate
			this.queuedGates.push(gateInfo)
			dirty = true
		}
		else if (mostRecentGate.cl !== gateInfo.cl) {
			this.processGateChange(gateInfo)
			dirty = true
		}

		// sanity check
		if (!this.currentGateInfo && this.queuedGates.length === 0) {
			throw new Error('seem to have ignored gate!')
		}

		// if no current and in window, kick off new
		if (!this.currentGateInfo && this.queuedGates.length > 0) {
			if (this.tryNextGate(this.lastCl)) {
				this.reportCatchingUp()
				dirty = true
			}
		}

		if (dirty) {
			this.persist()
		}
	}

	preIntegrate(cl: number) {
		if (this.currentGateInfo) {
			this.context.logger.verbose(`${this.lastCl} -> ${cl} ${this.currentGateInfo.cl}`)
		}

		let adjustedCl: number | null = null
		if (this.currentGateInfo && cl > this.currentGateInfo.cl && this.currentGateInfo.cl > this.lastCl) {
			// we're waiting for currentGateInfo.cl but got a higher cl
			// basically we drag lastCl forward to currentGateInfo.cl so isGateOpen returns false
			// but if there are queued gates, we should have a go at moving to the next gate instead

			// note tryNextGate can change currentGateInfo or set it to null
			if (!this.tryNextGate(cl)) {
				this.reportCaughtUp()
				if (this.currentGateInfo) {
					adjustedCl = this.lastCl = this.currentGateInfo.cl
				}
			}
		}
		return adjustedCl
	}

	/**
	 Assumptions: currentGateInfo is valid and incoming cl > currentGateInfo.cl
	 @return whether we started catching up to a new gate
	 */
	private tryNextGate(cl: number) {
		if (this.queuedGates.length === 0) {
			this.context.logger.verbose('nothing queued')
			return false
		}

		const nextGateIndex = this.queuedGates.findIndex(queued => queued.cl > cl)
		if (nextGateIndex < 0) {
			this.context.logger.verbose('all done')

			// all done (incoming CL is after all queued gates)
			this.currentGateInfo = this.queuedGates[this.queuedGates.length - 1]
			this.queuedGates.length = 0
			return false
		}

		if (!this.nowIsWithinAllowedCatchupWindow()) {
			// want to make this an info log, but would spam every tick at the moment
			this.context.logger.verbose('delaying gate catch-up due to configured window')

			// wait for next window before catching up with queued gates
			this.currentGateInfo = null
			return false
		}

		this.context.logger.verbose('next!')

		// on to next gate
		this.currentGateInfo = this.queuedGates[nextGateIndex]
		this.queuedGates = this.queuedGates.slice(nextGateIndex + 1)
		return true
	}

	setLastCl(cl: number) {
		let notifyCaughtUp = false
		if (this.currentGateInfo) {
			// ignore going backwards; did we reach the gate?
			if (cl > this.lastCl && cl >= this.currentGateInfo.cl) {
				if (!this.tryNextGate(cl)) {
					// either all done or waiting for next window to restart catching up
					notifyCaughtUp = true
				}
			}
		}

		this.lastCl = cl
		if (notifyCaughtUp) {
			this.reportCaughtUp()
		}
	}

	getNumChangesRemaining(changesFetched: Change[], changeIndex: number) {
		const mostRecentGate = this.getMostRecentGate()
		if (!mostRecentGate) {
			return changesFetched.length - changeIndex - 1
		}
		
		if (this.lastCl >= mostRecentGate.cl) {
			return 0
		}

		// e.g. say relevant changes sequential multiples of 10
		//  integrating changes 30->80 inclusive, gate set to 60
		//  index 0 is 30, 1 is 40 etc, so gateIndex to find is 3
		//  non-exact matches:
		//		if gate file was set to 45, we'll integrate up to 40, so find first CL >= to gate CL
		let gateIndex = changeIndex
		for (; gateIndex < changesFetched.length; ++gateIndex) {
			if (changesFetched[gateIndex].change >= mostRecentGate.cl) {
				break
			}
		}
		return gateIndex - changeIndex;
	}

	isGateOpen() {
		return !this.getGateClosedMessage()
	}

	getGateClosedMessage() {
		// gate prevents integrations in two cases:
		//	- we've caught up with the most recent gate
		//	- integration window is preventing catching up to the next gate

		if (this.currentGateInfo) {
			// should show last cl in queue on web page, null means caught up
			return this.lastCl >= this.currentGateInfo.cl ? 'waiting for CIS' : null
		}

		// null means no gate
		return this.queuedGates.length > 0 ? 'waiting for window' : null
	}

	applyStatus(outStatus: { [key: string]: any }) {
		// for now, just equivalent of what was there before
		// @todo info about intermediate gate

		const mostRecentGate = this.getMostRecentGate()
		if (mostRecentGate) {
			outStatus.lastGoodCL = mostRecentGate.cl
			outStatus.lastGoodCLJobLink = mostRecentGate.link
			outStatus.lastGoodCLDate = mostRecentGate.date
		}
	}

	logSummary() {
		const logger = this.context.logger
		logger.info('current gate: ' + (this.currentGateInfo ? this.currentGateInfo.cl.toString() : 'none'))
		logger.info(`queued: ${this.queuedGates.length}`)
		logger.info(`last cl: ${this.lastCl}`)
	}

	private nowIsWithinAllowedCatchupWindow() {
		if (!this.context.options.integrationWindow) {
			return true
		}

		const now = new Date;
		let inWindow = false

		for (const pane of this.context.options.integrationWindow) {
			if (pane.dayOfTheWeek) {
				const dayIndex = DAYS_OF_THE_WEEK.indexOf(pane.dayOfTheWeek)
				if (dayIndex < 0 || dayIndex > 6) {
					throw new Error('invalid day, should have been caught in validation')
				}

				// @todo check if getUTCDay matches
				// (need to account for duration spanning one or more midnights)
			}
			else {
				// subtlely going over midnight, e.g. start at 11pm for 4 hours
				// say current time is 1am, we do (1 + (24 - 11)) % 24, and see that it is < 4
				if ((now.getUTCHours() + 24 - pane.startHourUTC) % 24 < pane.durationHours) {
					inWindow = true
					break
				}
			}
		}

		if (this.context.options.invertIntegrationWindow) {
			inWindow = !inWindow
		}
		return inWindow
	}

	private reportCatchingUp() {
		const mostRecent = this.getMostRecentGate()
		if (!mostRecent) {
			throw new Error('what are we catching up to?')
		}
		this.eventContext.eventTriggers.reportBeginIntegratingToGate({
			context: this.eventContext,
			info: mostRecent
		})
	}

	private reportCaughtUp() {
		this.eventContext.eventTriggers.reportEndIntegratingToGate(this.eventContext)
	}

	private persist() {
		if (!this.persistence) {
			return
		}

		const data: any = {}
		if (this.currentGateInfo) {
			data.current = this.currentGateInfo
		}
		if (this.queuedGates.length > 0) {
			data.queued = this.queuedGates
		}
		this.persistence.set(GATE_INFO_KEY, data)
	}

	private loadFromPersistence() {
		if (!this.persistence) {
			return
		}
		const saved = this.persistence.get(GATE_INFO_KEY)
		if (saved) {
			if (saved.current) {
				this.currentGateInfo = saved.current
				this.reportCatchingUp()
			}
			if (saved.queued) {
				this.queuedGates = saved.queued
			}
		}
	}

	private get lastCl() {
		return this.eventContext.edgeLastCl
	}

	private set lastCl(cl: number) {
		this.eventContext.edgeLastCl = cl
	}

	private currentGateInfo: GateInfo | null = null
	private queuedGates: GateInfo[] = []
}


const colors = require('colors')
colors.enable()
colors.setTheme(require('colors/themes/generic-logging.js'))


export async function runTests(parentLogger: ContextualLogger) {
	const logger = parentLogger.createChild('Gate')

	const makeTestGate = (cl: number, options?: EdgeOptions): [DummyEventTriggers, Gate] => {
		const et = new DummyEventTriggers(cl)
		return [et, new Gate(et, { options: options || {}, p4: null, logger})]
	}

	let fails = 0
	let testName = ''
	const assert = (b: boolean, msg: string) => {
		if (!b) {
			logger.error(`"${testName}" failed: ${colors.error(msg)}`)
			++fails
		}
	}

	// rules (maybe encapsulate in helper functions)
	// any setLastCl preceded by preIntegrate, any preIntegrate by tick 

	const simpleGateTest = async (exact: boolean) => {

		const options: EdgeOptions = {
			lastGoodCLPath: 1
		}

		// initial CL and gate are both 1
		const [et, gate] = makeTestGate(1, options)
		await gate.tick()
		assert(et.beginCalls + et.endCalls === 0, 'no events yet')
		assert(!gate.isGateOpen() && gate.getGateClosedMessage()!.includes('CIS'), 'gate closed')

		// nothing should happen here - CL 2 has been committed but gate prevents it being integrated
		gate.preIntegrate(2)
		await gate.tick()
		assert(et.beginCalls + et.endCalls === 0, 'no events yet')
		assert(!gate.isGateOpen() && gate.getGateClosedMessage()!.includes('CIS'), 'gate closed')

		// gate is now > 1 so we can 
		options.lastGoodCLPath = exact ? 2 : 3
		await gate.tick()
		gate.preIntegrate(2)
		assert(et.beginCalls === 1, 'catching up')
		assert(gate.isGateOpen(), 'gate open')

	}

	const openWindow = (opts: EdgeOptions) => {
		opts.integrationWindow!.push({
			startHourUTC: (new Date).getUTCHours(),
			durationHours: 1
			})
	}

	const replaceQueuedGates = async (replacement: string) => {

		// queue gates until window opens
		const options: EdgeOptions = {
			lastGoodCLPath: 4,
			integrationWindow: []
		}

		const [et, gate] = makeTestGate(2, options)
		await gate.tick()
		gate.preIntegrate(4)
		assert(et.beginCalls + et.endCalls === 0, 'no events yet')
		assert(!gate.isGateOpen() && gate.getGateClosedMessage()!.includes('window'), 'gate closed')

		options.lastGoodCLPath = 6
		await gate.tick()

		options.lastGoodCLPath = 8
		await gate.tick()

		openWindow(options)
		await gate.tick()
		assert(et.beginCalls === 1, 'catching up')
		assert(gate.isGateOpen(), 'gate open')

		let incoming: number[] = []
		switch (replacement) {
		case 'after':
			options.lastGoodCLPath = 5
			incoming = [4, 5]
			break

		case 'before':
			options.lastGoodCLPath = 3
			incoming = [3]
			break

		case 'lastCl':
			options.lastGoodCLPath = 2
			break

		case 'before lastCl':
			options.lastGoodCLPath = 1
			break
		}

		await gate.tick()
		for (const cl of incoming) {

			gate.preIntegrate(cl)
			gate.setLastCl(cl)
			await gate.tick()
		}

		assert(et.beginCalls === 1 && et.endCalls === 1, 'caught up')
		assert(!gate.isGateOpen() && gate.getGateClosedMessage()!.includes('CIS'), 'gate closed')
	}

	////
	testName = 'golden path'
	const [et1, gate1] = makeTestGate(1)

	await gate1.tick()
	gate1.preIntegrate(2)
	gate1.setLastCl(2)
	assert(et1.beginCalls + et1.endCalls === 0, 'no events')
	assert(gate1.isGateOpen(), 'gate open')

	////
	testName = 'with gate'
	await simpleGateTest(true)

	////
	testName = 'with inexact gate'
	await simpleGateTest(false)

	///
	testName = 'window'
	// start on cl 1, catch up to gate at 2 when window opens
	const optionsw: EdgeOptions = {
		lastGoodCLPath: 2,
		integrationWindow: []
	}

	const [etw, gatew] = makeTestGate(1, optionsw)
	await gatew.tick()
	gatew.preIntegrate(2)
	assert(etw.beginCalls + etw.endCalls === 0, 'no events yet')
	assert(!gatew.isGateOpen() && gatew.getGateClosedMessage()!.includes('window'), 'gate closed')

	openWindow(optionsw)

	await gatew.tick()
	assert(etw.beginCalls === 1, 'catching up')
	assert(gatew.isGateOpen(), 'gate open')

	///
	testName = 'queued gates'
	await (async () => {

		// queue gates until window opens
		const options: EdgeOptions = {
			lastGoodCLPath: 2,
			integrationWindow: []
		}

		const [et, gate] = makeTestGate(1, options)
		await gate.tick()
		gate.preIntegrate(2)
		assert(et.beginCalls + et.endCalls === 0, 'no events yet')
		assert(!gate.isGateOpen() && gate.getGateClosedMessage()!.includes('window'), 'gate closed')

		options.lastGoodCLPath = 5
		await gate.tick()

		options.lastGoodCLPath = 7
		await gate.tick()

		openWindow(options)
		await gate.tick()
		assert(et.beginCalls === 1, 'catching up')
		assert(gate.isGateOpen(), 'gate open')

		gate.preIntegrate(5)
		gate.setLastCl(5)
		await gate.tick()
		assert(et.beginCalls === 1, 'no more events')
		assert(gate.isGateOpen(), 'gate open')

		gate.preIntegrate(7)
		gate.setLastCl(7)
		assert(et.beginCalls === 1 && et.endCalls === 1, 'caught up')
		assert(!gate.isGateOpen() && gate.getGateClosedMessage()!.includes('CIS'), 'gate closed')

	})()

	///
	testName = 'replace queued gates'
	await replaceQueuedGates('after')

	///
	testName = 'replace queued gates (before current)'
	await replaceQueuedGates('before')

	///
	testName = 'replace queued gates (lastCl)'
	await replaceQueuedGates('lastCl')

	///
	testName = 'replace queued gates (before lastCl)'
	await replaceQueuedGates('before lastCl')

// lastGoodCLPath
// integrationWindow
	if (fails === 0) {
		logger.info(colors.info('Gate tests succeeded'))
	}
	return fails
}
