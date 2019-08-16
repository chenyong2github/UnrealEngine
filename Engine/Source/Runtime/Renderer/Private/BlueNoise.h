// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BlueNoise.h: Resources for Blue-Noise vectors on the GPU.
=============================================================================*/

#pragma once

#include "UniformBuffer.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Math/IntVector.h"

// Texture data is assumed to be in tiled representation where:
// 1) Dimensions.xy represents a single blue-noise tile
// 2) Dimensions.z represents the number of slices available
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FBlueNoise, )
SHADER_PARAMETER(FIntVector, Dimensions)
SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

inline void InitializeBlueNoise(FBlueNoise& BlueNoise)
{
	check(GEngine);
	BlueNoise.Dimensions = FIntVector(GEngine->BlueNoiseTexture->GetSizeX(), GEngine->BlueNoiseTexture->GetSizeX(), GEngine->BlueNoiseTexture->GetSizeY() / GEngine->BlueNoiseTexture->GetSizeX());
	BlueNoise.Texture = GEngine->BlueNoiseTexture->Resource->TextureRHI;
}
