// Copyright Epic Games, Inc. All Rights Reserved.

import {BranchMapInterface} from './branch-interfaces'
import {BotConfig, BranchMapDefinition} from './branchdefs'

export interface EngineInterface {
	branchMap: BranchMapInterface
	filename: string

	ensureStopping(): boolean
	reinitFromBranchMapsObject(config: BotConfig, branchMaps: BranchMapDefinition): Promise<void>
}
