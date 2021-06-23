// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Archive/SnapshotArchive.h"

class UObject;
struct FObjectSnapshotData;
struct FWorldSnapshotData;

/* Shared logic for serializing class defaults. */
class FBaseClassDefaultArchive : public FSnapshotArchive
{
	using Super = FSnapshotArchive;
public:

	//~ Begin FSnapshotArchive Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	//~ End FSnapshotArchive Interface

protected:
	
	//~ Begin FSnapshotArchive Interface
	virtual UObject* ResolveObjectDependency(int32 ObjectIndex) const override;
	//~ End FSnapshotArchive Interface
	
	FBaseClassDefaultArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading, UObject* InObjectToRestore);

private:
	
	bool IsPropertyReferenceToSubobjectOrClassDefaults(const FProperty* InProperty) const;
};
