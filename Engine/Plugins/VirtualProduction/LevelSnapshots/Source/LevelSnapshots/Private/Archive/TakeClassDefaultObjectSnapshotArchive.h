// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SnapshotArchive.h"

struct FObjectSnapshotData;
struct FWorldSnapshotData;
class UObject;

/* Used when we're taking a snapshot of the world. */
class FTakeClassDefaultObjectSnapshotArchive final : public FSnapshotArchive
{
	using Super = FSnapshotArchive;
public:

	static FTakeClassDefaultObjectSnapshotArchive MakeArchiveForSavingClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData);
	static FTakeClassDefaultObjectSnapshotArchive MakeArchiveForRestoringClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData);

	//~ Begin FSnapshotArchive Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	//~ End FSnapshotArchive Interface

private:
	
	FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading);
	
};
