// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/RestorationEvents/ApplySnapshotPropertiesScope.h"

#include "LevelSnapshotsModule.h"

FApplySnapshotPropertiesScope::FApplySnapshotPropertiesScope(const FApplySnapshotPropertiesParams& InParams)
	:
	Params(InParams)
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPreApplySnapshotProperties(InParams);
}

FApplySnapshotPropertiesScope::~FApplySnapshotPropertiesScope()
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPostApplySnapshotProperties(Params);
}
