// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/ClassDefaults/TakeClassDefaultObjectSnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "WorldSnapshotData.h"

void FTakeClassDefaultObjectSnapshotArchive::SaveClassDefaultObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* SerializedObject)
{
	FTakeClassDefaultObjectSnapshotArchive SaveClass(InObjectData, InSharedData, SerializedObject);
	SerializedObject->Serialize(SaveClass);
}

FTakeClassDefaultObjectSnapshotArchive::FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject)
	:
	Super(InObjectData, InSharedData, false, InSerializedObject)
{}
