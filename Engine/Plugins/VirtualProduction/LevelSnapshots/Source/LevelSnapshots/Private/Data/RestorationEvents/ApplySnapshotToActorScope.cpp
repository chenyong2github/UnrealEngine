// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/RestorationEvents/ApplySnapshotToActorScope.h"

#include "LevelSnapshotsModule.h"

FApplySnapshotToActorScope::FApplySnapshotToActorScope(const FApplySnapshotToActorParams& Params)
	:
	Params(Params)
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPreApplySnapshotToActor(Params);
}

FApplySnapshotToActorScope::~FApplySnapshotToActorScope()
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPostApplySnapshotToActor(Params);
}
