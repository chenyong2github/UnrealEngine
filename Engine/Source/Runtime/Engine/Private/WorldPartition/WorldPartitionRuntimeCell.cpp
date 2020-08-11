// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCell.h"

UWorldPartitionRuntimeCell::UWorldPartitionRuntimeCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bIsAlwaysLoaded(false)
{
}

#if WITH_EDITOR

void UWorldPartitionRuntimeCell::AddCellData(const UWorldPartitionRuntimeCellData* InCellData)
{
	CellDataMap.Add(InCellData->GetClass(), InCellData);
}

#endif

const UWorldPartitionRuntimeCellData* UWorldPartitionRuntimeCell::GetCellData(const TSubclassOf<UWorldPartitionRuntimeCellData> InCellDataClass) const
{
	return CellDataMap.FindRef(InCellDataClass);
}

UWorldPartitionRuntimeCellData::UWorldPartitionRuntimeCellData(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}
