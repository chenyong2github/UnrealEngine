// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SnapshotArchive.h"

struct FObjectSnapshotData;
struct FWorldSnapshotData;
class UObject;

/* Used when we're taking a snapshot of the world. */
class FTakeWorldObjectSnapshotArchive final : public FSnapshotArchive
{
	using Super = FSnapshotArchive;
public:

	UE_NODISCARD static FTakeWorldObjectSnapshotArchive MakeArchiveForSavingWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject);

	//~ Begin FSnapshotArchive Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	//~ End FSnapshotArchive Interface
	
private:
	
	FTakeWorldObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject);
};
