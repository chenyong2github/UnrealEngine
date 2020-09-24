// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Landscape/LandscapeActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/Landscape/LandscapeActorDesc.h"

FWorldPartitionActorDesc* FLandscapeActorDescFactory::Create()
{
	return new FLandscapeActorDesc;
}
#endif