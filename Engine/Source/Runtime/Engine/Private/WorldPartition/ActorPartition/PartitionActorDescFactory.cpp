// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorPartition/PartitionActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"

FWorldPartitionActorDesc* FPartitionActorDescFactory::Create()
{
	return new FPartitionActorDesc;
}
#endif