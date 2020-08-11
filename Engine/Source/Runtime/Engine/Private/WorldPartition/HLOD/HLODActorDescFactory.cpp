// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/WorldPartition.h"
#include "ActorRegistry.h"
#include "Editor.h"
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"

FWorldPartitionActorDesc* FHLODActorDescFactory::CreateInstance(const FWorldPartitionActorDescInitData& ActorDescInitData)
{
	FWorldPartitionActorDescData Data;
	if (!ReadMetaData(ActorDescInitData, Data))
	{
		return nullptr;
	}

	TArray<FGuid> SubActorsGUIDs;
	FString MetadataStr;
	static const FName NAME_HLODSubActors(TEXT("HLODSubActors"));
	if (!FActorRegistry::ReadActorMetaData(NAME_HLODSubActors, MetadataStr, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	TArray<FString> SubActorsStr;
	if (MetadataStr.ParseIntoArray(SubActorsStr, TEXT(";")))
	{
		Algo::Transform(SubActorsStr, SubActorsGUIDs, [](const FString& ActorGuidStr)
		{
			FGuid ActorGuid;
			if (!FGuid::Parse(ActorGuidStr, ActorGuid))
			{
				ActorGuid.Invalidate();
			}
			return ActorGuid;
		});

		Algo::RemoveIf(SubActorsGUIDs, [](const FGuid& Guid)
		{
			return !Guid.IsValid();
		});
	}

	return new FHLODActorDesc(Data, SubActorsGUIDs);
}

FWorldPartitionActorDesc* FHLODActorDescFactory::CreateInstance(AActor* InActor)
{
	return new FHLODActorDesc(InActor);
}
#endif