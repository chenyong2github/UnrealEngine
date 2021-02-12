// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * WorldPartitionMiniMapHelper
 *
 * Helper class to build MiniMap texture in World Partition editor.
 *
 */
#pragma once

#if WITH_EDITOR
#include "Math/Box.h"
#include "Math/Matrix.h"

class AWorldPartitionMiniMap;
class UWorld;
class UTexture2D;
class AActor;

class ENGINE_API FWorldPartitionMiniMapHelper
{
public:
	static AWorldPartitionMiniMap* GetWorldPartitionMiniMap(UWorld* World, bool bCreateNewMiniMap=false);
	static void CaptureWorldMiniMapToTexture(UWorld* InWorld, UObject* InOuterForTexture, uint32 InMiniMapSize, UTexture2D*& InOutMiniMapTexture, FBox& OutWorldBounds);
private:
	static void CalTopViewOfWorld(FMatrix& OutProjectionMatrix, const FBox& WorldBox, uint32 ViewportWidth, uint32 ViewportHeight);
};
#endif

