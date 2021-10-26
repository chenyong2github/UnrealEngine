// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Coord system utilities
 *
 * Translates between Unreal and Recast coords.
 * Unreal: x, y, z
 * Recast: -x, z, -y
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"


extern NAVIGATIONSYSTEM_API FVector Unreal2RecastPoint(const FVector::FReal* UnrealPoint);
#if !UE_LARGE_WORLD_COORDINATES_DISABLED 
UE_DEPRECATED(5.0, "UnrealPoint should now be a FReal pointer!")
extern NAVIGATIONSYSTEM_API FVector Unreal2RecastPoint(const float* UnrealPoint);
#endif // !UE_LARGE_WORLD_COORDINATES_DISABLED
extern NAVIGATIONSYSTEM_API FVector Unreal2RecastPoint(const FVector& UnrealPoint);
extern NAVIGATIONSYSTEM_API FBox Unreal2RecastBox(const FBox& UnrealBox);
extern NAVIGATIONSYSTEM_API FMatrix Unreal2RecastMatrix();

extern NAVIGATIONSYSTEM_API FVector Recast2UnrealPoint(const FVector::FReal* RecastPoint);
#if !UE_LARGE_WORLD_COORDINATES_DISABLED 
UE_DEPRECATED(5.0, "RecastPoint should now be a FReal pointer!")
extern NAVIGATIONSYSTEM_API FVector Recast2UnrealPoint(const float* RecastPoint);
#endif // !UE_LARGE_WORLD_COORDINATES_DISABLED
extern NAVIGATIONSYSTEM_API FVector Recast2UnrealPoint(const FVector& RecastPoint);

extern NAVIGATIONSYSTEM_API FBox Recast2UnrealBox(const FVector::FReal* RecastMin, const FVector::FReal* RecastMax);
#if !UE_LARGE_WORLD_COORDINATES_DISABLED 
UE_DEPRECATED(5.0, "RecastMin and RecastMax should now be a FReal pointers!")
extern NAVIGATIONSYSTEM_API FBox Recast2UnrealBox(const float* RecastMin, const float* RecastMax);
#endif // !UE_LARGE_WORLD_COORDINATES_DISABLED
extern NAVIGATIONSYSTEM_API FBox Recast2UnrealBox(const FBox& RecastBox);
extern NAVIGATIONSYSTEM_API FMatrix Recast2UnrealMatrix();
extern NAVIGATIONSYSTEM_API FColor Recast2UnrealColor(const unsigned int RecastColor);
