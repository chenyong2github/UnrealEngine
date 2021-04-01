// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCell.h"

UWorldPartitionRuntimeCell::UWorldPartitionRuntimeCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bIsAlwaysLoaded(false)
{}

#if WITH_EDITOR

void UWorldPartitionRuntimeCell::AddCellData(const UWorldPartitionRuntimeCellData* InCellData)
{
	CellDataMap.Add(InCellData->GetClass(), InCellData);
}

void UWorldPartitionRuntimeCell::SetDataLayers(const TArray<const UDataLayer*> InDataLayers)
{
	check(DataLayers.IsEmpty());
	DataLayers.Reserve(InDataLayers.Num());
	for (const UDataLayer* DataLayer : InDataLayers)
	{
		check(DataLayer->IsDynamicallyLoaded());
		DataLayers.Add(DataLayer->GetFName());
	}
}

#endif

const UWorldPartitionRuntimeCellData* UWorldPartitionRuntimeCell::GetCellData(const TSubclassOf<UWorldPartitionRuntimeCellData> InCellDataClass) const
{
	return CellDataMap.FindRef(InCellDataClass);
}

UWorldPartitionRuntimeCellData::UWorldPartitionRuntimeCellData(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{}
