// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDesc.h"

#if WITH_EDITOR
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

void FHLODActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(InActor);
	SubActors = HLODActor->GetSubActors();
	HLODLayer = HLODActor->GetHLODLayer();
}

void FHLODActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);

	Ar << SubActors;

	FString HLODLayerStr;
	if (Ar.IsSaving())
	{
		HLODLayerStr = HLODLayer.ToString();
	}
	
	Ar << HLODLayerStr;
	
	if (Ar.IsLoading())
	{
		HLODLayer = HLODLayerStr;
	}
}
#endif
