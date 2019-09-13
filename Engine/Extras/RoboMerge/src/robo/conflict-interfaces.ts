// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// made to be minimal/serialisable
export interface PersistentConflict {
	// upper case branch names
	blockedBranchName: string
	targetBranchName?: string

	cl: number
	sourceCl: number
	author: string
	owner: string

	kind: string
	time: Date

	autoShelfCl?: number
	nagged: boolean

	resolution: string

	resolvingCl?: number
	resolvingAuthor?: string
	timeTakenToResolveSeconds?: number
	resolvingReason?: string
}
