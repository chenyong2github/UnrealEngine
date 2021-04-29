// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/ApplySnapshotDataArchive.h"

#include "Data/BaseObjectInfo.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsStats.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"

FApplySnapshotDataArchive FApplySnapshotDataArchive::MakeDeserializingIntoWorldObject(const FBaseObjectInfo& InObjectInfo, const FPropertySelection* InSelectedProperties)
{
	return FApplySnapshotDataArchive(InObjectInfo, InSelectedProperties);
}

FApplySnapshotDataArchive FApplySnapshotDataArchive::MakeForDeserializingTransientObject(const FBaseObjectInfo& InObjectInfo)
{
	return FApplySnapshotDataArchive(InObjectInfo);
}

bool FApplySnapshotDataArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ShouldSkipProperty_Loading"), STAT_ShouldSkipProperty_Loading, STATGROUP_LevelSnapshots);
	checkf(IsLoading(), TEXT("Should only be used to write data to objects"));
	
	bool bShouldSkipProperty = Super::ShouldSkipProperty(InProperty);
	if (!bShouldSkipProperty && SelectedProperties)
	{
		bShouldSkipProperty = !SelectedProperties.GetValue()->ShouldSerializeProperty(GetSerializedPropertyChain(), InProperty);
	}

	return bShouldSkipProperty;
}

FApplySnapshotDataArchive::FApplySnapshotDataArchive(const FBaseObjectInfo& InObjectInfo, const FPropertySelection* InSelectedProperties)
        :
        SelectedProperties(InSelectedProperties ? InSelectedProperties : TOptional<const FPropertySelection*>())
{
	SetReadOnlyObjectInfo(InObjectInfo);
	
	Super::SetWantBinaryPropertySerialization(false);
	Super::SetIsTransacting(false);
	Super::SetIsPersistent(true);
    
	Super::SetIsLoading(true);			
}
