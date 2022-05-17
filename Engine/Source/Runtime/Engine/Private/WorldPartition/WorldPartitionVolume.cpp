// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionVolume.h"

ADEPRECATED_WorldPartitionVolume::ADEPRECATED_WorldPartitionVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}