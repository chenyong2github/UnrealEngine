// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HLODLayer.cpp: UHLODLayer class implementation
=============================================================================*/

#include "WorldPartition/HLOD/HLODLayer.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODActor.h"

#include "Algo/ForEach.h"

#include "IMeshMergeUtilities.h"
#include "IMeshDescriptionModule.h"
#include "MeshMergeModule.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#endif

UHLODLayer::UHLODLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

void UHLODLayer::GenerateHLODForCell(UWorldPartition* InWorldPartition, FName InCellName, const TSet<FGuid>& InCellActors)
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
			HLODLayersActors.FindOrAdd(HLODLayer).Add(Actor);
		}
	}

	for (const auto& HLODLayerActors : HLODLayersActors)
	{
		UHLODLayer* HLODLayer = HLODLayerActors.Key;
		if (!ensure(HLODLayer))
		{
			// No default HLOD layer, can't generate HLODs for those actors.
			continue;
		}

		HLODLayer->BuildHLOD(InWorldPartition, InCellName, HLODLayerActors.Value);
	}
}

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



int32 UHLODLayer::BuildHLOD(UWorldPartition* InWorldPartition, FName InCellName, const TArray<AActor*>& InSubActors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD);

	int32 NumActorSpawned = 0;

	for (int32 iLevel = 0; iLevel < Levels.Num(); ++iLevel)
	{
		switch (Levels[iLevel].LevelType)
		{
		case EHLODLevelType::Instancing:
			NumActorSpawned += BuildHLOD_Instancing(iLevel, InWorldPartition, InCellName, InSubActors);
			break;

		case EHLODLevelType::MeshMerge:
			NumActorSpawned += BuildHLOD_MeshMerge(iLevel, InWorldPartition, InCellName, InSubActors);
			break;

		case EHLODLevelType::MeshProxy:
			NumActorSpawned += BuildHLOD_MeshProxy(iLevel, InWorldPartition, InCellName, InSubActors);
			break;

		default:
			checkf(false, TEXT("Unsupported type"));
		}
	}

	return NumActorSpawned;
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

TArray<UPrimitiveComponent*> GatherPrimitiveComponents(int32 iHLODLevel, const TArray<AActor*> InActors)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	for (AActor* SubActor : InActors)
	{
		for (UActorComponent* SubComponent : SubActor->GetComponents())
		{
			if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(SubComponent))
			{
				if (UHLODLayer::ShouldIncludeInHLOD(PrimitiveComponent, iHLODLevel))
				{
					PrimitiveComponents.Add(PrimitiveComponent);
				}
			}
		}
	}
	return PrimitiveComponents;
}

int32 UHLODLayer::BuildHLOD_Instancing(int32 iLevel, UWorldPartition* InWorldPartition, FName InCellName, const TArray<AActor*>& InSubActors) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_Instancing);

	const FHLODLevelSettings& LevelSettings = Levels[iLevel];

	TArray<UPrimitiveComponent*> PrimitiveComponents = GatherPrimitiveComponents(iLevel, InSubActors);

	// Gather all meshes to instantiate along with their transforms
	TMap<UStaticMesh*, TArray<UPrimitiveComponent*>> Instances;
	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
		{
			Instances.FindOrAdd(SMC->GetStaticMesh()).Add(SMC);
		}
	}

	int32 NumActors = 0;

	// Now, create an ISMC for each SM asset we found
	for (const auto& Entry : Instances)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.bDeferConstruction = true;
		SpawnParams.bCreateActorPackage = true;
		AWorldPartitionHLOD* HLODActor = InWorldPartition->GetWorld()->SpawnActor<AWorldPartitionHLOD>(SpawnParams);

		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(HLODActor);
		Component->SetStaticMesh(Entry.Key);
		Component->SetForcedLodModel(Entry.Key->GetNumLODs());

		// Add all instances
		TSet<AActor*> Actors;

		for (UPrimitiveComponent* SMC : Entry.Value)
		{
			Actors.Add(SMC->GetOwner());
			Component->AddInstanceWorldSpace(SMC->GetComponentTransform());
		}

		HLODActor->SetHLODLayer(this, iLevel);
		HLODActor->SetParentPrimitive(Component);
		HLODActor->SetChildrenPrimitives(Entry.Value);
		HLODActor->SetActorLabel(FString::Printf(TEXT("%s_%s_%s"), *GetName(), *InCellName.ToString(), *Entry.Key->GetName()));
		HLODActor->RuntimeGrid = Levels[iLevel].TargetGrid;

		InWorldPartition->UpdateActorDesc(HLODActor);

		NumActors++;
	}

	return NumActors;
}

int32 UHLODLayer::BuildHLOD_MeshMerge(int32 iLevel, UWorldPartition* InWorldPartition, FName InCellName, const TArray<AActor*>& InSubActors) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_MeshMerge);

	FActorSpawnParameters SpawnParams;
	SpawnParams.bDeferConstruction = true;
	SpawnParams.bCreateActorPackage = true;
	AWorldPartitionHLOD* HLODActor = InWorldPartition->GetWorld()->SpawnActor<AWorldPartitionHLOD>(SpawnParams);

	const FHLODLevelSettings& LevelSettings = Levels[iLevel];

	const IMeshDescriptionModule& MeshDescriptionModule = IMeshDescriptionModule::Get();
	const IMeshMergeUtilities& MergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

	TArray<UPrimitiveComponent*> PrimitiveComponents = GatherPrimitiveComponents(iLevel, InSubActors);

	TArray<UObject*> Assets;
	FVector MergedActorLocation;
	MergeUtilities.MergeComponentsToStaticMesh(PrimitiveComponents, HLODActor->GetWorld(), LevelSettings.MergeSetting, LevelSettings.FlattenMaterial.LoadSynchronous(), HLODActor->GetPackage(), TEXT(""), Assets, MergedActorLocation, 0.25f, false);

	// All merged mesh assets are stored in the HLOD Actor package
	Algo::ForEach(Assets, [](UObject* Asset) { Asset->ClearFlags(RF_Public | RF_Standalone); });

	UStaticMesh* StaticMesh;

	int32 NumComponents = 0;
	if (Assets.FindItemByClass<UStaticMesh>(&StaticMesh))
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(HLODActor);
		Component->SetStaticMesh(StaticMesh);
		Component->SetWorldLocation(MergedActorLocation);
		
		HLODActor->SetHLODLayer(this, iLevel);
		HLODActor->SetParentPrimitive(Component);
		HLODActor->SetChildrenPrimitives(PrimitiveComponents);
		HLODActor->SetActorLabel(FString::Printf(TEXT("%s_%s_MergedMesh"), *GetName(), *InCellName.ToString()));
		HLODActor->RuntimeGrid = Levels[iLevel].TargetGrid;

		InWorldPartition->UpdateActorDesc(HLODActor);
		NumComponents++;
	}
	else
	{
		InWorldPartition->GetWorld()->DestroyActor(HLODActor);
	}

	return NumComponents;
}

int32 UHLODLayer::BuildHLOD_MeshProxy(int32 iLevel, UWorldPartition* InWorldPartition, FName InCellName, const TArray<AActor*>& InSubActors) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_MeshProxy);

	const FHLODLevelSettings& LevelSettings = Levels[iLevel];

	return 0;
}
#endif // WITH_EDITOR