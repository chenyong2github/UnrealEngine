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

	static FTakeWorldObjectSnapshotArchive MakeArchiveForSavingWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject);

	//~ Begin FSnapshotArchive Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	//~ End FSnapshotArchive Interface
	
private:
	
	FTakeWorldObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject);
	
	/* Set when saving a 'real' object. Unset when saving class default object. */
	UObject* OriginalObject;
	
};
