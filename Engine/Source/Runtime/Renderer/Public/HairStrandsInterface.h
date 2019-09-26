// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: public interface for hair strands rendering.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

enum class EHairStrandsDebugMode : uint8
{
	None,
	SimHairStrands,
	RenderHairStrands,
	RenderHairUV,
	RenderHairRootUV,
	RenderHairSeed,
	RenderHairDimension,
	RenderHairRadiusVariation,
	Count
};

/// Return the active debug view mode
RENDERER_API EHairStrandsDebugMode GetHairStrandsDebugMode();

/// Return the number of sample subsample count used for the visibility pass
RENDERER_API uint32 GetHairVisibilitySampleCount();

struct FMinHairRadiusAtDepth1
{
	float Primary = 1;
	float Velocity = 1;
};

/// Compute the strand radius at a distance of 1 meter
RENDERER_API FMinHairRadiusAtDepth1 ComputeMinStrandRadiusAtDepth1(
	const FIntPoint& Resolution,
	const float FOV,
	const uint32 SampleCount,
	const float OverrideStrandHairRasterizationScale);

typedef void (*THairStrandsInterpolationFunction)(FRHICommandListImmediate& RHICmdList, struct FHairStrandsInterpolationInput* Input, struct FHairStrandsInterpolationOutput* Output);

struct FHairStrandsInterpolation
{
	struct FHairStrandsInterpolationInput* Input = nullptr;
	struct FHairStrandsInterpolationOutput* Output = nullptr;
	THairStrandsInterpolationFunction Function = nullptr;
};

RENDERER_API void RegisterHairStrands(uint64 Id, const FHairStrandsInterpolation& E);
RENDERER_API FHairStrandsInterpolation UnregisterHairStrands(uint64 Id);
void RunHairStrandsInterpolation(FRHICommandListImmediate& RHICmdList);


RENDERER_API bool IsHairStrandsSupported(const EShaderPlatform Platform);
bool IsHairStrandsEnable(EShaderPlatform Platform);