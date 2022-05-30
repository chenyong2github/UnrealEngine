// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "Engine/World.h"

int32 UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch = 0;

UWorldPartitionRuntimeCell::UWorldPartitionRuntimeCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bIsAlwaysLoaded(false)
, MinMaxZ(-WORLDPARTITION_MAX, WORLDPARTITION_MAX)
, Priority(0)
, CachedMinSourcePriority((uint8)EStreamingSourcePriority::Lowest)
, CachedSourceInfoEpoch(INT_MIN)
#if !UE_BUILD_SHIPPING
, DebugStreamingPriority(-1.f)
#endif
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeCell::SetDataLayers(const TArray<const UDataLayerInstance*>& InDataLayerInstances)
{
	check(DataLayers.IsEmpty());
	DataLayers.Reserve(InDataLayerInstances.Num());
	for (const UDataLayerInstance* DataLayerInstance : InDataLayerInstances)
	{
		check(DataLayerInstance->IsRuntime());
		DataLayers.Add(DataLayerInstance->GetDataLayerFName());
	}
	DataLayers.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
	UpdateDebugName();
}

void UWorldPartitionRuntimeCell::SetMinMaxZ(const FVector2D& InMinMaxZ)
{
	MinMaxZ = InMinMaxZ;
}

void UWorldPartitionRuntimeCell::SetDebugInfo(FIntVector InCoords, FName InGridName)
{
	Coords = InCoords;
	GridName = InGridName;
	UpdateDebugName();
}

void UWorldPartitionRuntimeCell::UpdateDebugName()
{
	TStringBuilder<512> Builder;
	Builder += GridName.ToString();
	Builder += TEXT("_");
	Builder += FString::Printf(TEXT("L%d_X%d_Y%d"), Coords.Z, Coords.X, Coords.Y);
	int32 DataLayerCount = DataLayers.Num();

	const UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(GetOuterUWorldPartition()->GetWorld());
	TArray<const UDataLayerInstance*> DataLayerObjects;
	if (DataLayerSubsystem && DataLayerCount > 0)
	{
		Builder += TEXT(" DL[");
		for (int i = 0; i < DataLayerCount; ++i)
		{
			const UDataLayerInstance* DataLayer = DataLayerSubsystem->GetDataLayerInstance(DataLayers[i]);
			DataLayerObjects.Add(DataLayer);
			Builder += DataLayer->GetDataLayerShortName();
			Builder += TEXT(",");
		}
		Builder += FString::Printf(TEXT("ID:%X]"), FDataLayersID(DataLayerObjects).GetHash());
	}
	DebugName = Builder.ToString();
}

void UWorldPartitionRuntimeCell::DumpStateLog(FHierarchicalLogArchive& Ar)
{
	Ar.Printf(TEXT("Actor Count: %d"), GetActorCount());
	Ar.Printf(TEXT("MinMaxZ: %s"), *GetMinMaxZ().ToString());
}

#endif

bool UWorldPartitionRuntimeCell::CacheStreamingSourceInfo(const UWorldPartitionRuntimeCell::FStreamingSourceInfo& Info) const
{
	bool bWasCacheDirtied = false;
	if (CachedSourceInfoEpoch != UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch)
	{
		CachedSourceInfoEpoch = UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch;
		bWasCacheDirtied = true;
		CachedSourcePriorityWeights.Reset();
	}

	static_assert((uint8)EStreamingSourcePriority::Lowest == 255);
	CachedSourcePriorityWeights.Add(1.f - ((float)Info.Source.Priority / 255.f));

	// If cache was dirtied, use value, else use minimum with existing cached value
	CachedMinSourcePriority = bWasCacheDirtied ? (uint8)Info.Source.Priority : FMath::Min((uint8)Info.Source.Priority, CachedMinSourcePriority);
	return bWasCacheDirtied;
}

int32 UWorldPartitionRuntimeCell::SortCompare(const UWorldPartitionRuntimeCell* Other) const
{
	// Source priority (lower value is higher prio)
	const int32 Comparison = (int32)CachedMinSourcePriority - (int32)Other->CachedMinSourcePriority;
	// Cell priority (lower value is higher prio)
	return (Comparison != 0) ? Comparison : (Priority - Other->Priority);
}

bool UWorldPartitionRuntimeCell::IsDebugShown() const
{
	return FWorldPartitionDebugHelper::IsDebugRuntimeHashGridShown(GridName) &&
		   FWorldPartitionDebugHelper::IsDebugStreamingStatusShown(GetStreamingStatus()) &&
		   FWorldPartitionDebugHelper::AreDebugDataLayersShown(DataLayers) &&
		   FWorldPartitionDebugHelper::IsDebugCellNameShown(DebugName);
}

FLinearColor UWorldPartitionRuntimeCell::GetDebugStreamingPriorityColor() const
{
#if !UE_BUILD_SHIPPING
	if ((CachedSourceInfoEpoch == UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch) &&
		(DebugStreamingPriority >= 0.f && DebugStreamingPriority <= 1.f))
	{
		return FWorldPartitionDebugHelper::GetHeatMapColor(1.f - DebugStreamingPriority);
	}
#endif
	return FLinearColor::Transparent;
}

TArray<const UDataLayerInstance*> UWorldPartitionRuntimeCell::GetDataLayerInstances() const
{
	if (const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		return WorldDataLayers->GetDataLayerInstances(GetDataLayers());
	}

	return TArray<const UDataLayerInstance*>();
}

bool UWorldPartitionRuntimeCell::ContainsDataLayer(const UDataLayerAsset* DataLayerAsset) const
{
	if (const AWorldDataLayers* WorldDataLayers = GetWorld()->GetWorldDataLayers())
	{
		if (const UDataLayerInstance* DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerAsset))
		{
			return ContainsDataLayer(DataLayerInstance);
		}
	}

	return false;
}

bool UWorldPartitionRuntimeCell::ContainsDataLayer(const UDataLayerInstance* DataLayerInstance) const
{
	return GetDataLayers().Contains(DataLayerInstance->GetDataLayerFName());
}
