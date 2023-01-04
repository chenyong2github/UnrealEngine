// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BlueNoise.h: Resources for Blue-Noise vectors on the GPU.
=============================================================================*/

#pragma once

#include "Math/IntVector.h"
#include "ShaderParameterMacros.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FBlueNoise, RENDERER_API)
	SHADER_PARAMETER(FIntVector, Dimensions)
	SHADER_PARAMETER(FIntVector, ModuloMasks)
	SHADER_PARAMETER_TEXTURE(Texture2D, ScalarTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, Vec2Texture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern RENDERER_API FBlueNoise GetBlueNoiseParameters();
