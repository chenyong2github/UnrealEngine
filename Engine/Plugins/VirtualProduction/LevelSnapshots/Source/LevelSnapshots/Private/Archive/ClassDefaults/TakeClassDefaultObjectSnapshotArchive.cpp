// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/ClassDefaults/TakeClassDefaultObjectSnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "WorldSnapshotData.h"

void UE::LevelSnapshots::Private::FTakeClassDefaultObjectSnapshotArchive::SaveClassDefaultObject(FClassSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* SerializedObject)
{
	FTakeClassDefaultObjectSnapshotArchive SaveClass(InObjectData, InSharedData, SerializedObject);
	SerializedObject->Serialize(SaveClass);
	InObjectData.ObjectFlags = SerializedObject->GetFlags();
	InObjectData.ClassPath = SerializedObject->GetClass();
	InObjectData.ClassFlags = SerializedObject->GetClass()->GetFlags();
}

UE::LevelSnapshots::Private::FTakeClassDefaultObjectSnapshotArchive::FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject)
	:
	Super(InObjectData, InSharedData, false, InSerializedObject)
{}
