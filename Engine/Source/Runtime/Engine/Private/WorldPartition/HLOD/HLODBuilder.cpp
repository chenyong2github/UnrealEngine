// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "Algo/ForEach.h"
#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "Templates/UniquePtr.h"

#include "IMeshMergeUtilities.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

/**
 * Base class for all HLODBuilders
 */
class FHLODBuilder
{
public:
	virtual ~FHLODBuilder() {}

	virtual void Build(const TArray<UPrimitiveComponent*>& InSubComponents) = 0;

	typedef TFunction<TArray<UPrimitiveComponent*>(AWorldPartitionHLOD*)> FCreateComponentsFunction;
		
	void SpawnHLODActor(const TCHAR* InName, const TArray<UPrimitiveComponent*>& InSubComponents, FCreateComponentsFunction InCreateComponentsFunc)
	{
		FString HLODActorName = FString::Printf(TEXT("%s_%s_%s"), *HLODLayer->GetName(), *CellName.ToString(), InName);
		FString HLODActorPath = FString::Printf(TEXT("%s.%s"), *World->PersistentLevel->GetPathName(), *HLODActorName);

		AWorldPartitionHLOD* HLODActor = FindObject<AWorldPartitionHLOD>(ANY_PACKAGE, *HLODActorPath);
		if (!HLODActor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = *HLODActorName;
			HLODActor = World->SpawnActor<AWorldPartitionHLOD>(SpawnParams);
		}

		TArray<UPrimitiveComponent*> HLODPrimitives = InCreateComponentsFunc(HLODActor);
		Algo::RemoveIf(HLODPrimitives, [](const UPrimitiveComponent* HLODPrimitive) { return !HLODPrimitive; });

		if (!HLODPrimitives.IsEmpty())
		{
			HLODActor->Modify();
			HLODActor->SetHLODLayer(HLODLayer, iLevel);
			HLODActor->SetHLODPrimitives(HLODPrimitives, CellLoadingRange);
			HLODActor->SetHLODBounds(CellBounds);
			HLODActor->SetChildrenPrimitives(InSubComponents);
			HLODActor->SetActorLabel(HLODActorName);
			HLODActor->RuntimeGrid = GetHLODLevelSettings().TargetGrid;

			WorldPartition->UpdateActorDesc(HLODActor);
		}
		else
		{
			World->DestroyActor(HLODActor);
			HLODActor = nullptr;
		}

		if (HLODActor)
		{
			HLODActors.Add(HLODActor);
		}
	}

	static TArray<UPrimitiveComponent*> GatherPrimitiveComponents(int32 iHLODLevel, const TArray<AActor*> InActors)
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

	const FHLODLevelSettings& GetHLODLevelSettings() const
	{
		return HLODLayer->GetLevels()[iLevel];
	}

public:
	UWorld*				World;
	UWorldPartition*	WorldPartition;
	const UHLODLayer*	HLODLayer;
	int32				iLevel;
	FName				CellName;
	FBox				CellBounds;
	float				CellLoadingRange;

	TArray<AWorldPartitionHLOD*> HLODActors;
};


/**
 * Build a AWorldPartitionHLOD whose components are ISMC
 */
class FHLODBuilder_Instancing : public FHLODBuilder
{
	virtual void Build(const TArray<UPrimitiveComponent*>& InSubComponents) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_Instancing::BuildHLOD);

		const FHLODLevelSettings& LevelSettings = GetHLODLevelSettings();

		FCreateComponentsFunction CreateComponentLambda = [&LevelSettings, &InSubComponents](AWorldPartitionHLOD* HLODActor)
		{
			TArray<UPrimitiveComponent*> Components;

			// Gather all meshes to instantiate along with their transforms
			TMap<UStaticMesh*, TArray<UPrimitiveComponent*>> Instances;
			for (UPrimitiveComponent* Primitive : InSubComponents)
			{
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
				{
					Instances.FindOrAdd(SMC->GetStaticMesh()).Add(SMC);
				}
			}

			// Create an ISMC for each SM asset we found
			for (const auto& Entry : Instances)
			{
				UStaticMesh* EntryStaticMesh = Entry.Key;
				const TArray<UPrimitiveComponent*>& EntryComponents = Entry.Value;
			
				UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(HLODActor);
				Component->SetStaticMesh(EntryStaticMesh);
				Component->SetForcedLodModel(EntryStaticMesh->GetNumLODs());

				// Add all instances
				for (UPrimitiveComponent* SMC : EntryComponents)
				{
					Component->AddInstanceWorldSpace(SMC->GetComponentTransform());
				}

				Components.Add(Component);
			};

			return Components;
		};

		SpawnHLODActor(TEXT("InstancedMeshes"), InSubComponents, CreateComponentLambda);
	}
};


/**
 * Build a merged mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshMerge : public FHLODBuilder
{
	virtual void Build(const TArray<UPrimitiveComponent*>& InSubComponents) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_MeshMerge);

		const FHLODLevelSettings& LevelSettings = GetHLODLevelSettings();

		FCreateComponentsFunction CreateComponentLambda = [&LevelSettings, &InSubComponents, this](AWorldPartitionHLOD* HLODActor)
		{
			TArray<UObject*> Assets;
			FVector MergedActorLocation;

			const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
			MeshMergeUtilities.MergeComponentsToStaticMesh(InSubComponents, HLODActor->GetWorld(), LevelSettings.MergeSetting, LevelSettings.FlattenMaterial.LoadSynchronous(), HLODActor->GetPackage(), CellName.ToString(), Assets, MergedActorLocation, 0.25f, false);

			// All merged mesh assets are stored in the HLOD Actor package
			Algo::ForEach(Assets, [](UObject* Asset) { Asset->ClearFlags(RF_Public | RF_Standalone); });

			UStaticMesh* StaticMesh = nullptr;
			UStaticMeshComponent* Component = nullptr;
			if (Assets.FindItemByClass<UStaticMesh>(&StaticMesh))
			{
				Component = NewObject<UStaticMeshComponent>(HLODActor);
				Component->SetStaticMesh(StaticMesh);
				Component->SetWorldLocation(MergedActorLocation);
			}

			return TArray<UPrimitiveComponent*>({ Component });
		};
		
		SpawnHLODActor(TEXT("MergedMesh"), InSubComponents, CreateComponentLambda);
	}
};

/**
 * Build a simplified mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshSimplify : public FHLODBuilder
{
	virtual void Build(const TArray<UPrimitiveComponent*>& InSubComponents) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_MeshProxy);

		const FHLODLevelSettings& LevelSettings = GetHLODLevelSettings();

		FCreateComponentsFunction CreateComponentLambda = [&LevelSettings, &InSubComponents, this] (AWorldPartitionHLOD* HLODActor)
		{
			TArray<UObject*> Assets;
			FCreateProxyDelegate ProxyDelegate;
			ProxyDelegate.BindLambda([&Assets](const FGuid Guid, TArray<UObject*>& InAssetsCreated) { Assets = InAssetsCreated; });

			TArray<UStaticMeshComponent*> StaticMeshComponents;
			Algo::TransformIf(InSubComponents, StaticMeshComponents, [](UPrimitiveComponent* InPrimitiveComponent) { return InPrimitiveComponent->IsA<UStaticMeshComponent>(); }, [](UPrimitiveComponent* InPrimitiveComponent) { return Cast<UStaticMeshComponent>(InPrimitiveComponent); });

			const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
			MeshMergeUtilities.CreateProxyMesh(StaticMeshComponents, LevelSettings.ProxySetting, LevelSettings.FlattenMaterial.LoadSynchronous(), HLODActor->GetPackage(), CellName.ToString(), FGuid::NewGuid(), ProxyDelegate, true);

			// All merged mesh assets are stored in the HLOD Actor package
			Algo::ForEach(Assets, [](UObject* Asset) { Asset->ClearFlags(RF_Public | RF_Standalone); });

			UStaticMesh* StaticMesh = nullptr;
			UStaticMeshComponent* Component = nullptr;
			if (Assets.FindItemByClass<UStaticMesh>(&StaticMesh))
			{
				Component = NewObject<UStaticMeshComponent>(HLODActor);
				Component->SetStaticMesh(StaticMesh);
			}

			return TArray<UPrimitiveComponent*>({ Component });
		};
		
		SpawnHLODActor(TEXT("SimplifiedMesh"), InSubComponents, CreateComponentLambda);
	}
};

TArray<AWorldPartitionHLOD*> FHLODBuilderUtilities::BuildHLODs(UWorldPartition* InWorldPartition, FName InCellName, FBox InCellBounds, float InCellLoadingRange, const UHLODLayer* InHLODLayer, const TArray<AActor*>& InSubActors)
{
	const TArray<FHLODLevelSettings>& LevelsSettings = InHLODLayer->GetLevels();

	TArray<AWorldPartitionHLOD*> HLODActors;

	for (int32 iLevel = 0; iLevel < LevelsSettings.Num(); ++iLevel)
	{
		TUniquePtr<FHLODBuilder> HLODBuilder = nullptr;

		EHLODLevelType HLODLevelType = LevelsSettings[iLevel].LevelType;
		switch (HLODLevelType)
		{
		case EHLODLevelType::Instancing:
			HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_Instancing());
			break;

		case EHLODLevelType::MeshMerge:
			HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_MeshMerge());
			break;

		case EHLODLevelType::MeshSimplify:
			HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_MeshSimplify());
			break;

		default:
			checkf(false, TEXT("Unsupported type"));
		}

		TArray<UPrimitiveComponent*> SubComponents = FHLODBuilder::GatherPrimitiveComponents(iLevel, InSubActors);

		if (HLODBuilder && !SubComponents.IsEmpty())
		{
			HLODBuilder->World = InWorldPartition->GetWorld();
			HLODBuilder->WorldPartition = InWorldPartition;
			HLODBuilder->HLODLayer = InHLODLayer;
			HLODBuilder->iLevel = iLevel;
			HLODBuilder->CellName = InCellName;
			HLODBuilder->CellBounds = InCellBounds;
			HLODBuilder->CellLoadingRange = InCellLoadingRange;

			HLODBuilder->Build(SubComponents);
			
			HLODActors += HLODBuilder->HLODActors;
		}
	}

	return HLODActors;
}

#endif
