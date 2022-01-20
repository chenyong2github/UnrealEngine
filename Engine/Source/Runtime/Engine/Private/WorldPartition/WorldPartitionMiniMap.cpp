// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionMiniMap.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

AWorldPartitionMiniMap::AWorldPartitionMiniMap(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MiniMapWorldBounds(ForceInit)
	, MiniMapTexture(nullptr)
{
}

#if WITH_EDITOR
void AWorldPartitionMiniMap::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	MiniMapTileSize = FMath::Clamp<uint32>(FMath::RoundUpToPowerOfTwo(MiniMapTileSize), 256, 8192);
}
#endif

#undef LOCTEXT_NAMESPACE
