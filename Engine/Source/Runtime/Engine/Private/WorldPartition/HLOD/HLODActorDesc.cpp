// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDesc.h"

#if WITH_EDITOR
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

bool FHLODActorDesc::Init(const AActor* InActor)
{
	if (FWorldPartitionActorDesc::Init(InActor))
	{
		const AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(InActor);
		SubActors = HLODActor->GetSubActors();
		HLODLayer = HLODActor->GetHLODLayer();

		UpdateHash();
		return true;
	}

	return false;
}

void FHLODActorDesc::SerializeMetaData(FActorMetaDataSerializer* Serializer)
{
	FWorldPartitionActorDesc::SerializeMetaData(Serializer);

	FString SubActorsGUIDsStr;
	if (Serializer->IsWriting())
	{
		if (SubActors.Num())
		{
			for (const FGuid& ActorGUID : SubActors)
			{
				SubActorsGUIDsStr += ActorGUID.ToString() + TEXT(";");
			}
			SubActorsGUIDsStr.RemoveFromEnd(TEXT(";"));
		}
	}

	Serializer->Serialize(TEXT("HLODSubActors"), SubActorsGUIDsStr);

	if (Serializer->IsReading())
	{
		TArray<FString> SubActorsStr;
		if (SubActorsGUIDsStr.ParseIntoArray(SubActorsStr, TEXT(";")))
		{
			Algo::Transform(SubActorsStr, SubActors, [](const FString& ActorGuidStr)
			{
				FGuid ActorGuid;
				if (!FGuid::Parse(ActorGuidStr, ActorGuid))
				{
					ActorGuid.Invalidate();
				}
				return ActorGuid;
			});

			Algo::RemoveIf(SubActors, [](const FGuid& InGuid)
			{
				return !InGuid.IsValid();
			});
		}
	}

	FString HLODLayerStr;
	if (Serializer->IsWriting())
	{
		HLODLayerStr = HLODLayer.ToString();
	}
	
	Serializer->Serialize(TEXT("HLODSubActors"), HLODLayerStr);
	
	if (Serializer->IsReading())
	{
		HLODLayer = HLODLayerStr;
	}
}

void FHLODActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << SubActors;
	HashBuilder << HLODLayer.ToString();
}

#endif
