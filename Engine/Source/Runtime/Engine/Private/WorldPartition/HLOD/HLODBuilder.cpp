// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

#include "Algo/ForEach.h"
#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "Misc/HashBuilder.h"
#include "Templates/UniquePtr.h"

#include "IMeshMergeUtilities.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "LevelInstance/LevelInstanceActor.h"

/**
 * Base class for all HLODBuilders
 */
class FHLODBuilder
{
public:
	virtual ~FHLODBuilder() {}

	virtual const TCHAR* GetActorSuffix() const = 0;
	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const TArray<UPrimitiveComponent*>& InSubComponents) = 0;

	void Build(const TArray<const AActor*>& InSubActors)
	{
		TArray<UPrimitiveComponent*> SubComponents = GatherPrimitiveComponents(0, InSubActors);
		if (SubComponents.IsEmpty())
		{
			return;
		}

		AWorldPartitionHLOD* HLODActor = nullptr;

		// Compute HLODActor hash
		uint64 CellHash = FHLODActorDesc::ComputeCellHash(HLODLayer->GetName(), Context->GridIndexX, Context->GridIndexY, Context->GridIndexZ, Context->DataLayersID);

		int32 HLODActorRefIndex = INDEX_NONE;
		FWorldPartitionHandle HLODActorHandle;
		if (Context->HLODActorDescs.RemoveAndCopyValue(CellHash, HLODActorHandle))
		{
			HLODActorRefIndex = Context->ActorReferences.Add(HLODActorHandle);
			HLODActor = CastChecked<AWorldPartitionHLOD>(HLODActorHandle->GetActor());
		}

		if (!HLODActor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = *FString::Printf(TEXT("%s_%016llx_%s"), *HLODLayer->GetName(), CellHash, GetActorSuffix());
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
			HLODActor = World->SpawnActor<AWorldPartitionHLOD>(SpawnParams);
			HLODActor->SetActorLabel(CellName.ToString());
		}

		TArray<UPrimitiveComponent*> HLODPrimitives = CreateComponents(HLODActor, SubComponents);
		HLODPrimitives.RemoveSwap(nullptr);

		if (!HLODPrimitives.IsEmpty())
		{
			HLODActor->Modify();
			HLODActor->SetHLODPrimitives(HLODPrimitives);
			HLODActor->SetSubActors(InSubActors);
			HLODActor->SetRuntimeGrid(HLODLayer->GetRuntimeGrid(HLODLevel));
			HLODActor->SetLODLevel(HLODLevel);
			HLODActor->SetHLODLayer(HLODLayer->GetParentLayer().LoadSynchronous());
			HLODActor->SetSubActorsHLODLayer(HLODLayer);
			HLODActor->SetGridIndices(Context->GridIndexX, Context->GridIndexY, Context->GridIndexZ);
		}
		else
		{
			if (HLODActorRefIndex != INDEX_NONE)
			{
				Context->HLODActorDescs.Add(CellHash, FWorldPartitionHandle(WorldPartition, HLODActor->GetActorGuid()));
				Context->ActorReferences.RemoveAtSwap(HLODActorRefIndex);
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

	TArray<UPrimitiveComponent*> GatherPrimitiveComponents(uint32 InHLODLevel, const TArray<const AActor*>& InActors)
	{
		TArray<UPrimitiveComponent*> PrimitiveComponents;

		auto GatherPrimitivesFromActor = [&PrimitiveComponents, InHLODLevel](const AActor* Actor)
		{
			if (UHLODLayer::ShouldIncludeInHLOD(Actor, Actor->IsA<ALevelInstance>()))
			{
				for (UActorComponent* SubComponent : Actor->GetComponents())
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
		};

		TSet<AActor*> UnderlyingActors;

		for (const AActor* Actor : InActors)
		{
			// Gather primitives from the Actor
			GatherPrimitivesFromActor(Actor);

			// Retrieve all underlying actors (ex: all sub actors of a LevelInstance)
			UnderlyingActors.Reset();
			Actor->EditorGetUnderlyingActors(UnderlyingActors);

			// Gather primitives from underlying actors
			for (const AActor* UnderlyingActor : UnderlyingActors)
			{
				GatherPrimitivesFromActor(UnderlyingActor);
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
	// We want to merge all SMC that are using the same static mesh
	// However, we must also take material overiddes into account.
	struct FInstancingKey
	{
		FInstancingKey(const UStaticMeshComponent* SMC)
		{
			FHashBuilder HashBuilder;

			StaticMesh = SMC->GetStaticMesh();
			HashBuilder << StaticMesh;

			const int32 NumMaterials = SMC->GetNumMaterials();
			Materials.Reserve(NumMaterials);

			for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
			{
				UMaterialInterface* Material = SMC->GetMaterial(MaterialIndex);

				Materials.Add(Material);
				HashBuilder << Material;
			}

			Hash = HashBuilder.GetHash();
		}

		void ApplyTo(UStaticMeshComponent* SMC) const
		{
			// Set static mesh
			SMC->SetStaticMesh(StaticMesh);
			
			// Set material overrides
			for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
			{
				SMC->SetMaterial(MaterialIndex, Materials[MaterialIndex]);
			}
		}

		friend uint32 GetTypeHash(const FInstancingKey& Key)
		{
			return Key.Hash;
		}

		bool operator==(const FInstancingKey& Other) const
		{
			return Hash == Other.Hash && StaticMesh == Other.StaticMesh && Materials == Other.Materials;
		}

	private:
		UStaticMesh*				StaticMesh;
		TArray<UMaterialInterface*>	Materials;
		uint32						Hash;
	};

	virtual const TCHAR* GetActorSuffix() const override { return TEXT("InstancedMeshes"); }

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const TArray<UPrimitiveComponent*>& InSubComponents) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_Instancing::CreateComponents);

		TArray<UPrimitiveComponent*> Components;

		// Gather all meshes to instantiate along with their transforms
		TMap<FInstancingKey, TArray<UPrimitiveComponent*>> Instances;
		for (UPrimitiveComponent* Primitive : InSubComponents)
		{
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
			{
				Instances.FindOrAdd(FInstancingKey(SMC)).Add(SMC);
			}
		}

		// Create an ISMC for each SM asset we found
		for (const auto& Entry : Instances)
		{
			const FInstancingKey EntryInstancingKey = Entry.Key;
			const TArray<UPrimitiveComponent*>& EntryComponents = Entry.Value;
			
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(InHLODActor);
			EntryInstancingKey.ApplyTo(Component);
			Component->SetForcedLodModel(Component->GetStaticMesh()->GetNumLODs());

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
	}
};


/**
 * Build a merged mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshMerge : public FHLODBuilder
{
	virtual const TCHAR* GetActorSuffix() const override { return TEXT("MergedMesh"); }

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const TArray<UPrimitiveComponent*>& InSubComponents) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_MeshMerge::CreateComponents);

		TArray<UObject*> Assets;
		FVector MergedActorLocation;

		const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
		MeshMergeUtilities.MergeComponentsToStaticMesh(InSubComponents, InHLODActor->GetWorld(), HLODLayer->GetMeshMergeSettings(), HLODLayer->GetHLODMaterial().LoadSynchronous(), InHLODActor->GetPackage(), CellName.ToString(), Assets, MergedActorLocation, 0.25f, false);

		UStaticMeshComponent* Component = nullptr;
		Algo::ForEach(Assets, [InHLODActor, &Component, &MergedActorLocation](UObject* Asset)
		{
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(nullptr, InHLODActor);

			if (Cast<UStaticMesh>(Asset))
			{
				Component = NewObject<UStaticMeshComponent>(InHLODActor);
				Component->SetStaticMesh(static_cast<UStaticMesh*>(Asset));
				Component->SetWorldLocation(MergedActorLocation);
			}
		});

		return TArray<UPrimitiveComponent*>({ Component });
	}
};

/**
 * Build a simplified mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshSimplify : public FHLODBuilder
{
	virtual const TCHAR* GetActorSuffix() const override { return TEXT("SimplifiedMesh"); }

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const TArray<UPrimitiveComponent*>& InSubComponents) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_MeshSimplify::CreateComponents);

		TArray<UObject*> Assets;
		FCreateProxyDelegate ProxyDelegate;
		ProxyDelegate.BindLambda([&Assets](const FGuid Guid, TArray<UObject*>& InAssetsCreated) { Assets = InAssetsCreated; });

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		Algo::TransformIf(InSubComponents, StaticMeshComponents, [](UPrimitiveComponent* InPrimitiveComponent) { return InPrimitiveComponent->IsA<UStaticMeshComponent>(); }, [](UPrimitiveComponent* InPrimitiveComponent) { return Cast<UStaticMeshComponent>(InPrimitiveComponent); });

		const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
		MeshMergeUtilities.CreateProxyMesh(StaticMeshComponents, HLODLayer->GetMeshSimplifySettings(), HLODLayer->GetHLODMaterial().LoadSynchronous(), InHLODActor->GetPackage(), CellName.ToString(), FGuid::NewGuid(), ProxyDelegate, true);

		UStaticMeshComponent* Component = nullptr;
		Algo::ForEach(Assets, [InHLODActor, &Component](UObject* Asset)
		{
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(nullptr, InHLODActor);

			if (Cast<UStaticMesh>(Asset))
			{
				Component = NewObject<UStaticMeshComponent>(InHLODActor);
				Component->SetStaticMesh(static_cast<UStaticMesh*>(Asset));
			}
		});

		return TArray<UPrimitiveComponent*>({ Component });
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

	TArray<AWorldPartitionHLOD*> HLODActors;

	if (HLODBuilder)
	{
		HLODBuilder->World = InWorldPartition->GetWorld();
		HLODBuilder->WorldPartition = InWorldPartition;
		HLODBuilder->HLODLayer = InHLODLayer;
		HLODBuilder->HLODLevel = InHLODLevel;
		HLODBuilder->CellName = InCellName;
		HLODBuilder->CellBounds = InCellBounds;
		HLODBuilder->Context = InContext;

		HLODBuilder->Build(InSubActors);
			
		HLODActors = HLODBuilder->HLODActors;
	}

	return HLODActors;
}

#endif
