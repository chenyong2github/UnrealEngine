// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HLODLayer.cpp: UHLODLayer class implementation
=============================================================================*/

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"

#if WITH_EDITOR
#include "Serialization/ArchiveCrc32.h"

#include "Engine/World.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescView.h"

#include "Modules/ModuleManager.h"
#include "IWorldPartitionHLODUtilities.h"
#include "WorldPartitionHLODUtilitiesModule.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogHLODLayer, Log, All);

UHLODLayer::UHLODLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bIsSpatiallyLoaded(true)
	, CellSize(3200)
	, LoadingRange(12800)
#endif
{
}

#if WITH_EDITOR

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
		// Fallback to the world partition default HLOD layer
		if (UWorldPartition* WorldPartition = InActor->GetWorld()->GetWorldPartition())
		{
			return WorldPartition->DefaultHLODLayer;
		}
	}

	return nullptr;
}

UHLODLayer* UHLODLayer::GetHLODLayer(const FWorldPartitionActorDescView& InActorDesc, const UWorldPartition* InWorldPartition)
{
	check(InWorldPartition);

	const FName HLODLayerName = InActorDesc.GetHLODLayer();
	if (UHLODLayer* HLODLayer = HLODLayerName.IsNone() ? nullptr : Cast<UHLODLayer>(FSoftObjectPath(HLODLayerName).TryLoad()))
	{
		return HLODLayer;
	}

	// Only fallback to the default HLODLayer for the first level of HLOD
	bool bIsHLOD0 = !InActorDesc.GetActorClass()->IsChildOf<AWorldPartitionHLOD>();
	if (bIsHLOD0)
	{
		// Fallback to the world partition default HLOD layer
		return InWorldPartition->DefaultHLODLayer;
	}

	return nullptr;
}

UHLODLayer* UHLODLayer::GetHLODLayer(const FWorldPartitionActorDesc& InActorDesc, const UWorldPartition* InWorldPartition)
{
	return GetHLODLayer(FWorldPartitionActorDescView(&InActorDesc), InWorldPartition);
}

bool UHLODLayer::DoesRequireWarmup() const
{
	IWorldPartitionHLODUtilities* WPHLODUtilities = FModuleManager::Get().LoadModuleChecked<IWorldPartitionHLODUtilitiesModule>("WorldPartitionHLODUtilities").GetUtilities();
	if (WPHLODUtilities)
	{
		return WPHLODUtilities->GetHLODBuilderClass(this)->GetDefaultObject<UHLODBuilder>()->RequiresWarmup();
	}

	return false;
}

void UHLODLayer::PostLoad()
{
	Super::PostLoad();

	if (HLODBuilderSettings == nullptr)
	{
		IWorldPartitionHLODUtilities* WPHLODUtilities = FModuleManager::Get().LoadModuleChecked<IWorldPartitionHLODUtilitiesModule>("WorldPartitionHLODUtilities").GetUtilities();
		if (WPHLODUtilities)
		{
			HLODBuilderSettings = WPHLODUtilities->CreateHLODBuilderSettings(this);
		}
	}

	if (bAlwaysLoaded_DEPRECATED)
	{
		bIsSpatiallyLoaded = false;
	}
}

void UHLODLayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : FName();

	IWorldPartitionHLODUtilities* WPHLODUtilities = FModuleManager::Get().LoadModuleChecked<IWorldPartitionHLODUtilitiesModule>("WorldPartitionHLODUtilities").GetUtilities();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UHLODLayer, LayerType) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UHLODLayer, HLODBuilderClass))
	{
		HLODBuilderSettings = WPHLODUtilities->CreateHLODBuilderSettings(this);
	}
}

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA

FName UHLODLayer::GetRuntimeGridName(uint32 InLODLevel, int32 InCellSize, double InLoadingRange)
{
	return *FString::Format(TEXT("HLOD{0}_{1}m_{2}m"), { InLODLevel, int32(InCellSize * 0.01f), int32(InLoadingRange * 0.01f)});
}

FName UHLODLayer::GetRuntimeGrid(uint32 InHLODLevel) const
{
	return !IsSpatiallyLoaded() ? NAME_None : GetRuntimeGridName(InHLODLevel, CellSize, LoadingRange);
}

const TSoftObjectPtr<UHLODLayer>& UHLODLayer::GetParentLayer() const
{
	static const TSoftObjectPtr<UHLODLayer> NullLayer;
	return !IsSpatiallyLoaded() ? NullLayer : ParentLayer;
}

#endif // WITH_EDITOR
