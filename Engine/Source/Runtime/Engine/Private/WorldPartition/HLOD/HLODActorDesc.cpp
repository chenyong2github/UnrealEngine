// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDesc.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

FHLODActorDesc::FHLODActorDesc(const FWorldPartitionActorDescData& DescData, const TArray<FGuid>& InSubActors, const FSoftObjectPath& InHLODLayer)
	: FWorldPartitionActorDesc(DescData)
	, SubActors(InSubActors)
	, HLODLayer(InHLODLayer)
{
}

FHLODActorDesc::FHLODActorDesc(AActor* InActor)
	: FWorldPartitionActorDesc(InActor)
{
	if (AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(InActor))
	{
		SubActors = HLODActor->GetSubActors();
		HLODLayer = HLODActor->GetHLODLayer();
	}
}

void FHLODActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << SubActors;
	HashBuilder << HLODLayer.ToString();
}

#endif
