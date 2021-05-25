// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Archive/ClassDefaults/BaseClassDefaultArchive.h"

struct FObjectSnapshotData;
struct FWorldSnapshotData;
class UObject;

/* Used when we're taking a snapshot of the world. */
class FApplyClassDefaulDataArchive : public FBaseClassDefaultArchive
{
	using Super = FBaseClassDefaultArchive;
public:

	/* Archive will be used for reconstructing a CDO */
	static void SerializeClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InCDO);
	/* Archive will be used to to apply any non-object properties before the object receives its saved snapshot values. This is to handle when a CDO has changed its default values since a snapshot was taken. */
	static void RestoreChangedClassDefaults(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* ObjectToRestore);

protected:
	
	enum class ESerialisationMode
	{
		RestoringCDO,
		RestoringChangedDefaults
	};

	FApplyClassDefaulDataArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, ESerialisationMode InSerialisationMode);
};
