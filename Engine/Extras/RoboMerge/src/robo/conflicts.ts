// Copyright Epic Games, Inc. All Rights Reserved.

import {AlreadyIntegrated, Blockage, Branch, ChangeInfo, Conflict,
			ForcedCl, PendingChange} from './branch-interfaces'
import {PersistentConflict} from './conflict-interfaces'
import {BotEventTriggers} from './events'
import {Context} from './settings'

type BranchArg = string | Branch
function getBranchName(branchArg: BranchArg) {
	return (branchArg as Branch).name || branchArg as string
}

function conflictMatches(conflict: PersistentConflict, sourceCl: number, branchArg?: BranchArg | null) {
	const targetBranchName = branchArg && getBranchName(branchArg)
	return conflict.sourceCl === sourceCl && (
		targetBranchName ? conflict.targetBranchName === targetBranchName.toUpperCase() : !conflict.targetBranchName
	);
}


function makePersistentConflict(blockage: Blockage): PersistentConflict {

	const sourceChange = blockage.change
	const result: PersistentConflict = {
		blockedBranchName: sourceChange.branch.upperName,
		cl: sourceChange.cl,
		sourceCl: sourceChange.source_cl,
		author: sourceChange.author,
		owner: blockage.owner,
		kind: blockage.kind,
		time: blockage.time,
		nagged: false,
		resolution: ''
	}

	if ((blockage as Conflict).action) {
		const conflict = blockage as Conflict
		result.targetBranchName = conflict.action.branch.upperName
		result.autoShelfCl = conflict.shelfCl
	}

	return result
}

function setResolvedTimeAsNow(conflict: PersistentConflict) {
	conflict.timeTakenToResolveSeconds = (Date.now() - conflict.time.getTime()) / 1000
}

/** Per branch bot record of conflicts waiting for resolution */
export class Conflicts {

	static RESOLUTION_RESOLVED = 'resolved'
	static RESOLUTION_SKIPPED = 'skipped'
	static RESOLUTION_CANCELLED = 'cancelled' // target removed from command
	static RESOLUTION_DUNNO = 'fixed'

	constructor(private eventTriggers: BotEventTriggers, private persistence: Context) {

	// need backward compat, or just switch over when no conflicts?

		const conflictsToLoad = persistence.get('conflicts')
		if (conflictsToLoad) {
			for (const conflict of conflictsToLoad) {
				conflict.time = new Date(conflict.time)
				this.conflicts.push(conflict)
			}
		}
	}

	getConflicts() {
		return this.conflicts
	}

	/** note that although this takes a 'Blockage', it is normally passed a 'Conflict' (derived type) */
	updateBlockage(blockage: Blockage) {
		const replacement = makePersistentConflict(blockage)

		for (let index = 0; index != this.conflicts.length; ++index) {
			const conflict = this.conflicts[index]
			if (conflictMatches(conflict, replacement.sourceCl, replacement.targetBranchName)) {
				this.conflicts[index] = replacement
				this.persist()
				return
			}
		}

		// fail hard here - calling code has to guarantee that blockage to update will be found
		throw new Error('failed to update blockage')
	}

	onBlockage(blockage: Blockage/*, details: BlockageDetails*/) {

		const conflict = makePersistentConflict(blockage)
		this.conflicts.push(conflict)
		this.persist()

		this.eventTriggers.reportBlockage(blockage)
	}

	/**
	 * Check if any branch bots have moved past their conflict CLs
	 *
	 * This happens before a tick, so that all other bots will have had a chance
	 * to update, so that resolving changelists will have been seen
	 */
	preBranchBotTick(lastCl: number) {
		// quick scan - no need to rebuild conflict list if there are no changes
		let anyResolutions = false
		for (const conflict of this.conflicts) {
			if (lastCl >= conflict.cl) {
				anyResolutions = true
				break
			}
		}

		if (anyResolutions) {
			const remainingConflicts: PersistentConflict[] = []
			const resolvedConflicts: PersistentConflict[] = []
			for (const conflict of this.conflicts) {
				if (lastCl < conflict.cl) {
					remainingConflicts.push(conflict)
					continue
				}

				resolvedConflicts.push(conflict)
				if (!conflict.resolution) {
					if (conflict.targetBranchName) {
						// didn't work out what this was: put an innocuous message
						conflict.resolution = Conflicts.RESOLUTION_DUNNO
					}
					else {
						// at time of writing, must have been a syntax error
						conflict.resolution = Conflicts.RESOLUTION_RESOLVED
						conflict.resolvingAuthor = conflict.author
					}

				}

				if (!conflict.timeTakenToResolveSeconds) {
					// fall back to time of this event being fired - should be at most 30 seconds too long
					setResolvedTimeAsNow(conflict)
				}
			}

			this.setConflicts(remainingConflicts)

			// fire events after updating the conflicts state
			for (const conflict of resolvedConflicts) {
				this.eventTriggers.reportBranchUnblocked(conflict)
			}
		}
	}

	/** Peek at every changelist that comes by, to see if it's a change intended to resolve existing conflicts */
	onChangeParsed(change: ChangeInfo) {
		this.eventTriggers.reportChangeParsed(change)

		for (const conflict of this.conflicts) {

			// for this branch, see if a conflicting merge has been cancelled
			if (conflict.cl !== change.cl) {
				continue
			}

			// this is the same CL! Does the target still exist?
			let hasTarget = false
			if (change.targets) {
				for (const action of change.targets) {
					if (action.branch.upperName === conflict.targetBranchName) {
						hasTarget = true
						break
					}
				}
			}

			if (!hasTarget) {
				conflict.resolution = Conflicts.RESOLUTION_CANCELLED
				setResolvedTimeAsNow(conflict)
				this.setConflicts(this.conflicts.filter(el => el !== conflict))
				this.eventTriggers.reportBranchUnblocked(conflict)

				// this change is a cancellation - no need for further checking
				return
			}
		}
	}

	/** This is where we actually confirm that a conflict has been resolved */
	onAlreadyIntegrated(change: AlreadyIntegrated) {
		this.eventTriggers.reportAlreadyIntegrated(change)

		const conflict = this.find(change.action.branch, change.change.source_cl)
		if (conflict) {
			conflict.resolution = Conflicts.RESOLUTION_RESOLVED
		}
	}

	// If a forced CL is the same as a conflict CL, the conflict as skipped.
	// Otherwise we'll notify that the branch has been forced to a CL
	onForcedLastCl(forced: ForcedCl) {
		this.eventTriggers.reportForcedLastCl(forced)
		if (!this.notifyConflictSkipped(forced)) {
			this.eventTriggers.reportNonSkipLastClChange(forced)
		}
	}

	// called for changes relating to all *other* bots
	onGlobalChange(change: ChangeInfo) {
		for (const conflict of this.conflicts) {
			// match using source_cl - ideally would match source branch as well (two routes for same source CL?)
			if (conflictMatches(conflict, change.source_cl, change.branch)) {
				// for other branches, see if the change has resolved any conflicts
				conflict.resolvingCl = change.cl
				conflict.resolvingAuthor = change.author
				setResolvedTimeAsNow(conflict)
			}
		}
	}

	private notifyConflictSkipped(forced: ForcedCl) {
		if (forced.forcedCl <= forced.previousCl) {
			return false
		}

		const remainingConflicts: PersistentConflict[] = []
		const unblocksToReport: PersistentConflict[] = []

		for (const conflict of this.conflicts) {
			// If the forced CL is the same as our conflict CL, count this conflict as skipped
			if (conflict.cl === forced.forcedCl) {
				conflict.resolvingAuthor = forced.culprit
				conflict.resolution = Conflicts.RESOLUTION_SKIPPED
				conflict.resolvingReason = forced.reason
				setResolvedTimeAsNow(conflict)
				unblocksToReport.push(conflict)
			}
			else {
				remainingConflicts.push(conflict)
			}
		}

		if (unblocksToReport.length === 0) {
			return false
		}

		this.setConflicts(remainingConflicts)

		for (const unblock of unblocksToReport) {
			this.eventTriggers.reportBranchUnblocked(unblock)
		}
		return true
	}

	persist() {
		this.persistence.set('conflicts', this.conflicts)
	}

	find(targetBranch: Branch | null, sourceCl: number): PersistentConflict | null
	find(pending: PendingChange): PersistentConflict | null

	find(arg0: Branch | null | PendingChange, sourceCl?: number): PersistentConflict | null {
		let targetBranch
		const maybePending = arg0 as (PendingChange | null)
		if (maybePending && maybePending.action) {
			targetBranch = maybePending.action.branch
			sourceCl = maybePending.change.source_cl
		}
		else {
			targetBranch = arg0 as (Branch | null)
		}

		for (const conflict of this.conflicts) {
			if (conflictMatches(conflict, sourceCl!, targetBranch)) {
				return conflict
			}
		}

		return null
	}

	private setConflicts(conflicts: PersistentConflict[]) {
		this.conflicts = conflicts
		this.persist()
	}

	private conflicts: PersistentConflict[] = []
}
