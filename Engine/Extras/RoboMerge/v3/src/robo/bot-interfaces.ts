// Copyright Epic Games, Inc. All Rights Reserved.

import { Branch, BranchArg, BranchGraphInterface, OperationResult, StompVerification } from "./branch-interfaces";
import { BlockagePauseInfoMinimal, BranchStatus, ReconsiderArgs } from "./status-types"
import { TickJournal } from "./tick-journal";

export interface Bot {
	start(): Promise<void>;
	tick(): Promise<boolean>;

	fullName: string;
	fullNameForLogging: string

	isRunning: boolean;
	isActive: boolean;

	lastCl: number

	tickJournal?: TickJournal;
}

export interface NodeBotIPC extends BotIPC {
	reconsider(instigator: string, changeCl: number, additionalArgs?: Partial<ReconsiderArgs>): void

	createShelf(owner: string, workspace: string, changeCl: number, targetBranchName: string): OperationResult

	verifyStomp(changeCl: number, targetBranchName: string): Promise<StompVerification>
	stompChanges(owner: string, changeCl: number, targetBranchName: string): Promise<OperationResult>
}



export interface BotIPC extends Bot, IPCControls {
}

export interface IPCControls {
	block(info: BlockagePauseInfoMinimal): void
	unblock(reason: string): void

	pause(message: string, owner: string): void
	unpause(): void

	reconsider(instigator: string, changeCl: number, additionalArgs?: Partial<ReconsiderArgs>): void

	acknowledge(acknowledger: string, changeCl: number) : OperationResult
	unacknowledge(changeCl: number) : OperationResult
	
	forceSetLastClWithContext(value: number, culprit: string, reason: string): number
}

export interface NodeBotInterface extends NodeBotIPC {
	branch: Branch
	lastCl: number

	readonly branchGraph: BranchGraphInterface
	//readonly edges: Map<string, EdgeBot>
	hasEdge(branchName: BranchArg): boolean
	getEdgeIPCControls(branchName: BranchArg): IPCControls | undefined

	getNumConflicts(): number
	applyStatus(status: BranchStatus): void
}
