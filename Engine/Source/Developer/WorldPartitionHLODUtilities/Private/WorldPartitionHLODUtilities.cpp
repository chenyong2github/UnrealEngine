// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionHLODUtilities.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

#include "ISMPartition/ISMComponentDescriptor.h"

#include "Serialization/ArchiveCrc32.h"
#include "Templates/UniquePtr.h"

#include "Engine/StaticMesh.h"
#include "Engine/HLODProxy.h"
#include "Materials/Material.h"
#include "StaticMeshCompiler.h"

#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

#include "HLODBuilder.h"
#include "HLODBuilderInstancing.h"
#include "HLODBuilderMeshMerge.h"
#include "HLODBuilderMeshSimplify.h"
#include "HLODBuilderMeshApproximate.h"

static bool LoadSubActors(AWorldPartitionHLOD* InHLODActor, TArray<FWorldPartitionReference>& OutActors)
{
	OutActors.Reserve(InHLODActor->GetSubActors().Num());

	UWorld* World = InHLODActor->GetWorld();
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	check(WorldPartition);

	bool bIsDirty = false;

	// Gather (and potentially load) actors
	for (const FGuid& SubActorGuid : InHLODActor->GetSubActors())
	{
		FWorldPartitionReference ActorRef(WorldPartition, SubActorGuid);

		if (ActorRef.IsValid())
		{
			AActor* LoadedActor = ActorRef.Get()->GetActor();

			// Load level instances
			if (ALevelInstance* LevelInstance = Cast<ALevelInstance>(LoadedActor))
			{
				// Wait for level instance loading
				if (LevelInstance->SupportsLoading())
				{
					LevelInstance->GetLevelInstanceSubsystem()->BlockLoadLevelInstance(LevelInstance);
				}
			}

			OutActors.Add(MoveTemp(ActorRef));
		}
		else
		{
			bIsDirty = true;
		}
	}

	// Wait for compilation to finish
	FStaticMeshCompilingManager::Get().FinishAllCompilation();

	return !bIsDirty;
}

static uint32 ComputeHLODHash(AWorldPartitionHLOD* InHLODActor, const TArray<FWorldPartitionReference>& InActors)
{
	FArchiveCrc32 Ar;

	// Base key, changing this will force a rebuild of all HLODs
	FString HLODBaseKey = "5184EABE2BEF440DB5A461554A28A3E4";
	Ar << HLODBaseKey;

	// HLOD Layer
	uint32 HLODLayerHash = InHLODActor->GetSubActorsHLODLayer()->GetCRC();
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - HLODLayer (%s) = %x"), *InHLODActor->GetSubActorsHLODLayer()->GetName(), HLODLayerHash);
	Ar << HLODLayerHash;

	// We get the CRC of each component
	TArray<uint32> ComponentsCRCs;
	for (UPrimitiveComponent* Component : FHLODBuilder::GatherPrimitiveComponents(InActors))
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
	TMap<UHLODLayer*, TArray<FGuid>> HLODLayersActors;
	for (const FActorInstance& ActorInstance : InActors)
	{
		FWorldPartitionActorDesc& ActorDesc = InCreationParams.WorldPartition->GetActorDescChecked(ActorInstance.Actor);
		if (ActorDesc.GetActorIsHLODRelevant())
		{
			UHLODLayer* HLODLayer = UHLODLayer::GetHLODLayer(ActorDesc, InCreationParams.WorldPartition);
			if (HLODLayer)
			{
				HLODLayersActors.FindOrAdd(HLODLayer).Add(ActorInstance.Actor);
			}
		}
	}

	TArray<AWorldPartitionHLOD*> HLODActors;
	for (const auto& Pair : HLODLayersActors)
	{
		const UHLODLayer* HLODLayer = Pair.Key;
		const TArray<FGuid> HLODLayerActors = Pair.Value;
		check(!HLODLayerActors.IsEmpty());

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

			FSoftObjectPath WorldPartitionPath(InCreationParams.WorldPartition);
			FString CellObjectPath = FString::Printf(TEXT("%s.%s"), *WorldPartitionPath.ToString(), *InCreationParams.CellName.ToString());
			TSoftObjectPtr<UWorldPartitionRuntimeCell> RuntimeCell(CellObjectPath);

			HLODActor->SetActorLabel(FString::Printf(TEXT("HLOD%d_%s"), InCreationParams.HLODLevel, *InCreationParams.CellName.ToString()));
			HLODActor->SetFolderPath(*FString::Printf(TEXT("HLOD/HLOD%d"), InCreationParams.HLODLevel));
			HLODActor->SetSourceCell(RuntimeCell);
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
			bool bSubActorsChanged = HLODActor->GetSubActors().Num() != HLODLayerActors.Num();
			if (!bSubActorsChanged)
			{
				TArray<FGuid> A = HLODActor->GetSubActors();
				TArray<FGuid> B = HLODLayerActors;
				A.Sort();
				B.Sort();
				bSubActorsChanged = A != B;
			}

			if (bSubActorsChanged)
			{
				HLODActor->SetSubActors(HLODLayerActors);
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

		// HLOD level
		if (HLODActor->GetLODLevel() != InCreationParams.HLODLevel)
		{
			HLODActor->SetLODLevel(InCreationParams.HLODLevel);
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

uint32 FWorldPartitionHLODUtilities::BuildHLOD(AWorldPartitionHLOD* InHLODActor)
{
	TArray<FWorldPartitionReference> SubActors;
	
	bool bIsDirty = !LoadSubActors(InHLODActor, SubActors);
	if (bIsDirty)
	{
		UE_LOG(LogHLODBuilder, Warning, TEXT("HLOD actor \"%s\" needs to be rebuilt as it references actors that have been deleted."), *InHLODActor->GetActorLabel());
	}

	uint32 OldHLODHash = bIsDirty ? 0 : InHLODActor->GetHLODHash();
	uint32 NewHLODHash = ComputeHLODHash(InHLODActor, SubActors);

	if (OldHLODHash == NewHLODHash)
	{
		return OldHLODHash;
	}

	TUniquePtr<FHLODBuilder> HLODBuilder = nullptr;
	
	const UHLODLayer* HLODLayer = InHLODActor->GetSubActorsHLODLayer();
	EHLODLayerType HLODLevelType = HLODLayer->GetLayerType();
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

	case EHLODLayerType::MeshApproximate:
		HLODBuilder = TUniquePtr<FHLODBuilder>(new FHLODBuilder_MeshApproximate());
		break;

	default:
		checkf(false, TEXT("Unsupported type"));
	}

	if (ensure(HLODBuilder))
	{
		HLODBuilder->Build(InHLODActor, HLODLayer, SubActors);
	}
			
	InHLODActor->MarkPackageDirty();

	return NewHLODHash;
}

#endif
