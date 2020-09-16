// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"

FWorldPartitionActorDesc* FLevelInstanceActorDescFactory::Create()
{
	return new FLevelInstanceActorDesc;
}
#endif