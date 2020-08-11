// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"

BEGIN_SHADER_PARAMETER_STRUCT(FDMXPixelMappingRendererVertexShaderParameters, )
	SHADER_PARAMETER(FVector4, 				DrawRectanglePosScaleBias)
	SHADER_PARAMETER(FVector4, 				DrawRectangleInvTargetSizeAndTextureSize)
	SHADER_PARAMETER(FVector4, 				DrawRectangleUVScaleBias)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDMXPixelMappingRendererPixelShaderParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, 	InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, 	InputSampler)

	SHADER_PARAMETER(FIntPoint, 			InputTextureSize)
	SHADER_PARAMETER(FIntPoint, 			OutputTextureSize)
	SHADER_PARAMETER(FVector4, 				PixelFactor)
	SHADER_PARAMETER(FIntVector4, 			InvertPixel)
END_SHADER_PARAMETER_STRUCT()

struct FFDMXPixelMappingRendererPassData
{
	FDMXPixelMappingRendererVertexShaderParameters VSParameters;
	FDMXPixelMappingRendererPixelShaderParameters PSParameters;
};


/**
 * Pixel Mapping downsampling vertex shader
 */
class FDMXPixelMappingRendererVS
	: public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDMXPixelMappingRendererVS);
	SHADER_USE_PARAMETER_STRUCT(FDMXPixelMappingRendererVS, FGlobalShader);

	using FParameters = FDMXPixelMappingRendererVertexShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Pixel Mapping downsampling pixel shader
 */
class FDMXPixelMappingRendererPS
	: public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDMXPixelMappingRendererPS);
	SHADER_USE_PARAMETER_STRUCT(FDMXPixelMappingRendererPS, FGlobalShader);

	using FParameters = FDMXPixelMappingRendererPixelShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

