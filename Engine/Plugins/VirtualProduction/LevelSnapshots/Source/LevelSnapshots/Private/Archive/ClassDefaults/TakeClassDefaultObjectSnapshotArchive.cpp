// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/ClassDefaults/TakeClassDefaultObjectSnapshotArchive.h"

#include "LevelSnapshotsLog.h"
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

void UE::LevelSnapshots::Private::FTakeClassDefaultObjectSnapshotArchive::OnAddObjectDependency(int32 ObjectIndex, UObject* Object) const
{
	if (Object)
	{
		const bool bIsClassDefault = Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
		const bool bIsPointingToDefaultSubobject = Object->HasAnyFlags(RF_DefaultSubObject);
		const bool bIsPointingToSelf = Object == GetSerializedObject();
		const bool bIsPointingToSubobject = Object->IsIn(GetSerializedObject());
		const bool bShouldSkip = bIsClassDefault || bIsPointingToDefaultSubobject || bIsPointingToSelf || bIsPointingToSubobject;

		// We don't ever want to interfere with the default subobjects that the class archetype assigns so the reference is marked so we know it is supposed to be skipped when restoring 
		if (bShouldSkip)
		{
			GetSharedData().SerializedReferenceMetaData.FindOrAdd(ObjectIndex).Flags |= EObjectReferenceSnapshotFlags::SkipWhenSerializingArchetypeData;
		}
	}
}

UE::LevelSnapshots::Private::FTakeClassDefaultObjectSnapshotArchive::FTakeClassDefaultObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject)
	:
	Super(InObjectData, InSharedData, false, InSerializedObject)
{
#if UE_BUILD_DEBUG
	UE_LOG(LogLevelSnapshots, VeryVerbose, TEXT("FTakeClassDefaultObjectSnapshotArchive: %s (%s)"), *InSerializedObject->GetPathName(), *InSerializedObject->GetClass()->GetPathName());
#endif
}
