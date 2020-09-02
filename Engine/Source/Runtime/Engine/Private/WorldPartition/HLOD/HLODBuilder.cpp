// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartitionEditorCellPreviewActor.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "Templates/UniquePtr.h"

#include "IMeshMergeUtilities.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "IMeshDescriptionModule.h"
#include "Materials/Material.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshComponentAdapter.h"
#include "MeshDescription.h"
#include "Async/ParallelFor.h"

/**
 * Base class for all HLODBuilders
 */
class FHLODBuilder
{
public:
	virtual ~FHLODBuilder() {}

	virtual void Build(const TArray<AActor*>& InSubActors) const = 0;

	typedef TFunction<UPrimitiveComponent*(AWorldPartitionHLOD*)> FCreateComponentFunction;
		
	AWorldPartitionHLOD* SpawnHLODActor(const TCHAR* InName, const TArray<UPrimitiveComponent*>& InSubComponents, FCreateComponentFunction InCreateComponentFunc) const
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.bDeferConstruction = true;
		SpawnParams.bCreateActorPackage = true;
		AWorldPartitionHLOD* HLODActor = World->SpawnActor<AWorldPartitionHLOD>(SpawnParams);

		UPrimitiveComponent* ParentComponent = InCreateComponentFunc(HLODActor);

		if (ParentComponent)
		{
			HLODActor->SetHLODLayer(HLODLayer, iLevel);
			HLODActor->SetParentPrimitive(ParentComponent);
			HLODActor->SetChildrenPrimitives(InSubComponents);
			HLODActor->SetActorLabel(FString::Printf(TEXT("%s_%s_%s"), *HLODLayer->GetName(), *CellName.ToString(), InName));
			HLODActor->RuntimeGrid = GetHLODLevelSettings().TargetGrid;

			WorldPartition->UpdateActorDesc(HLODActor);
		}
		else
		{
			World->DestroyActor(HLODActor);
			HLODActor = nullptr;
		}

		return HLODActor;
	}

	TArray<UPrimitiveComponent*> GatherPrimitiveComponents(int32 iHLODLevel, const TArray<AActor*> InActors) const
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
};


/**
 * Build a AWorldPartitionHLOD whose components are ISMC
 */
class FHLODBuilder_Instancing : public FHLODBuilder
{
	virtual void Build(const TArray<AActor*>& InSubActors) const override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_Instancing::BuildHLOD);

		const FHLODLevelSettings& LevelSettings = GetHLODLevelSettings();

		// Gather all meshes to instantiate along with their transforms
		TArray<UPrimitiveComponent*> PrimitiveComponents = GatherPrimitiveComponents(iLevel, InSubActors);
		TMap<UStaticMesh*, TArray<UPrimitiveComponent*>> Instances;
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
			{
				Instances.FindOrAdd(SMC->GetStaticMesh()).Add(SMC);
			}
		}

		// Now, create an ISMC for each SM asset we found
		for (const auto& Entry : Instances)
		{
			UStaticMesh* StaticMesh = Entry.Key;
			const TArray<UPrimitiveComponent*>& SubComponents = Entry.Value;

			FCreateComponentFunction CreateComponentLambda = [&LevelSettings, StaticMesh, SubComponents](AWorldPartitionHLOD* HLODActor)
			{
				UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(HLODActor);
				Component->SetStaticMesh(StaticMesh);
				Component->SetForcedLodModel(StaticMesh->GetNumLODs());

				// Add all instances
				for (UPrimitiveComponent* SMC : SubComponents)
				{
					Component->AddInstanceWorldSpace(SMC->GetComponentTransform());
				}

				return Component;
			};

			SpawnHLODActor(*StaticMesh->GetName(), SubComponents, CreateComponentLambda);
		}
	}
};


/**
 * Build a merged mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshMerge : public FHLODBuilder
{
	virtual void Build(const TArray<AActor*>& InSubActors) const override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_MeshMerge);

		const FHLODLevelSettings& LevelSettings = GetHLODLevelSettings();

		TArray<UPrimitiveComponent*> SubComponents = GatherPrimitiveComponents(iLevel, InSubActors);

		FCreateComponentFunction CreateComponentLambda = [&LevelSettings, SubComponents, this](AWorldPartitionHLOD* HLODActor)
		{
			TArray<UObject*> Assets;
			FVector MergedActorLocation;

			const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
			MeshMergeUtilities.MergeComponentsToStaticMesh(SubComponents, HLODActor->GetWorld(), LevelSettings.MergeSetting, LevelSettings.FlattenMaterial.LoadSynchronous(), HLODActor->GetPackage(), CellName.ToString(), Assets, MergedActorLocation, 0.25f, false);

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

			return Component;
		};
		
		SpawnHLODActor(TEXT("MergedMesh"), SubComponents, CreateComponentLambda);
	}
};

/**
 * Build a simplified mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshSimplify : public FHLODBuilder
{
	virtual void Build(const TArray<AActor*>& InSubActors) const override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UHLODLayer::BuildHLOD_MeshProxy);

		const FHLODLevelSettings& LevelSettings = GetHLODLevelSettings();

		TArray<UPrimitiveComponent*> SubComponents = GatherPrimitiveComponents(iLevel, InSubActors);

		FCreateComponentFunction CreateComponentLambda = [&LevelSettings, SubComponents, this] (AWorldPartitionHLOD* HLODActor)
		{
			TArray<UObject*> Assets;
			FCreateProxyDelegate ProxyDelegate;
			ProxyDelegate.BindLambda([&Assets](const FGuid Guid, TArray<UObject*>& InAssetsCreated) { Assets = InAssetsCreated; });

			TArray<UStaticMeshComponent*> StaticMeshComponents;
			Algo::TransformIf(SubComponents, StaticMeshComponents, [](UPrimitiveComponent* InPrimitiveComponent) { return InPrimitiveComponent->IsA<UStaticMeshComponent>(); }, [](UPrimitiveComponent* InPrimitiveComponent) { return Cast<UStaticMeshComponent>(InPrimitiveComponent); });

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

			return Component;
		};
		
		SpawnHLODActor(TEXT("SimplifiedMesh"), SubComponents, CreateComponentLambda);
	}
};


/**
 *
 */
void FHLODBuilderUtilities::BuildHLODs(UWorldPartition* InWorldPartition, FName InCellName, const UHLODLayer* InHLODLayer, const TArray<AActor*>& InSubActors)
{
	const TArray<FHLODLevelSettings>& LevelsSettings = InHLODLayer->GetLevels();

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

		if (HLODBuilder)
		{
			HLODBuilder->World = InWorldPartition->GetWorld();
			HLODBuilder->WorldPartition = InWorldPartition;
			HLODBuilder->HLODLayer = InHLODLayer;
			HLODBuilder->iLevel = iLevel;
			HLODBuilder->CellName = InCellName;

			HLODBuilder->Build(InSubActors);
		}
	}
}

AWorldPartitionEditorCellPreview* FHLODBuilderUtilities::BuildCellPreviewMesh(const TArray<AActor*>& InCellActors, const FBox& InCellBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilderUtilities::BuildCellPreviewMesh);

	TMap<UHLODLayer*, TArray<UStaticMeshComponent*>> CellLODPrimitives;

	// Gather all components that will be used to create the preview mesh
	for (AActor* Actor : InCellActors)
	{
		UHLODLayer* ActorHLODLayer = UHLODLayer::GetHLODLayer(Actor);

		if (ActorHLODLayer && UHLODLayer::ShouldIncludeInHLOD(Actor))
		{
			TArray<UStaticMeshComponent*> StaticMeshComponent;
			Actor->GetComponents(StaticMeshComponent);

			for (UStaticMeshComponent* Component : StaticMeshComponent)
			{
				if (UHLODLayer::ShouldIncludeInHLOD(Component, ActorHLODLayer->GetLevels().Num() - 1))
				{
					CellLODPrimitives.FindOrAdd(ActorHLODLayer).Add(Component);
				}
			}
		}
	}

	if (CellLODPrimitives.IsEmpty())
	{
		return nullptr;
	}

	UWorld* World = InCellActors[0]->GetWorld();

	const IMeshDescriptionModule& MeshDescriptionModule = IMeshDescriptionModule::Get();
	const IMeshMergeUtilities& MergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

	// Spawn a cell preview actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.bDeferConstruction = true;
	SpawnParams.bCreateActorPackage = true;
	AWorldPartitionEditorCellPreview* CellPreviewActor = World->SpawnActor<AWorldPartitionEditorCellPreview>(AWorldPartitionEditorCellPreview::StaticClass(), FTransform(InCellBounds.GetCenter()), SpawnParams);
	CellPreviewActor->SetCellBounds(InCellBounds);
	
	TArray<UStaticMeshComponent*> ComponentsToAdd;

	for (const auto& Primitives : CellLODPrimitives)
	{
		const UHLODLayer* HLODLayer = Primitives.Key;
		const TArray<UStaticMeshComponent*>& StaticMeshComponents = Primitives.Value;

		for (const FHLODLevelSettings& LevelSettings : HLODLayer->GetLevels())
		{
			switch (LevelSettings.LevelType)
			{
			case EHLODLevelType::Instancing:
			{
				// Gather all meshes to instantiate along with transforms
				TMap<UStaticMesh*, TArray<UStaticMeshComponent*>> Instances;
				for (UStaticMeshComponent* SMC : StaticMeshComponents)
				{
					Instances.FindOrAdd(SMC->GetStaticMesh()).Add(SMC);
				}

				for (const auto& Entry : Instances)
				{
					// Get or create preview mesh
					UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(CellPreviewActor);
					Component->SetStaticMesh(Entry.Key);
					Component->SetForcedLodModel(Entry.Key->GetNumLODs());

					for (UStaticMeshComponent* SMC : Entry.Value)
					{
						Component->AddInstanceWorldSpace(SMC->GetComponentTransform());
					}

					ComponentsToAdd.Add(Component);
				}
			}
			break;

			default:
			{
				FMeshDescription MergedMeshDescription;
				FStaticMeshAttributes(MergedMeshDescription).Register();

				TArray<FMeshDescription> SourceMeshesDescriptions;
				SourceMeshesDescriptions.SetNum(StaticMeshComponents.Num());

				{
					TRACE_CPUPROFILER_EVENT_SCOPE(Gather Meshes);
					ParallelFor(SourceMeshesDescriptions.Num(), [&MergeUtilities, &StaticMeshComponents, &SourceMeshesDescriptions](uint32 Index)
					{
						FMeshDescription& SourceMeshDescription = SourceMeshesDescriptions[Index];
						FStaticMeshAttributes(SourceMeshDescription).Register();

						UStaticMeshComponent* SMComponent = StaticMeshComponents[Index];
					
						MergeUtilities.RetrieveMeshDescription(SMComponent, SMComponent->GetStaticMesh()->GetNumLODs() - 1, SourceMeshDescription, false);
					});
				}

				{
					TRACE_CPUPROFILER_EVENT_SCOPE(Merge Meshes);

					FStaticMeshOperations::FAppendSettings AppendSettings;
					AppendSettings.bMergeVertexColor = false;
					for (int32 ChannelIdx = 0; ChannelIdx < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIdx)
					{
						AppendSettings.bMergeUVChannels[ChannelIdx] = false;
					}
				
					MergedMeshDescription.CreatePolygonGroupWithID(FPolygonGroupID(0));

					AppendSettings.PolygonGroupsDelegate = FAppendPolygonGroupsDelegate::CreateLambda([](const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroups)
					{
						for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
						{
							RemapPolygonGroups.Add(SourcePolygonGroupID, FPolygonGroupID(0));
						}
					});

					for (const FMeshDescription& SourceMeshDescription : SourceMeshesDescriptions)
					{
						FStaticMeshOperations::AppendMeshDescription(SourceMeshDescription, MergedMeshDescription, AppendSettings);
					}
				}

				FStaticMeshComponentRecreateRenderStateContext RecreateRenderStateContext(FindObject<UStaticMesh>(CellPreviewActor->GetPackage(), TEXT("EditorCellPreviewMesh")));

				UStaticMesh* StaticMesh = NewObject<UStaticMesh>(CellPreviewActor->GetPackage(), TEXT("EditorCellPreviewMesh"));
				StaticMesh->BuildFromMeshDescriptions( { &MergedMeshDescription } );
				
				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(CellPreviewActor);
				Component->SetStaticMesh(StaticMesh);

				ComponentsToAdd.Add(Component);
			}
			
			}
		}
	}

	// If for some reason no components were created, delete the actor.
	if (ComponentsToAdd.IsEmpty())
	{
		World->DestroyActor(CellPreviewActor);
		return nullptr;
	}

	// Setup the newly created components
	for (UPrimitiveComponent* ComponentToAdd : ComponentsToAdd)
	{
		ComponentToAdd->AttachToComponent(CellPreviewActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

		// Setup custom depth rendering to achieve a red tint using a post process material
		// Temporary until a cleaner solution is implemented
		const int32 CellPreviewStencilValue = 180;
		ComponentToAdd->bRenderCustomDepth = true;
		ComponentToAdd->CustomDepthStencilValue = CellPreviewStencilValue;

		ComponentToAdd->SetMobility(EComponentMobility::Static);
		ComponentToAdd->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		ComponentToAdd->RegisterComponent();
		ComponentToAdd->MarkRenderStateDirty();

		CellPreviewActor->AddInstanceComponent(ComponentToAdd);
	}

	CellPreviewActor->SetVisibility(false);
	return CellPreviewActor;
}

#endif
