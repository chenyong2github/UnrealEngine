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
, CachedSourcePriority(0)
, CachedSourceInfoEpoch(INT_MIN)
{}

#if WITH_EDITOR

void UWorldPartitionRuntimeCell::AddCellData(const UWorldPartitionRuntimeCellData* InCellData)
{
	CellDataMap.Add(InCellData->GetClass(), InCellData);
}

void UWorldPartitionRuntimeCell::SetDataLayers(const TArray<const UDataLayer*>& InDataLayers)
{
	check(DataLayers.IsEmpty());
	DataLayers.Reserve(InDataLayers.Num());
	for (const UDataLayer* DataLayer : InDataLayers)
	{
		check(DataLayer->IsDynamicallyLoaded());
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

bool UWorldPartitionRuntimeCell::CacheStreamingSourceInfo(const FWorldPartitionStreamingSource& Source) const
{
	bool bWasCacheDirtied = false;
	if (CachedSourceInfoEpoch != UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch)
	{
		CachedSourceInfoEpoch = UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch;
		bWasCacheDirtied = true;
	}

	// If cache was dirtied, use value, else use minimum with existing cached value
	CachedSourcePriority = bWasCacheDirtied ? (int32)Source.Priority : FMath::Min((int32)Source.Priority, CachedSourcePriority);
	return bWasCacheDirtied;
}

int32 UWorldPartitionRuntimeCell::SortCompare(const UWorldPartitionRuntimeCell* Other) const
{
	// Source priority (lower value is higher prio)
	const int32 Comparison = CachedSourcePriority - Other->CachedSourcePriority;
	// Cell priority (lower value is higher prio)
	return (Comparison != 0) ? Comparison : (Priority - Other->Priority);
}

bool UWorldPartitionRuntimeCell::IsDebugShown() const
{
	return FWorldPartitionDebugHelper::IsDebugRuntimeHashGridShown(GridName) &&
		   FWorldPartitionDebugHelper::IsDebugStreamingStatusShown(GetStreamingStatus()) &&
		   FWorldPartitionDebugHelper::AreDebugDataLayersShown(DataLayers) &&
		   FWorldPartitionDebugHelper::IsDebugCellNameShow(DebugName);
}

const UWorldPartitionRuntimeCellData* UWorldPartitionRuntimeCell::GetCellData(const TSubclassOf<UWorldPartitionRuntimeCellData> InCellDataClass) const
{
	return CellDataMap.FindRef(InCellDataClass);
}

UWorldPartitionRuntimeCellData::UWorldPartitionRuntimeCellData(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{}