// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

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
		AWorldPartitionHLOD* HLODActor = nullptr;

		// Compute HLODActor hash
		uint64 CellHash = FHLODActorDesc::ComputeCellHash(HLODLayer->GetName(), Context->GridIndexX, Context->GridIndexY, Context->GridIndexZ);

		FGuid HLODActorGuid;
		if (Context->HLODActorDescs.RemoveAndCopyValue(CellHash, HLODActorGuid))
		{
			FWorldPartitionActorDesc* HLODActorDesc = WorldPartition->GetActorDesc(HLODActorGuid);
			check(HLODActorDesc);

			check(!HLODActorDesc->GetLoadedRefCount());
			HLODActorDesc->AddLoadedRefCount();
			HLODActor = CastChecked<AWorldPartitionHLOD>(HLODActorDesc->Load());
			HLODActor->GetLevel()->AddLoadedActor(HLODActor);
		}

		if (!HLODActor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = *FString::Printf(TEXT("%s_%016llx_%s"), *HLODLayer->GetName(), CellHash, InName);
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
			HLODActor = World->SpawnActor<AWorldPartitionHLOD>(SpawnParams);
			HLODActor->SetActorLabel(CellName.ToString());
		}

		TArray<UPrimitiveComponent*> HLODPrimitives = InCreateComponentsFunc(HLODActor);
		HLODPrimitives.RemoveSwap(nullptr);

		if (!HLODPrimitives.IsEmpty())
		{
			HLODActor->Modify();
			HLODActor->SetHLODPrimitives(HLODPrimitives);
			HLODActor->SetChildrenPrimitives(InSubComponents);
			HLODActor->RuntimeGrid = HLODLayer->GetRuntimeGrid(HLODLevel);
			HLODActor->SetLODLevel(HLODLevel);
			HLODActor->SetHLODLayer(HLODLayer->GetParentLayer().Get());
			HLODActor->SetSubActorsHLODLayer(HLODLayer);
			HLODActor->SetGridIndices(Context->GridIndexX, Context->GridIndexY, Context->GridIndexZ);
		}
		else
		{
			if (HLODActorGuid.IsValid())
			{
				FWorldPartitionActorDesc* HLODActorDesc = WorldPartition->GetActorDesc(HLODActorGuid);
				check(HLODActorDesc);

				check(HLODActorDesc->GetLoadedRefCount() == 1);
				HLODActorDesc->RemoveLoadedRefCount();

				HLODActor->GetLevel()->RemoveLoadedActor(HLODActor);
				HLODActorDesc->Unload();

				Context->HLODActorDescs.Add(CellHash, HLODActorGuid);
			}
			else
			{
				World->DestroyActor(HLODActor);
				HLODActor = nullptr;
			}
		}

		if (HLODActor)
		{
			HLODActors.Add(HLODActor);
		}
	}

	static TArray<UPrimitiveComponent*> GatherPrimitiveComponents(uint32 InHLODLevel, const TArray<const AActor*> InActors)
	{
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		for (const AActor* SubActor : InActors)
		{
			for (UActorComponent* SubComponent : SubActor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(SubComponent))
				{
					if (UHLODLayer::ShouldIncludeInHLOD(PrimitiveComponent, InHLODLevel))
					{
						PrimitiveComponents.Add(PrimitiveComponent);
					}
				}
			}
		}
		return PrimitiveComponents;
	}

public:
	UWorld*					World;
	UWorldPartition*		WorldPartition;
	const UHLODLayer*		HLODLayer;
	uint32					HLODLevel;
	FName					CellName;
	FBox					CellBounds;
	float					CellLoadingRange;
	FHLODGenerationContext* Context;

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

		FCreateComponentsFunction CreateComponentLambda = [&InSubComponents](AWorldPartitionHLOD* HLODActor)
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
					// If we have an ISMC, retrieve all instances
					if (UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(SMC))
					{
						for (int32 InstanceIdx = 0; InstanceIdx < InstancedStaticMeshComponent->GetInstanceCount(); InstanceIdx++)
						{
							FTransform InstanceTransform;
							InstancedStaticMeshComponent->GetInstanceTransform(InstanceIdx, InstanceTransform, true);
							Component->AddInstanceWorldSpace(InstanceTransform);
						}
					}
					else
					{
						Component->AddInstanceWorldSpace(SMC->GetComponentTransform());
					}
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

		FCreateComponentsFunction CreateComponentLambda = [&InSubComponents, this](AWorldPartitionHLOD* HLODActor)
		{
			TArray<UObject*> Assets;
			FVector MergedActorLocation;

			const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
			MeshMergeUtilities.MergeComponentsToStaticMesh(InSubComponents, HLODActor->GetWorld(), HLODLayer->GetMeshMergeSettings(), HLODLayer->GetHLODMaterial().LoadSynchronous(), HLODActor->GetPackage(), CellName.ToString(), Assets, MergedActorLocation, 0.25f, false);

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

		FCreateComponentsFunction CreateComponentLambda = [&InSubComponents, this] (AWorldPartitionHLOD* HLODActor)
		{
			TArray<UObject*> Assets;
			FCreateProxyDelegate ProxyDelegate;
			ProxyDelegate.BindLambda([&Assets](const FGuid Guid, TArray<UObject*>& InAssetsCreated) { Assets = InAssetsCreated; });

			TArray<UStaticMeshComponent*> StaticMeshComponents;
			Algo::TransformIf(InSubComponents, StaticMeshComponents, [](UPrimitiveComponent* InPrimitiveComponent) { return InPrimitiveComponent->IsA<UStaticMeshComponent>(); }, [](UPrimitiveComponent* InPrimitiveComponent) { return Cast<UStaticMeshComponent>(InPrimitiveComponent); });

			const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
			MeshMergeUtilities.CreateProxyMesh(StaticMeshComponents, HLODLayer->GetMeshSimplifySettings(), HLODLayer->GetHLODMaterial().LoadSynchronous(), HLODActor->GetPackage(), CellName.ToString(), FGuid::NewGuid(), ProxyDelegate, true);

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

TArray<AWorldPartitionHLOD*> FHLODBuilderUtilities::BuildHLODs(UWorldPartition* InWorldPartition, FHLODGenerationContext* InContext, FName InCellName, const FBox& InCellBounds, const UHLODLayer* InHLODLayer, uint32 InHLODLevel, const TArray<const AActor*>& InSubActors)
{
	TUniquePtr<FHLODBuilder> HLODBuilder = nullptr;

	EHLODLayerType HLODLevelType = InHLODLayer->GetLayerType();
	switch (HLODLevelType)
	{
	case EHLODLayerType::Instancing:
		HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_Instancing());
		break;

	case EHLODLayerType::MeshMerge:
		HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_MeshMerge());
		break;

	case EHLODLayerType::MeshSimplify:
		HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_MeshSimplify());
		break;

	default:
		checkf(false, TEXT("Unsupported type"));
	}

	TArray<UPrimitiveComponent*> SubComponents = FHLODBuilder::GatherPrimitiveComponents(0, InSubActors);
	TArray<AWorldPartitionHLOD*> HLODActors;

	if (HLODBuilder && !SubComponents.IsEmpty())
	{
		HLODBuilder->World = InWorldPartition->GetWorld();
		HLODBuilder->WorldPartition = InWorldPartition;
		HLODBuilder->HLODLayer = InHLODLayer;
		HLODBuilder->HLODLevel = InHLODLevel;
		HLODBuilder->CellName = InCellName;
		HLODBuilder->CellBounds = InCellBounds;
		HLODBuilder->Context = InContext;

		HLODBuilder->Build(SubComponents);
			
		HLODActors = HLODBuilder->HLODActors;
	}

	return HLODActors;
}

#endif
