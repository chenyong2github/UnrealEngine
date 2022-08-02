// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Archive/ClassDefaults/BaseClassDefaultArchive.h"

class UObject;
struct FClassSnapshotData;
struct FObjectSnapshotData;
struct FWorldSnapshotData;

namespace UE::LevelSnapshots::Private
{
	/* Used when we're taking a snapshot of the world. */
	class FTakeClassDefaultObjectSnapshotArchive final : public FBaseClassDefaultArchive
	{
		using Super = FBaseClassDefaultArchive;
	public:

		static void SaveClassDefaultObject(FClassSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* SerializedObject);

	private:
	
		FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject);
	};
}


