// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as util from 'util'

import {Badge} from '../common/badge'
import {Perforce} from '../common/perforce'
import {Blockage, Branch, BranchMapInterface, ChangeInfo} from './branch-interfaces'
import {PersistentConflict} from './conflict-interfaces'
import {BotEvents, BotEventHandler} from './events'

const BADGE_LABEL = 'Merge'

//										 -   1  -        -----    2   -----
const CHANGE_INFO_VIA_REGEX = /CL \d+ in (\/\/.*)\/\.\.\.((?: via CL \d+)*)/;

//													   - 1-     - 2-        - 3-
const BOT_DESCRIPTION_LINE_REGEX = /#ROBOMERGE-BOT:?\s*(.*)\s*\((.*)\s*->\s*(.*)\)/;

type BadgeFunc = (cl: number, stream: string) => void

/** Add badges to branches listed as 'via' */
async function markViaBranches(viaClStrings: string[], badgeFunc: BadgeFunc, branchMap: BranchMapInterface, p4: Perforce) {
	for (const viaClStr of viaClStrings) {
		const cl = parseInt(viaClStr)
		if (isNaN(cl)) {
			util.log('Warning - failed to parse via CL: ' + viaClStr)
			continue
		}

		const result = await p4.describe(cl)

		for (const line of result.description.split('\n')) {
			const botLineMatch = line.match(BOT_DESCRIPTION_LINE_REGEX)
			if (botLineMatch) {
				// 1: bot, 2: origin, 3: stream committed to
				const branchName = botLineMatch[3]
				const branch = branchMap.getBranch(branchName)
				if (!branch) {
					util.log('Warning - branch not found to add UGS badge to: ' + branchName)
				}
				else if (branch.stream) {
					// only adding badges to stream projects at the moment
					badgeFunc(cl, branch.stream)
				}
			}
		}
	}
}

function isChangeUpstreamFromBadgeProject(change: ChangeInfo) {
	if (change.branch.badgeProject) {
		// for now, checking strictly upstream; could have a flag
		return false
	}

	if (change.allDownstream) {
		for (const branch of change.allDownstream) {
			if (branch.badgeProject) {
				return true
			}
		}
	}

	return false
}



class BadgeHandler implements BotEventHandler {

	constructor(public p4: Perforce, badgeBranch: Branch, private allBranchStreams: Map<string, string>) {
		// for now, single badge project
		// possibility: could check multiple. WOuld need to be prioritised for consistency
		this.badgeProject = badgeBranch.config.badgeProject!
		// this.badgeLabel = `Merge:->${badgeBranch.name}`
		this.botname = badgeBranch.parent.botname
	}

	/** See if this change implies badges should be added or updated */
	onChangeParsed(info: ChangeInfo) {

		// make sure it's a robo-merge to the branch, not a normal commit
		if (info.cl !== info.source_cl && info.branch.config.badgeProject) {
			const result = info.propagatingNullMerge ? Badge.SKIPPED : Badge.SUCCESS

			if (!this.markIntegrationChain(info, result)) {
				util.log('Warning: unable to parse source of suspected RM CL: ' + info.source)
			}
		}
	}

	private markInProgressCl(info: ChangeInfo, result: string) {
		Badge.mark(result, BADGE_LABEL, `${info.branch.stream}/${this.badgeProject}`, info.cl, this.botname)
	}

	onBlockage(blockage: Blockage) {
		const change = blockage.change

		if (!change.isManual && isChangeUpstreamFromBadgeProject(change)) {
			// let's commit this first - mark project where conflict happened
			// will call markIntegrationChain, but need to make sure it hits exactly the right branches in this case
			this.markInProgressCl(change, Badge.FAILURE)
		}
	}

	onBranchUnblocked(info: PersistentConflict) {
		// don't know if this lead to a badge project. Not much harm in having the odd false positive here
		const stream = this.allBranchStreams.get(info.blockedBranchName)
		if (stream) {
			Badge.markSuccess(BADGE_LABEL, `${stream}/${this.badgeProject}`, info.cl, this.botname)
		}
	}

	onConflictStatus(anyConflicts: boolean) {
		const status = anyConflicts ? Badge.FAILURE : Badge.SUCCESS
		for (const stream of this.allBranchStreams.values()) {
			Badge.mark(status, 'RoboMerge', `${stream}/${this.badgeProject}`, 0, this.botname)
		}
	}

	/** On successful (possibly null) merge, mark whole chain (but not target branch) with same status */
	private markIntegrationChain(info: ChangeInfo, result: string) {
		const match = info.source.match(CHANGE_INFO_VIA_REGEX)
		if (!match) {
			return false
		}

		const badgeFunc = (cl: number, stream: string) =>
							Badge.mark(result, BADGE_LABEL, `${stream}/${this.badgeProject}`, cl, this.botname)

		const sourceStream = match[1]
		badgeFunc(info.source_cl, sourceStream)

		// vias!
		const allVias = match[2]
		if (allVias) {
			// note that at the point CL that commits to badge project is included in the vias
			// (may not actually be committed anywhere else)
			const viaClStrs = allVias.split(' via CL ')

			// this will look up each changelist sequentially - no need to wait for it
			// (maybe should technically wait for it at some level - could theoretically start marking a different status
			// in parallel otherwise)
			markViaBranches(viaClStrs.slice(1, viaClStrs.length - 1), badgeFunc, info.branch.parent, this.p4)
		}
		return true
	}

	private botname: string
	private badgeProject: string
}

export function bindBadgeHandler(events: BotEvents, branchMap: BranchMapInterface, p4: Perforce) {

	let badgeBranch: Branch | null = null

	for (const branch of branchMap.branches) {
		if (branch.config.badgeProject) {
			if (branch.stream) {
				badgeBranch = branch
			}
			else {
				util.log(`Branch '${branch.name}' set as badge branch but isn't a stream`)
			}
			break
		}
	}

	// badges enabled if badge branch set in config
	if (badgeBranch) {
		const allStreams = new Map<string, string>()
		for (const bm of branchMap.branches) {
			if (bm.stream) {
				allStreams.set(bm.upperName, bm.stream)
			}
		}
		events.registerHandler(new BadgeHandler(p4, badgeBranch, allStreams))
	}
}
