// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionMiniMapVolume.h"

AWorldPartitionMiniMapVolume::AWorldPartitionMiniMapVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(TEXT("Sprite")))
{
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}