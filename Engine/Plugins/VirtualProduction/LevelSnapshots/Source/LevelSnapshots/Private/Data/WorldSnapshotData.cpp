// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/WorldSnapshotData.h"

#include "LevelSnapshotsModule.h"
#include "Util/Property/PropertyIterator.h"

#include "Serialization/BufferArchive.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void FWorldSnapshotData::ForEachOriginalActor(TFunctionRef<void(const FSoftObjectPath&, const FActorSnapshotData&)> HandleOriginalActorPath) const
{
	for (auto OriginalPathIt = ActorData.CreateConstIterator(); OriginalPathIt; ++OriginalPathIt)
	{
		HandleOriginalActorPath(OriginalPathIt->Key, OriginalPathIt->Value);
	}	
}

bool FWorldSnapshotData::HasMatchingSavedActor(const FSoftObjectPath& OriginalObjectPath) const
{
	return ActorData.Contains(OriginalObjectPath);
}

namespace UE::LevelSnapshots::Private::Internal
{
	static void CollectActorReferences(FWorldSnapshotData& WorldData, FArchive& Ar)
	{
		for (auto ActorIt = WorldData.ActorData.CreateIterator(); ActorIt; ++ActorIt)
		{
			Ar << ActorIt->Key;
			Ar << ActorIt->Value.ActorClass;
		}
	}

	static void CollectClassDefaultReferences(FWorldSnapshotData& WorldData, FArchive& Ar)
	{
		for (auto ClassDefaultIt = WorldData.ClassDefaults.CreateIterator(); ClassDefaultIt; ++ClassDefaultIt)
		{
			Ar << ClassDefaultIt->Key;
		}
	}
	
	static void CollectReferencesAndNames(FWorldSnapshotData& WorldData, FArchive& Ar)
	{
		// References
		Ar << WorldData.SerializedObjectReferences;
		CollectActorReferences(WorldData, Ar);
		CollectClassDefaultReferences(WorldData, Ar);

		// Names
		Ar << WorldData.SerializedNames;
		Ar << WorldData.SnapshotVersionInfo.CustomVersions;

		// Serialize hardcoded property names
		for (int32 i = 0; i < static_cast<int32>(EName::MaxHardcodedNameIndex); ++i)
		{
			FName HardcodedName(static_cast<EName>(i));
			Ar << HardcodedName;
		}

		auto ProcessProperty = [&Ar](const FProperty* Property)
		{
			FName PropertyName = Property->GetFName();
			Ar << PropertyName;
		};
		auto ProcessStruct = [&Ar](UStruct* Struct)
		{
			FName StructName = Struct->GetFName();
			Ar << StructName;
		};
		
		FPropertyIterator(FWorldSnapshotData::StaticStruct(), ProcessProperty, ProcessStruct);
		FPropertyIterator(FSnapshotVersionInfo::StaticStruct(), ProcessProperty, ProcessStruct);
		FPropertyIterator(FActorSnapshotData::StaticStruct(), ProcessProperty, ProcessStruct);
		FPropertyIterator(FSubobjectSnapshotData::StaticStruct(), ProcessProperty, ProcessStruct);
		FPropertyIterator(FCustomSerializationData::StaticStruct(), ProcessProperty, ProcessStruct);
	}
}

bool FWorldSnapshotData::Serialize(FArchive& Ar)
{
	// When this struct is saved, the save algorithm collects references. It's faster if we just give it the info directly.
	if (Ar.IsObjectReferenceCollector())
	{
		UE::LevelSnapshots::Private::Internal::CollectReferencesAndNames(*this, Ar);
		return true;
	}
	
	return false;
}

void FWorldSnapshotData::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && !SnapshotVersionInfo.IsInitialized())
	{
		// Assets saved before we added version tracking need to receive versioning info of 4.27.
		// Skip snapshot version info because it did not exist yet at that time (you'll get migration bugs otherwise).
		const bool bWithoutSnapshotVersion = true;
		SnapshotVersionInfo.Initialize(bWithoutSnapshotVersion);
	}
}

#undef LOCTEXT_NAMESPACE