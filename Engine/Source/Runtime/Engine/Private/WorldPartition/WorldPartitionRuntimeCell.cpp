// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "Engine/World.h"

int32 UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch = 0;

UWorldPartitionRuntimeCell::UWorldPartitionRuntimeCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bIsAlwaysLoaded(false)
, Priority(0)
, CachedMinSourcePriority((uint8)EStreamingSourcePriority::Lowest)
, CachedSourceInfoEpoch(INT_MIN)
#if !UE_BUILD_SHIPPING
, DebugStreamingPriority(-1.f)
#endif
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeCell::SetDataLayers(const TArray<const UDataLayer*>& InDataLayers)
{
	check(DataLayers.IsEmpty());
	DataLayers.Reserve(InDataLayers.Num());
	for (const UDataLayer* DataLayer : InDataLayers)
	{
		check(DataLayer->IsRuntime());
		DataLayers.Add(DataLayer->GetFName());
	}
	DataLayers.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
	UpdateDebugName();
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

	const AWorldDataLayers* WorldDataLayers = GetOuterUWorldPartition()->GetWorld()->GetWorldDataLayers();
	TArray<const UDataLayer*> DataLayerObjects;
	if (WorldDataLayers && (DataLayerCount > 0))
	{
		Builder += TEXT(" DL[");
		for (int i = 0; i < DataLayerCount; ++i)
		{
			const UDataLayer* DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayers[i]);
			DataLayerObjects.Add(DataLayer);
			Builder += DataLayer->GetDataLayerLabel().ToString();
			Builder += TEXT(",");
		}
		Builder += FString::Printf(TEXT("ID:%X]"), FDataLayersID(DataLayerObjects).GetHash());
	}
	DebugName = Builder.ToString();
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