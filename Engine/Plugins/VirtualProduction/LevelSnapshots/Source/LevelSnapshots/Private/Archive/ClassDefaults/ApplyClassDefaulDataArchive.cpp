// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/ClassDefaults/ApplyClassDefaulDataArchive.h"

#include "LevelSnapshotsModule.h"
#include "ObjectSnapshotData.h"
#include "WorldSnapshotData.h"

void UE::LevelSnapshots::Private::FApplyClassDefaulDataArchive::SerializeClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InCDO)
{
	check(!FLevelSnapshotsModule::GetInternalModuleInstance().ShouldSkipClassDefaultSerialization(InCDO->GetClass()));
	
	FApplyClassDefaulDataArchive Archive(InObjectData, InSharedData, InCDO, ESerialisationMode::RestoringCDO);
	InCDO->Serialize(Archive);
}

void UE::LevelSnapshots::Private::FApplyClassDefaulDataArchive::RestoreChangedClassDefaults(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore)
{
	FApplyClassDefaulDataArchive Archive(InObjectData, InSharedData, InObjectToRestore, ESerialisationMode::RestoringChangedDefaults);
	InObjectToRestore->Serialize(Archive);
}

UE::LevelSnapshots::Private::FApplyClassDefaulDataArchive::FApplyClassDefaulDataArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, ESerialisationMode InSerialisationMode)
	:
	Super(InObjectData, InSharedData, true, InObjectToRestore)
{
	// Only overwrite transient properties when actually serializing into a snapshot CDO
	ArSerializingDefaults = InSerialisationMode == ESerialisationMode::RestoringCDO;
}
