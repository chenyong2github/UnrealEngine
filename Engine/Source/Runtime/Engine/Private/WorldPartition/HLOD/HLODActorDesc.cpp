// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDesc.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODActor.h"

FHLODActorDesc::FHLODActorDesc(const FWorldPartitionActorDescData& DescData, const TArray<FGuid>& InSubActors)
	: FWorldPartitionActorDesc(DescData)
{
	SubActors = InSubActors;
}

FHLODActorDesc::FHLODActorDesc(AActor* InActor)
	: FWorldPartitionActorDesc(InActor)
{
	if (AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(InActor))
	{
		SubActors = HLODActor->GetSubActors();
	}
}

void FHLODActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << SubActors;
}

#endif
