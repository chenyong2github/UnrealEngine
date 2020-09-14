// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorPartition/PartitionActorDescFactory.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "ActorRegistry.h"

#if WITH_EDITOR
FWorldPartitionActorDesc* FPartitionActorDescFactory::CreateInstance(const FWorldPartitionActorDescInitData& ActorDescInitData)
{
	FWorldPartitionActorDescData Data;
	if (!ReadMetaData(ActorDescInitData, Data))
	{
		return nullptr;
	}

	int32 GridSize;
	static const FName NAME_GridSize(TEXT("GridSize"));
	if (!FActorRegistry::ReadActorMetaData(NAME_GridSize, GridSize, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	int64 GridIndexX;
	static const FName NAME_GridIndexX(TEXT("GridIndexX"));
	if (!FActorRegistry::ReadActorMetaData(NAME_GridIndexX, GridIndexX, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	int64 GridIndexY;
	static const FName NAME_GridIndexY(TEXT("GridIndexY"));
	if (!FActorRegistry::ReadActorMetaData(NAME_GridIndexY, GridIndexY, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	int64 GridIndexZ;
	static const FName NAME_GridIndexZ(TEXT("GridIndexZ"));
	if (!FActorRegistry::ReadActorMetaData(NAME_GridIndexZ, GridIndexZ, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	return new FPartitionActorDesc(Data, (uint32)GridSize, GridIndexX, GridIndexY, GridIndexZ);
}

FWorldPartitionActorDesc* FPartitionActorDescFactory::CreateInstance(AActor* InActor)
{
	return new FPartitionActorDesc(InActor);
}
#endif