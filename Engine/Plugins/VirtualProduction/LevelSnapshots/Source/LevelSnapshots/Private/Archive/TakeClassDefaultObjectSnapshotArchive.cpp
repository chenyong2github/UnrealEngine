// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakeClassDefaultObjectSnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "WorldSnapshotData.h"

FTakeClassDefaultObjectSnapshotArchive FTakeClassDefaultObjectSnapshotArchive::MakeArchiveForSavingClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData)
{
	return FTakeClassDefaultObjectSnapshotArchive(InObjectData, InSharedData, false);
}

FTakeClassDefaultObjectSnapshotArchive FTakeClassDefaultObjectSnapshotArchive::MakeArchiveForRestoringClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData)
{
	return FTakeClassDefaultObjectSnapshotArchive(InObjectData, InSharedData, true);
}

bool FTakeClassDefaultObjectSnapshotArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	return Super::ShouldSkipProperty(InProperty);
}

FTakeClassDefaultObjectSnapshotArchive::FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading)
	:
	Super(InObjectData, InSharedData, bIsLoading)
{
	// Description of CPF_Transient: "Property is transient: shouldn't be saved or loaded, except for Blueprint CDOs."
	ExcludedPropertyFlags = CPF_BlueprintAssignable | CPF_Deprecated;
	
	// Otherwise we are not allowed to serialize transient properties
	ArSerializingDefaults = true;
}
