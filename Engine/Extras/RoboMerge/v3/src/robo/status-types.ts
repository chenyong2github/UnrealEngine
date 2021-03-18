import { NodeOptions } from './branchdefs';

export type FailureKind =
	'Integration error' |
	'Exclusive check-out' |
	'Merge conflict' |
	'Commit failure' |
	'Syntax error' |
	'Disallowed files' |
	'Too many files' |
	'Conversion to edits failure' |
	'Unit Test error'

export enum AvailableTypeEnum {
	'available'
}

export interface AvailableInfo {
	type: keyof typeof AvailableTypeEnum
	startedAt: Date
}

export enum BlockagePauseTypeEnum {
	'branch-stop'
}

export enum ManualPauseTypeEnum {
	'manual-lock'
}

export interface ManualPauseInfo {
	type: keyof typeof ManualPauseTypeEnum
	owner: string
	message: string
	startedAt: Date
}

export interface BlockagePauseInfoMinimal {
	type: keyof typeof BlockagePauseTypeEnum
	owner: string
	message: string

	change?: number
}

export interface BlockagePauseInfo extends BlockagePauseInfoMinimal {
	startedAt: Date
	endsAt?: Date

	// used for blockages
	author?: string
	targetBranchName?: string
	targetStream?: string  // Used by create shelf feature to filter user workspaces
	sourceCl?: number
	source?: string

	// Set when the Acknowledge button is clicked
	acknowledger?: string
	acknowledgedAt?: Date
}

export type PauseStatusFields = {
	available: AvailableInfo
	blockage: BlockagePauseInfo
	manual_pause: ManualPauseInfo // careful	, startedAt gets written out as string
}

export type AnyStateInfo = ManualPauseInfo | BlockagePauseInfo | AvailableInfo

export type BotStatusFields = Partial<PauseStatusFields> & {
	display_name: string
	last_cl: number

	headCL?: number

	is_active: boolean
	is_available: boolean
	is_blocked: boolean
	is_paused: boolean

	status_msg?: string
	status_since?: string

	lastBlockage?: number

	retry_cl?: number
}

export type EdgeStatusFields = BotStatusFields & {
	name: string
	target: string
	targetStream?: string
	rootPath: string

	resolver: string
	disallowSkip: boolean
	incognitoMode: boolean
	excludeAuthors: string[]

	lastGoodCL?: number
	lastGoodCLJobLink?: string
	lastGoodCLDate?: Date
}

export type NodeStatusFields = BotStatusFields & {
	queue: QueuedChange[]

	conflicts: ConflictStatusFields[]
	edges: { [key: string]: EdgeStatusFields }
}

// only includes stuff directly used by web code
export type BranchDefForStatus = {
	// BranchBase
	name: string
	rootPath: string

	// Branch
	upperName: string
	visibility: string

	isMonitored: boolean


	aliases: any
	blockAssetTargets: string[]
	bot: string
	config: NodeOptions
	convertIntegratesToEdits: boolean
	defaultFlow: string[]
	flowsTo: string[]
	forceFlowTo: string[]
}

export type BranchStatus = Partial<NodeStatusFields> & {
	def: BranchDefForStatus
	bot: string

	branch_spec_cl: number
}

export type ConflictStatusFields = {
	cl: number
	sourceCl: number
	target?: string
	targetStream?: string 
	kind: FailureKind
	author: string
	owner: string
}

type GraphBotError = {
	nodeBot: string
	error: string
}

export interface GraphBotState {
	isRunningBots: boolean
	lastBranchspecCl?: number
	lastError?: GraphBotError
}

export type User = {userName: string, displayName: string, privileges?: string[]}
export type UserStatusData = {
	started: Date
	version: string
	user: User
	branches: BranchStatus[]
	botStates: [string, GraphBotState][]
	insufficientPrivelege?: boolean
}


// queued change vs reconsider:

//  reconsider is the name of user facing 'queue change' operation, but also the top level call to add to the queue,
//  used by stomp etc.

// slightly sneaky that since NodeBot.reconsider's additional args are optional, it fulfils both of these, i.e.
// implements the base bot reconsider function, and it also the 'queue change' function.

// ReconsiderArgs are the additional args used by stomp etc. I'm adding commandOverride to them, even thoguh that's a
// slightly different pattern (unless it breaks something)

export type ReconsiderArgs = {
	additionalFlags: string[]
	workspace: string
	targetBranchName: string
	description: string
	commandOverride: string
}

export type QueuedChange = Partial<ReconsiderArgs> & {
	cl: number
	who: string
}
