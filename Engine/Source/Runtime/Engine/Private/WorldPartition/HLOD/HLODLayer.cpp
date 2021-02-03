// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HLODLayer.cpp: UHLODLayer class implementation
=============================================================================*/

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"

#if WITH_EDITOR
#include "Algo/Copy.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODBuilder.h"

#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
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

TArray<AWorldPartitionHLOD*> UHLODLayer::GenerateHLODForCell(UWorldPartition* InWorldPartition, FHLODGenerationContext* InContext, FName InCellName, const FBox& InCellBounds, uint32 InHLODLevel, const TArray<AActor*>& InCellActors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::GenerateHLODForCell);

	TMap<UHLODLayer*, TArray<const AActor*>> HLODLayersActors;
	for (AActor* Actor : InCellActors)
	{
		if (ShouldIncludeInHLOD(Actor))
		{
			UHLODLayer* HLODLayer = UHLODLayer::GetHLODLayer(Actor);
			if (HLODLayer)
			{
				if (ALevelInstance* LevelInstance = Cast<ALevelInstance>(Actor))
				{
					// Wait for level instance loading
					LevelInstance->GetLevelInstanceSubsystem()->BlockLoadLevelInstance(LevelInstance);
				}

				HLODLayersActors.FindOrAdd(HLODLayer).Add(Actor);
			}
		}
	}

	TArray<AWorldPartitionHLOD*> HLODActors;
	for (const auto& HLODLayerActors : HLODLayersActors)
	{
		if (!HLODLayerActors.Value.IsEmpty())
		{
			HLODActors += FHLODBuilderUtilities::BuildHLODs(InWorldPartition, InContext, InCellName, InCellBounds, HLODLayerActors.Key, InHLODLevel, HLODLayerActors.Value);
		}
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
		// Check if this actor is part of a DataLayer that has a default HLOD layer
		for(const UDataLayer* DataLayer : InActor->GetDataLayerObjects())
		{
			UHLODLayer* HLODLayer = DataLayer ? DataLayer->GetDefaultHLODLayer() : nullptr;
			if (HLODLayer)
			{
				return HLODLayer;
			}
		}

		// Fallback to the world partition default HLOD layer
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

bool UHLODLayer::ShouldIncludeInHLOD(const AActor* InActor, bool bInFromLevelInstance)
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

	if (!bInFromLevelInstance && InActor->HasAnyFlags(RF_Transient))
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
