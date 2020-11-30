// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HLODLayer.cpp: UHLODLayer class implementation
=============================================================================*/

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"

#if WITH_EDITOR
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODBuilder.h"
#endif

UHLODLayer::UHLODLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, CellSize(3200)
	, LoadingRange(12800)
#endif
{
#if WITH_EDITORONLY_DATA
	HLODMaterial = ConstructorHelpers::FObjectFinder<UMaterialInterface>(TEXT("/Engine/EngineMaterials/BaseFlattenMaterial")).Object;
#endif
}

#if WITH_EDITOR

TArray<AWorldPartitionHLOD*> UHLODLayer::GenerateHLODForCell(UWorldPartition* InWorldPartition, FName InCellName, const FBox& InCellBounds, uint32 InHLODLevel, const TArray<AActor*>& InCellActors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::GenerateHLODForCell);

	TMap<UHLODLayer*, TArray<const AActor*>> HLODLayersActors;
	for (const AActor* Actor : InCellActors)
	{
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
		HLODActors += FHLODBuilderUtilities::BuildHLODs(InWorldPartition, InCellName, InCellBounds, HLODLayerActors.Key, InHLODLevel, HLODLayerActors.Value);
	}
	return HLODActors;
}

bool UHLODLayer::ShouldIncludeInHLOD(const UPrimitiveComponent* InComponent, int32 InLevelIndex)
{
	if (const UStaticMeshComponent* SMC = Cast<const UStaticMeshComponent>(InComponent))
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

	// Only fallback to the default HLODLayer for the first level of HLOD
	bool bIsHLOD0 = !InActor->IsA<AWorldPartitionHLOD>();
	if (bIsHLOD0) 
	{
		if (UWorldPartition* WorldPartition = InActor->GetWorld()->GetWorldPartition())
		{
			return WorldPartition->DefaultHLODLayer;
		}
	}
	return nullptr;
}

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA

FName UHLODLayer::GetRuntimeGridName(uint32 InLODLevel, int32 InCellSize, float InLoadingRange)
{
	return *FString::Format(TEXT("HLOD{0}_{1}m_{2}m"), { InLODLevel, int32(InCellSize * 0.01f), int32(InLoadingRange * 0.01f)});
}

FName UHLODLayer::GetRuntimeGrid(uint32 InHLODLevel) const
{
	return GetRuntimeGridName(InHLODLevel, CellSize, LoadingRange);
}

#endif // WITH_EDITOR

bool UHLODLayer::ShouldIncludeInHLOD(const AActor* InActor)
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
