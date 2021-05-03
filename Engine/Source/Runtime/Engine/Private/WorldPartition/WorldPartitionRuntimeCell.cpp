// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "Engine/World.h"

UWorldPartitionRuntimeCell::UWorldPartitionRuntimeCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bIsAlwaysLoaded(false)
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