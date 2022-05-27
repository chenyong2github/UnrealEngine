// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

namespace UE::Landscape
{

/**
 * Shader that applies a texture-based height patch to a landscape heightmap.
 */
class LANDSCAPEPATCH_API FApplyLandscapeTextureHeightPatchPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FApplyLandscapeTextureHeightPatchPS);
	SHADER_USE_PARAMETER_STRUCT(FApplyLandscapeTextureHeightPatchPS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceHeightmap)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InHeightPatch)
		SHADER_PARAMETER_SAMPLER(SamplerState, InHeightPatchSampler)
		SHADER_PARAMETER(FMatrix44f, InHeightmapToPatch)
		// Height value to consider the 0 (deepest) values in height patch
		SHADER_PARAMETER(float, InPatchBaseHeight)
		// Amount of the patch edge to not apply in UV space. Generally set to 0.5/Dimensions to avoid applying
		// the edge half-pixels.
		SHADER_PARAMETER(FVector2f, InEdgeUVDeadBorder)
		// In patch texture space, the size of the margin across which the alpha falls from 1 to 0
		SHADER_PARAMETER(float, InFalloffWorldMargin)
		SHADER_PARAMETER(FVector2f, InPatchWorldDimensions)
		SHADER_PARAMETER(float, InHeightScale)

		// TODO: Apparently we're not allowed to use bools, and we're theoretically supposed to pack these into a bitfield.
		// Will be done in later CL.
		SHADER_PARAMETER(uint32, bRectangularFalloff)
		SHADER_PARAMETER(uint32, bApplyPatchAlpha)
		SHADER_PARAMETER(uint32, bAdditiveMode)

		RENDER_TARGET_BINDING_SLOTS() // Holds our output
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
	static void AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntRect& DestinationBounds);
};

/**
 * Simple shader that just offsets each height value in a height patch by a constant.
 */
class LANDSCAPEPATCH_API FOffsetHeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOffsetHeightmapPS);
	SHADER_USE_PARAMETER_STRUCT(FOffsetHeightmapPS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InHeightmap)
		SHADER_PARAMETER(float, InHeightOffset)
		RENDER_TARGET_BINDING_SLOTS() // Holds our output
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
	static void AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* InParameters);
};

/**
 * Simple shader for copying textures of potentially different resolutions.
 */
//~ Theoretically CopyToResolveTarget or AddCopyToResolveTargetPass should work, but I was unable to make them
//~ work without lots of complaints from the renderer.
class LANDSCAPEPATCH_API FSimpleTextureCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSimpleTextureCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FSimpleTextureCopyPS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, InSourceSampler)
		SHADER_PARAMETER(FVector2f, InDestinationResolution)
		RENDER_TARGET_BINDING_SLOTS() // Holds our output
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);
	static void AddToRenderGraph(FRDGBuilder& GraphBuilder, FRDGTextureRef SourceTexture, FRDGTextureRef DestinationTexture);
};

}//end UE::Landscape