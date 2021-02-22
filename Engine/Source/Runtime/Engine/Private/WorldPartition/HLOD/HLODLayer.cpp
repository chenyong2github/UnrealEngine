// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HLODLayer.cpp: UHLODLayer class implementation
=============================================================================*/

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"

#if WITH_EDITOR
#include "Algo/Copy.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/Material.h"
#include "Engine/HLODProxy.h"
#include "Serialization/ArchiveCrc32.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogHLODLayer, Log, All);

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

uint32 UHLODLayer::GetCRC() const
{
	UHLODLayer& This = *const_cast<UHLODLayer*>(this);

	FArchiveCrc32 Ar;

	Ar << This.LayerType;
	UE_LOG(LogHLODLayer, VeryVerbose, TEXT(" - LayerType = %d"), Ar.GetCrc());

	if (LayerType == EHLODLayerType::MeshMerge)
	{
		Ar << This.MeshMergeSettings;
		UE_LOG(LogHLODLayer, VeryVerbose, TEXT(" - MeshMergeSettings = %d"), Ar.GetCrc());
	}
	else if (LayerType == EHLODLayerType::MeshSimplify)
	{
		Ar << This.MeshSimplifySettings;
		UE_LOG(LogHLODLayer, VeryVerbose, TEXT(" - MeshSimplifySettings = %d"), Ar.GetCrc());
	}

	Ar << This.CellSize;
	UE_LOG(LogHLODLayer, VeryVerbose, TEXT(" - CellSize = %d"), Ar.GetCrc());

	uint32 Hash = Ar.GetCrc();

	const bool bUseHLODMaterial = LayerType == EHLODLayerType::MeshMerge || LayerType == EHLODLayerType::MeshSimplify;
	if (bUseHLODMaterial && !HLODMaterial.IsNull())
	{
		UMaterialInterface* Material = HLODMaterial.LoadSynchronous();
		if (Material)
		{
			uint32 MaterialCRC = UHLODProxy::GetCRC(Material);
			UE_LOG(LogHLODLayer, VeryVerbose, TEXT(" - Material = %d"), MaterialCRC);
			Hash = HashCombine(Hash, MaterialCRC);
		}
	}

	return Hash;
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

UHLODLayer* UHLODLayer::GetHLODLayer(const FWorldPartitionActorDesc& InActorDesc, const UWorldPartition* InWorldPartition)
{
	check(InWorldPartition);

	if (UHLODLayer* HLODLayer = InActorDesc.GetHLODLayer())
	{
		return HLODLayer;
	}

	// Only fallback to the default HLODLayer for the first level of HLOD
	bool bIsHLOD0 = !InActorDesc.GetActorClass()->IsChildOf<AWorldPartitionHLOD>();
	if (bIsHLOD0)
	{
		if (UDataLayerSubsystem* DataLayerSubsystem = InWorldPartition->GetWorld()->GetSubsystem<UDataLayerSubsystem>())
		{
			if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(InWorldPartition->GetWorld()))
			{
				for (const FName& DataLayerName : InActorDesc.GetDataLayers())
				{
					const UDataLayer* DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName);
					UHLODLayer* HLODLayer = DataLayer ? DataLayer->GetDefaultHLODLayer() : nullptr;
					if (HLODLayer)
					{
						return HLODLayer;
					}
				}
			}
		}

		// Fallback to the world partition default HLOD layer
		return InWorldPartition->DefaultHLODLayer;
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
