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

#include "UObject/GCObjectScopeGuard.h"
#include "Serialization/ArchiveCrc32.h"
#include "Templates/UniquePtr.h"

#include "HLODBuilderInstancing.h"
#include "HLODBuilderMeshMerge.h"
#include "HLODBuilderMeshSimplify.h"
#include "HLODBuilderMeshApproximate.h"

#include "Algo/Transform.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

static UWorldPartitionLevelStreamingDynamic* CreateLevelStreamingFromHLODActor(AWorldPartitionHLOD* InHLODActor, bool& bOutDirty)
{
	UPackage::WaitForAsyncFileWrites();

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
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - HLOD Layer (%s) = %x"), *InHLODActor->GetSubActorsHLODLayer()->GetName(), HLODLayerHash);
	Ar << HLODLayerHash;

	// Min Visible Distance
	uint32 HLODMinVisibleDistanceHash = GetTypeHash(InHLODActor->GetMinVisibleDistance());
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - HLOD Min Visible Distance (%.02f) = %x"), InHLODActor->GetMinVisibleDistance(), HLODMinVisibleDistanceHash);
	Ar << HLODMinVisibleDistanceHash;

	// Append all components CRCs
	uint32 HLODComponentsHash = UHLODBuilder::ComputeHLODHash(InActors);
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - HLOD Source Components = %x"), HLODComponentsHash);
	Ar << HLODComponentsHash;

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

		// Minimum visible distance
		if (!FMath::IsNearlyEqual(HLODActor->GetMinVisibleDistance(), InCreationParams.MinVisibleDistance))
		{
			HLODActor->SetMinVisibleDistance(InCreationParams.MinVisibleDistance);
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
	// Retrieve the HLOD builder class
	TSubclassOf<UHLODBuilder> HLODBuilderClass = GetHLODBuilderClass(InHLODLayer);
	if (!HLODBuilderClass)
	{
		return NewObject<UHLODBuilderSettings>(InHLODLayer, UHLODBuilderSettings::StaticClass());
	}

	// Retrieve the HLOD builder settings class
	TSubclassOf<UHLODBuilderSettings> HLODBuilderSettingsClass = HLODBuilderClass->GetDefaultObject<UHLODBuilder>()->GetSettingsClass();
	if (!ensure(HLODBuilderSettingsClass))
	{
		return NewObject<UHLODBuilderSettings>(InHLODLayer, UHLODBuilderSettings::StaticClass());
	}

	UHLODBuilderSettings* HLODBuilderSettings = NewObject<UHLODBuilderSettings>(InHLODLayer, HLODBuilderSettingsClass);

	// Deprecated properties handling
	if (InHLODLayer->GetHLODBuilderSettings() == nullptr)
	{
		EHLODLayerType HLODLayerType = InHLODLayer->GetLayerType();
		switch (HLODLayerType)
		{
		case EHLODLayerType::MeshMerge:
			CastChecked<UHLODBuilderMeshMergeSettings>(HLODBuilderSettings)->MeshMergeSettings = InHLODLayer->MeshMergeSettings_DEPRECATED;
			CastChecked<UHLODBuilderMeshMergeSettings>(HLODBuilderSettings)->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED;
			break;

		case EHLODLayerType::MeshSimplify:
			CastChecked<UHLODBuilderMeshSimplifySettings>(HLODBuilderSettings)->MeshSimplifySettings = InHLODLayer->MeshSimplifySettings_DEPRECATED;
			CastChecked<UHLODBuilderMeshSimplifySettings>(HLODBuilderSettings)->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED;
			break;

		case EHLODLayerType::MeshApproximate:
			CastChecked<UHLODBuilderMeshApproximateSettings>(HLODBuilderSettings)->MeshApproximationSettings = InHLODLayer->MeshApproximationSettings_DEPRECATED;
			CastChecked<UHLODBuilderMeshApproximateSettings>(HLODBuilderSettings)->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED;
			break;
		};
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
			FGCObjectScopeGuard BuilderGCScopeGuard(HLODBuilder);

			HLODBuilder->SetHLODBuilderSettings(HLODLayer->GetHLODBuilderSettings());

			FHLODBuildContext HLODBuildContext;
			HLODBuildContext.World = InHLODActor->GetWorld();
			HLODBuildContext.AssetsOuter = InHLODActor->GetPackage();
			HLODBuildContext.AssetsBaseName = InHLODActor->GetActorLabel();
			HLODBuildContext.MinVisibleDistance = InHLODActor->GetMinVisibleDistance();

			TArray<UActorComponent*> HLODComponents = HLODBuilder->Build(HLODBuildContext, LevelStreaming->GetLoadedLevel()->Actors);
			if (HLODComponents.IsEmpty())
			{
				UE_LOG(LogHLODBuilder, Warning, TEXT("HLOD generation created no component for %s"), *InHLODActor->GetActorLabel());
			}

			InHLODActor->Modify();
			InHLODActor->SetHLODComponents(HLODComponents);

			// Ideally, this should be performed elsewhere, to allow more flexibility in the HLOD generation
			for (UActorComponent* HLODComponent : HLODComponents)
			{
				if (UPrimitiveComponent* HLODPrimitive = Cast<UPrimitiveComponent>(HLODComponent))
				{
					// Disable collisions
					HLODPrimitive->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
					HLODPrimitive->SetGenerateOverlapEvents(false);
					HLODPrimitive->SetCanEverAffectNavigation(false);
					HLODPrimitive->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
					HLODPrimitive->SetCanEverAffectNavigation(false);
					HLODPrimitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					HLODPrimitive->SetMobility(EComponentMobility::Static);

					// Enable optimizations
					HLODPrimitive->bComputeFastLocalBounds = true;
					HLODPrimitive->bComputeBoundsOnceForGame = true;
				}

				if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(HLODComponent))
				{
					if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
					{
						// If the HLOD process did create this static mesh
						if (StaticMeshComponent->GetPackage() == StaticMesh->GetPackage())
						{
							// Set up ray tracing far fields for always loaded HLODs
							StaticMeshComponent->bRayTracingFarField = !HLODLayer->IsSpatiallyLoaded() && StaticMesh->bSupportRayTracing;

							// Disable collisions
							if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
							{
								BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
								BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
							}

							// Rename owned static mesh
							StaticMesh->Rename(*MakeUniqueObjectName(StaticMesh->GetOuter(), StaticMesh->GetClass(), *FString::Printf(TEXT("StaticMesh_%s"), *HLODLayer->GetName())).ToString());
						}
					}
				}
			}
		}
	}

	return NewHLODHash;
}

#endif
