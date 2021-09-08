// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/RestorationEvents/ApplySnapshotPropertiesScope.h"

#include "LevelSnapshotsModule.h"

FApplySnapshotPropertiesScope::FApplySnapshotPropertiesScope(FApplySnapshotPropertiesParams Params)
	:
	Params(Params)
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPreApplySnapshotProperties(Params);
}

FApplySnapshotPropertiesScope::~FApplySnapshotPropertiesScope()
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPostApplySnapshotProperties(Params);
}
