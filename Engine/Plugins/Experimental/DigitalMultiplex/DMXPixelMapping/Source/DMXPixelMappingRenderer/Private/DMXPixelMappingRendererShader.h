// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"

/**
 * Used in shader permutation for determining number of samples to use in texture blending.
 * If adding to this you must also adjust the public facing option: 'EPixelBlendingQuality' under the runtime module's DMXPixelMappingOutputComponent.h
 */
enum class EDMXPixelShaderBlendingQuality : uint8
{
	Low,
	Medium,
	High,

	MAX
};

BEGIN_SHADER_PARAMETER_STRUCT(FDMXPixelMappingRendererVertexShaderParameters, )
	SHADER_PARAMETER(FVector4, 				DrawRectanglePosScaleBias)
	SHADER_PARAMETER(FVector4, 				DrawRectangleInvTargetSizeAndTextureSize)
	SHADER_PARAMETER(FVector4, 				DrawRectangleUVScaleBias)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDMXPixelMappingRendererPixelShaderParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, 	InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState,	InputSampler)

	SHADER_PARAMETER(FIntPoint, 			InputTextureSize)
	SHADER_PARAMETER(FIntPoint, 			OutputTextureSize)
	SHADER_PARAMETER(FVector4, 				PixelFactor)
	SHADER_PARAMETER(FIntVector4, 			InvertPixel)
	SHADER_PARAMETER(FVector2D,				UVCellSize)
END_SHADER_PARAMETER_STRUCT()


struct FFDMXPixelMappingRendererPassData
{
	FDMXPixelMappingRendererVertexShaderParameters VSParameters;
	FDMXPixelMappingRendererPixelShaderParameters PSParameters;
};

class FDMXPixelBlendingQualityDimension : SHADER_PERMUTATION_ENUM_CLASS("PIXELBLENDING_QUALITY", EDMXPixelShaderBlendingQuality);
class FDMXVertexUVDimension : SHADER_PERMUTATION_BOOL("VERTEX_UV_STATIC_CALCULATION");

/**
 * Pixel Mapping downsampling vertex shader
 */
class FDMXPixelMappingRendererVS
	: public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDMXPixelMappingRendererVS);
	SHADER_USE_PARAMETER_STRUCT(FDMXPixelMappingRendererVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FDMXPixelBlendingQualityDimension, FDMXVertexUVDimension>;
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

	using FPermutationDomain = TShaderPermutationDomain<FDMXPixelBlendingQualityDimension, FDMXVertexUVDimension>;
	using FParameters = FDMXPixelMappingRendererPixelShaderParameters;
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

