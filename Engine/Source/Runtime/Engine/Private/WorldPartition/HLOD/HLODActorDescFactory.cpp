// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODActorDesc.h"

FWorldPartitionActorDesc* FHLODActorDescFactory::Create()
{
	return new FHLODActorDesc;
}
#endif