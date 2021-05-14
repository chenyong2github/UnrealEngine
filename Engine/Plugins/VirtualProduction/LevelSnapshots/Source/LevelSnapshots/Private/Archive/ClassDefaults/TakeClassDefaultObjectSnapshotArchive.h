// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Archive/ClassDefaults/BaseClassDefaultArchive.h"

class UObject;
struct FObjectSnapshotData;
struct FWorldSnapshotData;

/* Used when we're taking a snapshot of the world. */
class FTakeClassDefaultObjectSnapshotArchive final : public FBaseClassDefaultArchive
{
	using Super = FBaseClassDefaultArchive;
public:

	static void SaveClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* SerializedObject);

private:
	
	FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject);
};
