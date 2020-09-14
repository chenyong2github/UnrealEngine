// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HLODLayer.cpp: UHLODLayer class implementation
=============================================================================*/

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODBuilder.h"
#endif

UHLODLayer::UHLODLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

TArray<AWorldPartitionHLOD*> UHLODLayer::GenerateHLODForCell(UWorldPartition* InWorldPartition, FName InCellName, FBox InCellBounds, float InCellLoadingRange, const TSet<FGuid>& InCellActors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::GenerateHLODForCell);

	TMap<UHLODLayer*, TArray<AActor*>> HLODLayersActors;
	for (const FGuid& ActorGuid : InCellActors)
	{
		FWorldPartitionActorDesc& ActorDesc = *InWorldPartition->GetActorDesc(ActorGuid);

		// Skip editor only actors - they might not be loaded and doesn't contribute to HLODs anyway
		if (ActorDesc.GetActorIsEditorOnly())
		{
			continue;
		}

		AActor* Actor = ActorDesc.GetActor();
		check(Actor);

		if (ShouldIncludeInHLOD(Actor))
		{
			UHLODLayer* HLODLayer = UHLODLayer::GetHLODLayer(Actor);
			if (HLODLayer)
			{
				HLODLayersActors.FindOrAdd(HLODLayer).Add(Actor);
			}
		}
	}

	TArray<AWorldPartitionHLOD*> HLODActors;
	for (const auto& HLODLayerActors : HLODLayersActors)
	{
		HLODActors += FHLODBuilderUtilities::BuildHLODs(InWorldPartition, InCellName, InCellBounds, InCellLoadingRange, HLODLayerActors.Key, HLODLayerActors.Value);
	}
	return HLODActors;
}

bool UHLODLayer::ShouldIncludeInHLOD(UPrimitiveComponent* InComponent, int32 InLevelIndex)
{
	if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(InComponent))
	{
		if (!SMC->GetStaticMesh())
		{
			return false;
		}
	}

	if (InComponent->IsEditorOnly())
	{
		return false;
	}

	if (InComponent->bHiddenInGame)
	{
		return false;
	}

	if (!InComponent->ShouldGenerateAutoLOD(InLevelIndex))
	{
		return false;
	}

	return true;
}

UHLODLayer* UHLODLayer::GetHLODLayer(const AActor* InActor)
{
	if (UHLODLayer* HLODLayer = InActor->GetHLODLayer())
	{
		return HLODLayer;
	}
	if (UWorldPartition* WorldPartition = InActor->GetWorld()->GetWorldPartition())
	{
		return WorldPartition->DefaultHLODLayer;
	}
	return nullptr;
}

#endif // WITH_EDITOR


bool UHLODLayer::ShouldIncludeInHLOD(AActor* InActor)
{
	if (!InActor)
	{
		return false;
	}

	if (InActor->IsHidden())
	{
		return false;
	}

	if (InActor->IsEditorOnly())
	{
		return false;
	}

	if (InActor->HasAnyFlags(RF_Transient))
	{
		return false;
	}

	if (InActor->IsTemplate())
	{
		return false;
	}

	if (InActor->IsPendingKill())
	{
		return false;
	}

	if (!InActor->bEnableAutoLODGeneration)
	{
		return false;
	}

	FVector Origin, Extent;
	InActor->GetActorBounds(false, Origin, Extent);
	if (Extent.SizeSquared() <= 0.1)
	{
		return false;
	}

	return true;
}
