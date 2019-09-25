// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import {BotConfig, BranchBase, BranchOptions} from './branchdefs'
import {RoboWorkspace} from '../common/perforce'
import {TickJournal} from './tick-journal'

export interface Bot {
	start(): Promise<void>;
	tick(next: () => (Promise<any> | void)): void;

	fullName: string;

	isRunning: boolean;
	isActive: boolean;

	lastCl: number

	tickJournal?: TickJournal;
}

export interface BranchBotIPC extends Bot {
	pause(info: PauseInfoMinimal): void
	unpause(reason: string): void
	acknowledge(acknowledger: string) : void

	forceSetLastCl(cl: number, who: string, reason: string): number
	reconsider(cl: number, additionalFlags: string[], who: string): void

	verbose: boolean
}

export interface BranchBotInterface extends BranchBotIPC {
	branch: Branch

	getNumConflicts(): number
	applyStatus(status: BranchStatus): void
}

export interface BranchMapInterface {
	botname: string
	config: BotConfig

	branches: Branch[]

	// todo: make a better utility for this
	_computeReachableFrom(visited: Set<Branch>, flowKey: string, branch: Branch): Set<string>

	getBranch(name: string): Branch | undefined
}

export interface BranchStatus {
	def: Branch
	bot: string

	branch_spec_cl: number

	// will add more structure to this for filtering
	[other: string]: any
}

export interface Branch extends BranchBase {
	bot?: BranchBotInterface
	parent: BranchMapInterface
	workspace: RoboWorkspace

	branchspec: Object // ought to be Map<string, BranchSpec>
	upperName: string
	stream?: string
	config: BranchOptions
	reachable?: string[]
	forcedDownstream?: string[]
	enabled: boolean
	convertIntegratesToEdits: boolean
	visibility: string[] | string
	blockAssetTargets: Set<string>

	isMonitored: boolean // property
}

export interface BotSummary {
	numConflicts: number
	numPauses: number
}

export interface Target {
	branch: Branch
	mergeMode: string
}

export interface MergeAction {
	branch: Branch
	mergeMode: string
	furtherMerges: Target[]
	flags: Set<string>
	description?: string  // gets filled in for immediate targets in _mergeCl
}

export interface TargetInfo {
	errors?: string[]
	allDownstream?: Branch[]

	targets?: MergeAction[]

	// informational field, indicating original change was (probably) a null merge
	propagatingNullMerge: boolean

	owner?: string
	author: string
}

export interface ChangeInfo extends TargetInfo {
	branch: Branch
	cl: number
	source_cl: number
	isManual: boolean
	authorTag?: string
	source: string
	description: string
}

export interface PendingChange {
	change: ChangeInfo
	action: MergeAction
	newCl: number
}

export interface Blockage {
	change: ChangeInfo
	kind: string		// short description of integration error or conflict
	description: string	// detailed description (can be very long - don't want to store this)
	owner: string
	ownerEmail: Promise<string | null>

	time: Date

	id?: string 		// id if a resolution case was created (not for manual/reconsider ops)
}

export interface Conflict extends Blockage {
	action: MergeAction
	shelfCl?: number
}

// info for use by resolver page
export interface ConflictInfo {
	blockedBranchName: string
	targetBranchName: string

	cl: number
	newCl: number

	author: string
	owner?: string
}

export interface AlreadyIntegrated {
	change: ChangeInfo
	action: MergeAction
}

// TO DELETE
		export interface AutoShelfRecord {

		// may only need source/branch as key to know not to send new blockage event

			source_cl: number
			shelve_cl: number
			branchName: string
			time: Date
			sentNagEmail: boolean
		}

		export interface PauseInfoMinimal {
			type: string
			owner: string
			message: string
		}

		// goes directly into status object as pause_info
		export interface PauseInfo extends PauseInfoMinimal {
			startedAt: Date
			endsAt?: Date

			// used for blockages
			author?: string
			targetBranchName?: string
			change?: number
			sourceCl?: number
			shelfCl?: number
			source?: string

			// Set when the Acknowledge button is clicked
			acknowledger?: string
			acknowledgedAt?: Date
		}

export interface ForcedCl {
	branch: Branch
	forcedCl: number
	previousCl: number
	culprit: string
	reason: string
}
