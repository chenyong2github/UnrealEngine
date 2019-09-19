// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as util from 'util';

import {RoboMerge} from './robo'
import {Status} from './status'
import {PAUSE_TYPE_MANUAL_LOCK} from './branchbot'

console.log('Reminder that RoboMerge variable is null in this file!', RoboMerge)

const START_TIME = new Date();

export interface Message {
	name: string
	args?: any[]
	userPrivileges?: string[]

	cbid?: string
}

// roboserver.ts -- getQueryFromSecure()
type Query = {[key: string]: string};

export class IPC {
	robo: RoboMerge

	constructor(inRobo: RoboMerge) {
		this.robo = inRobo
	}

	handle(msg: Message) {
		switch (msg.name) {
		case 'getBranches': return this.getBranches()
		case 'getBranch': return this.getBranch(msg.args![0], msg.args![1])
		case 'setVerbose': return this.setVerbose(!!msg.args![0])
		case 'getp4tasks': return this.robo.p4.running
		case 'getPersistence': return this.getPersistence(msg.args![0] as string)

		case 'doBranchOp': return this.doBranchOp(
			msg.args![0] as string,
			msg.args![1] as string,
			msg.args![2] as string,
			msg.args![3] as Query)

		default:
			return {statusCode: 500, message: `Did not understand msg "${msg.name}"`};
		}
	}

	private getBranches() {
		const status = new Status(START_TIME, this.robo.VERSION_STRING)
		for (let engine of this.robo.engines.values()) {
			for (let branch of engine.branchMap.branches) {
				if (branch.isMonitored) {
					status.addBranch(branch, engine.autoUpdater)
				}
			}
		}
		return status
	}

	private getBranch(botname: string, branchname: string) {
		const status = new Status(START_TIME, this.robo.VERSION_STRING)
		botname = botname.toUpperCase()
		branchname = branchname.toUpperCase()

		const engine = this.robo.engines.get(botname)
		if (engine) {
			const branch = engine.branchMap.getBranch(branchname)
			if (branch) {
				status.addBranch(branch, engine.autoUpdater)
			}
		}

		return status
	}

	private getPersistence(botname: string) {
		const dump = this.robo.dumpSettingsForBot(botname)
		return dump || {statusCode: 400, msg: `Unknown bot '${botname}'`}
	}

	private setVerbose(enabled: boolean) {
		const allBranches = this.robo.getAllBranches()
		for (let branch of allBranches) {
			if (branch.bot) {
				branch.bot.verbose = enabled
			}
		}
		this.robo.p4.verbose = enabled
		if (enabled) {
			util.log('Verbose logging enabled')
			for (let cmd_rec of this.robo.p4.running) {
				util.log(`Running: ${cmd_rec.cmd} since ${cmd_rec.start.toLocaleDateString()}`)
			}
		}
		else {
			util.log('Verbose logging disabled')
		}
		return 'OK'
	}

	private doBranchOp(botname: string, branchname: string, branchOp: string, query: Query) {
		// find the bot
		const map = this.robo.getBranchMap(botname)

		if (!map) {
			return {statusCode: 404, message: 'Could not find bot ' + botname}
		}

		let branch = map.getBranch(branchname)
		if (!branch) {
			return {statusCode: 404, message: 'Could not find branch ' + branchname}
		}

		if (!branch.isMonitored) {
			return {statusCode: 400, message: `Branch ${branch.name} is not being monitored.`}
		}

		if (!query.who) {
			return {statusCode: 400, message: `Attempt to run operation ${branchOp} on ${branch.name}: user name must be supplied.`}
		}

		const bot = branch.bot!
		let cl : number
		let reason : string
		switch (branchOp) {
		case 'pause':
			bot.pause({type: PAUSE_TYPE_MANUAL_LOCK, message: query.msg, owner: query.who})
			return 'ok'

		case 'unpause':
			reason = 'manual unlock by ' + query.who
			bot.unpause(reason)
			return 'ok'

		case 'set_last_cl':
			cl = parseInt(query.cl)

			if (isNaN(cl)) {
				return {statusCode: 400, message: 'Invalid CL parameter: ' + cl}
			}

			let prevCl = bot.forceSetLastCl(cl, query.who, query.reason)

			util.log(`Forcing last CL=${cl} on ${botname} : ${branch.name} (was CL ${prevCl}), requested by ${query.who} (Reason: ${query.reason})`)
			return 'ok'

		case 'reconsider':
			cl = parseInt(query.cl)
			if (isNaN(cl)) {
				return {statusCode: 400, message: 'Invalid CL parameter: ' + cl}
			}
			bot.reconsider(cl, query.additionalFlags ? query.additionalFlags.split(/\s*,\s*/) : [], query.who)
			return 'ok'
		
		case 'acknowledge':
			bot.acknowledge(query.who)
			return 'ok'
		}


		return {statusCode: 404, message: 'Unrecognized branch op: ' + branchOp}
	}
}
