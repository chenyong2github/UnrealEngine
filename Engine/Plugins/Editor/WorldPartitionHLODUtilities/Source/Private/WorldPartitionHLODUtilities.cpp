// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionHLODUtilities.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/HLOD/HLODBuilder.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"

#include "ISMPartition/ISMComponentDescriptor.h"

#include "Serialization/ArchiveCrc32.h"
#include "Templates/UniquePtr.h"

#include "Engine/StaticMesh.h"
#include "Engine/HLODProxy.h"
#include "Serialization/ArchiveCrc32.h"
#include "Materials/Material.h"
#include "AssetCompilingManager.h"

#include "HLODBuilderInstancing.h"
#include "HLODBuilderMeshMerge.h"
#include "HLODBuilderMeshSimplify.h"
#include "HLODBuilderMeshApproximate.h"

#include "Algo/Transform.h"

static UWorldPartitionLevelStreamingDynamic* CreateLevelStreamingFromHLODActor(AWorldPartitionHLOD* InHLODActor, bool& bOutDirty)
{
	bOutDirty = false;
	UWorld* World = InHLODActor->GetWorld();
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	check(WorldPartition);
	
	const FName LevelStreamingName = FName(*FString::Printf(TEXT("HLODLevelStreaming_%s"), *InHLODActor->GetName()));
	TArray<FWorldPartitionRuntimeCellObjectMapping> Mappings;
	Mappings.Reserve(InHLODActor->GetSubActors().Num());
	Algo::Transform(InHLODActor->GetSubActors(), Mappings, [](const FHLODSubActor& SubActor) { return FWorldPartitionRuntimeCellObjectMapping(SubActor.ActorPackage, SubActor.ActorPath, SubActor.ContainerID, SubActor.ContainerTransform, SubActor.ContainerPackage); });

	UWorldPartitionLevelStreamingDynamic* LevelStreaming = UWorldPartitionLevelStreamingDynamic::LoadInEditor(World, LevelStreamingName, Mappings);
	check(LevelStreaming);

	if (!LevelStreaming->GetLoadSucceeded())
	{
		bOutDirty = true;
		UE_LOG(LogHLODBuilder, Warning, TEXT("HLOD actor \"%s\" needs to be rebuilt as it didn't succeed in loading all actors."), *InHLODActor->GetActorLabel());
	}

	return LevelStreaming;
}

static uint32 GetCRC(const UHLODLayer* InHLODLayer)
{
	UHLODLayer& HLODLayer= *const_cast<UHLODLayer*>(InHLODLayer);

	uint32 CRC;

	CRC = GetTypeHash(HLODLayer.GetLayerType());
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - LayerType = %d"), CRC);

	CRC = HashCombine(HLODLayer.GetHLODBuilderSettings()->GetCRC(), CRC);
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - HLODBuilderSettings = %d"), CRC);

	CRC = HashCombine(HLODLayer.GetCellSize(), CRC);
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - CellSize = %d"), CRC);

	return CRC;
}

static uint32 ComputeHLODHash(AWorldPartitionHLOD* InHLODActor, const TArray<AActor*>& InActors)
{
	FArchiveCrc32 Ar;

	// Base key, changing this will force a rebuild of all HLODs
	FString HLODBaseKey = "5052091956924DB3BD9ACE00B71944AC";
	Ar << HLODBaseKey;

	// HLOD Layer
	uint32 HLODLayerHash = GetCRC(InHLODActor->GetSubActorsHLODLayer());
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - HLODLayer (%s) = %x"), *InHLODActor->GetSubActorsHLODLayer()->GetName(), HLODLayerHash);
	Ar << HLODLayerHash;

	// We get the CRC of each component
	TArray<uint32> ComponentsCRCs;
	for (UPrimitiveComponent* Component : UHLODBuilder::GatherPrimitiveComponents(InActors))
	{
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
		{
			uint32 ComponentCRC = 0;

			UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - Component \'%s\' from actor \'%s\'"), *Component->GetName(), *Component->GetOwner()->GetName());

			// CRC component
			uint32 StaticMeshComponentCRC = UHLODProxy::GetCRC(StaticMeshComponent);
			UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - StaticMeshComponent (%s) = %x"), *StaticMeshComponent->GetName(), StaticMeshComponentCRC);
			ComponentCRC = HashCombine(ComponentCRC, StaticMeshComponentCRC);

			if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				// CRC static mesh
				int32 StaticMeshCRC = UHLODProxy::GetCRC(StaticMesh);
				UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - StaticMesh (%s) = %x"), *StaticMesh->GetName(), StaticMeshCRC);
				ComponentCRC = HashCombine(ComponentCRC, StaticMeshCRC);

				// CRC materials
				const int32 NumMaterials = StaticMeshComponent->GetNumMaterials();
				for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
				{
					UMaterialInterface* MaterialInterface = StaticMeshComponent->GetMaterial(MaterialIndex);
					if (MaterialInterface)
					{
						uint32 MaterialInterfaceCRC = UHLODProxy::GetCRC(MaterialInterface);
						UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - MaterialInterface (%s) = %x"), *MaterialInterface->GetName(), MaterialInterfaceCRC);
						ComponentCRC = HashCombine(ComponentCRC, MaterialInterfaceCRC);

						TArray<UTexture*> Textures;
						MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
						for (UTexture* Texture : Textures)
						{
							uint32 TextureCRC = UHLODProxy::GetCRC(Texture);
							UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Texture (%s) = %x"), *Texture->GetName(), TextureCRC);
							ComponentCRC = HashCombine(ComponentCRC, TextureCRC);
						}
					}
				}
			}

			ComponentsCRCs.Add(ComponentCRC);
		}
	}

	// Sort the components CRCs to ensure the order of components won't have an impact on the final CRC
	ComponentsCRCs.Sort();

	// Append all components CRCs
	Ar << ComponentsCRCs;

	return Ar.GetCrc();
}

TArray<AWorldPartitionHLOD*> FWorldPartitionHLODUtilities::CreateHLODActors(FHLODCreationContext& InCreationContext, const FHLODCreationParams& InCreationParams, const TSet<FActorInstance>& InActors, const TArray<const UDataLayer*>& InDataLayers)
{
	struct FSubActorsInfo
	{
		TArray<FHLODSubActor>	SubActors;
		bool					bIsSpatiallyLoaded;
	};
	TMap<UHLODLayer*, FSubActorsInfo> SubActorsInfos;

	for (const FActorInstance& ActorInstance : InActors)
	{
		const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();
		if (ActorDescView.GetActorIsHLODRelevant())
		{
			UHLODLayer* HLODLayer = UHLODLayer::GetHLODLayer(ActorDescView, InCreationParams.WorldPartition);
			if (HLODLayer)
			{
				FSubActorsInfo& SubActorsInfo = SubActorsInfos.FindOrAdd(HLODLayer);

				SubActorsInfo.SubActors.Emplace(ActorDescView.GetGuid(), ActorDescView.GetActorPackage(), ActorDescView.GetActorPath(), ActorInstance.ContainerInstance->ID, ActorInstance.ContainerInstance->Container->GetContainerPackage(), ActorInstance.ContainerInstance->Transform);
				if (ActorDescView.GetIsSpatiallyLoaded())
				{
					SubActorsInfo.bIsSpatiallyLoaded = true;
				}
			}
		}
	}

	TArray<AWorldPartitionHLOD*> HLODActors;
	for (const auto& Pair : SubActorsInfos)
	{
		const UHLODLayer* HLODLayer = Pair.Key;
		const FSubActorsInfo& SubActorsInfo = Pair.Value;
		check(!SubActorsInfo.SubActors.IsEmpty());

		// Compute HLODActor hash
		uint64 CellHash = FHLODActorDesc::ComputeCellHash(HLODLayer->GetName(), InCreationParams.GridIndexX, InCreationParams.GridIndexY, InCreationParams.GridIndexZ, InCreationParams.DataLayersID);

		AWorldPartitionHLOD* HLODActor = nullptr;
		FWorldPartitionHandle HLODActorHandle;
		if (InCreationContext.HLODActorDescs.RemoveAndCopyValue(CellHash, HLODActorHandle))
		{
			InCreationContext.ActorReferences.Add(HLODActorHandle);
			HLODActor = CastChecked<AWorldPartitionHLOD>(HLODActorHandle->GetActor());
		}

		bool bNewActor = HLODActor == nullptr;
		if (bNewActor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = *FString::Printf(TEXT("%s_%016llx"), *HLODLayer->GetName(), CellHash);
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
			HLODActor = InCreationParams.WorldPartition->GetWorld()->SpawnActor<AWorldPartitionHLOD>(SpawnParams);

			HLODActor->SetActorLabel(FString::Printf(TEXT("HLOD%d_%s"), InCreationParams.HLODLevel, *InCreationParams.CellName.ToString()));
			HLODActor->SetFolderPath(*FString::Printf(TEXT("HLOD/HLOD%d"), InCreationParams.HLODLevel));
			HLODActor->SetSourceCellName(InCreationParams.CellName);
			HLODActor->SetSubActorsHLODLayer(HLODLayer);
			HLODActor->SetGridIndices(InCreationParams.GridIndexX, InCreationParams.GridIndexY, InCreationParams.GridIndexZ);

			// Make sure the generated HLOD actor has the same data layers as the source actors
			for (const UDataLayer* DataLayer : InDataLayers)
			{
				HLODActor->AddDataLayer(DataLayer);
			}
		}
		else
		{
#if DO_CHECK
			uint64 GridIndexX, GridIndexY, GridIndexZ;
			HLODActor->GetGridIndices(GridIndexX, GridIndexY, GridIndexZ);
			check(GridIndexX == InCreationParams.GridIndexX);
			check(GridIndexY == InCreationParams.GridIndexY);
			check(GridIndexZ == InCreationParams.GridIndexZ);
			check(HLODActor->GetSubActorsHLODLayer() == HLODLayer);
			check(FDataLayersID(HLODActor->GetDataLayerObjects()) == InCreationParams.DataLayersID);
#endif
		}

		bool bIsDirty = false;

		// Sub actors
		{
			bool bSubActorsChanged = HLODActor->GetSubActors().Num() != SubActorsInfo.SubActors.Num();
			if (!bSubActorsChanged)
			{
				TArray<FHLODSubActor> A = HLODActor->GetSubActors();
				TArray<FHLODSubActor> B = SubActorsInfo.SubActors;
				A.Sort();
				B.Sort();
				bSubActorsChanged = A != B;
			}

			if (bSubActorsChanged)
			{
				HLODActor->SetSubActors(SubActorsInfo.SubActors);
				bIsDirty = true;
			}
		}

		// Runtime grid
		FName RuntimeGrid = HLODLayer->GetRuntimeGrid(InCreationParams.HLODLevel);
		if (HLODActor->GetRuntimeGrid() != RuntimeGrid)
		{
			HLODActor->SetRuntimeGrid(RuntimeGrid);
			bIsDirty = true;
		}

		// Spatially loaded
		// HLOD that are always loaded will not take the SubActorsInfo.GridPlacement into account
		bool bExpectedIsSpatiallyLoaded = !HLODLayer->IsSpatiallyLoaded() ? false : SubActorsInfo.bIsSpatiallyLoaded;
		if (HLODActor->GetIsSpatiallyLoaded() != bExpectedIsSpatiallyLoaded)
		{
			HLODActor->SetIsSpatiallyLoaded(bExpectedIsSpatiallyLoaded);
			bIsDirty = true;
		}

		// HLOD level
		if (HLODActor->GetLODLevel() != InCreationParams.HLODLevel)
		{
			HLODActor->SetLODLevel(InCreationParams.HLODLevel);
			bIsDirty = true;
		}

		// Require warmup
		if (HLODActor->DoesRequireWarmup() != HLODLayer->DoesRequireWarmup())
		{
			HLODActor->SetRequireWarmup(HLODLayer->DoesRequireWarmup());
			bIsDirty = true;
		}

		// Parent HLOD layer
		UHLODLayer* ParentHLODLayer = HLODLayer->GetParentLayer().LoadSynchronous();
		if (HLODActor->GetHLODLayer() != ParentHLODLayer)
		{
			HLODActor->SetHLODLayer(ParentHLODLayer);
			bIsDirty = true;
		}

		// Cell bounds
		if (!HLODActor->GetHLODBounds().Equals(InCreationParams.CellBounds))
		{
			HLODActor->SetHLODBounds(InCreationParams.CellBounds);
			bIsDirty = true;
		}

		// If any change was performed, mark HLOD package as dirty
		if (bIsDirty)
		{
			HLODActor->MarkPackageDirty();
		}

		HLODActors.Add(HLODActor);
	}

	return HLODActors;
}

TSubclassOf<UHLODBuilder> FWorldPartitionHLODUtilities::GetHLODBuilderClass(const UHLODLayer* InHLODLayer)
{
	EHLODLayerType HLODLayerType = InHLODLayer->GetLayerType();
	switch (HLODLayerType)
	{
	case EHLODLayerType::Instancing:
		return UHLODBuilderInstancing::StaticClass();
		break;

	case EHLODLayerType::MeshMerge:
		return UHLODBuilderMeshMerge::StaticClass();
		break;

	case EHLODLayerType::MeshSimplify:
		return UHLODBuilderMeshSimplify::StaticClass();
		break;

	case EHLODLayerType::MeshApproximate:
		return UHLODBuilderMeshApproximate::StaticClass();
		break;

	case EHLODLayerType::Custom:
		return InHLODLayer->GetHLODBuilderClass();
		break;

	default:
		checkf(false, TEXT("Unsupported type"));
		return nullptr;
	}
}

UHLODBuilderSettings* FWorldPartitionHLODUtilities::CreateHLODBuilderSettings(UHLODLayer* InHLODLayer)
{
	TSubclassOf<UHLODBuilder> HLODBuilderClass = GetHLODBuilderClass(InHLODLayer);
	if (!HLODBuilderClass)
	{
		return NewObject<UHLODBuilderSettings>(InHLODLayer, UHLODBuilderSettings::StaticClass());
	}

	UHLODBuilderSettings* HLODBuilderSettings = HLODBuilderClass->GetDefaultObject<UHLODBuilder>()->CreateSettings(InHLODLayer);
	if (!ensure(HLODBuilderSettings))
	{
		return NewObject<UHLODBuilderSettings>(InHLODLayer, UHLODBuilderSettings::StaticClass());
	}

	return HLODBuilderSettings;
}

uint32 FWorldPartitionHLODUtilities::BuildHLOD(AWorldPartitionHLOD* InHLODActor)
{
	bool bIsDirty = false;
	UWorldPartitionLevelStreamingDynamic* LevelStreaming = CreateLevelStreamingFromHLODActor(InHLODActor, bIsDirty);
	ON_SCOPE_EXIT
	{
		UWorldPartitionLevelStreamingDynamic::UnloadFromEditor(LevelStreaming);
	};

	uint32 OldHLODHash = bIsDirty ? 0 : InHLODActor->GetHLODHash();
	uint32 NewHLODHash = ComputeHLODHash(InHLODActor, LevelStreaming->GetLoadedLevel()->Actors);

	if (OldHLODHash == NewHLODHash)
	{
		UE_LOG(LogHLODBuilder, Verbose, TEXT("HLOD actor \"%s\" doesn't need to be rebuilt."), *InHLODActor->GetActorLabel());
		return OldHLODHash;
	}

	const UHLODLayer* HLODLayer = InHLODActor->GetSubActorsHLODLayer();
	TSubclassOf<UHLODBuilder> HLODBuilderClass = GetHLODBuilderClass(HLODLayer);

	if (HLODBuilderClass)
	{
		UHLODBuilder* HLODBuilder = NewObject<UHLODBuilder>(GetTransientPackage(), HLODBuilderClass);
		if (ensure(HLODBuilder))
		{
			HLODBuilder->AddToRoot();
			if (HLODBuilder->RequiresCompiledAssets())
			{
				// Wait for compilation to finish
				FAssetCompilingManager::Get().FinishAllCompilation();
			}

			HLODBuilder->Build(InHLODActor, HLODLayer, LevelStreaming->GetLoadedLevel()->Actors);
			HLODBuilder->RemoveFromRoot();
		}
			
		InHLODActor->MarkPackageDirty();
	}

	return NewHLODHash;
}

#endif
