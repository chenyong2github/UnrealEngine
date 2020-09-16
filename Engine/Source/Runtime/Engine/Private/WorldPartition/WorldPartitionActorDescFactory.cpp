// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDesc.h"

FWorldPartitionActorDesc* FWorldPartitionActorDescFactory::Create()
{
	return new FWorldPartitionActorDesc;
}
#endif