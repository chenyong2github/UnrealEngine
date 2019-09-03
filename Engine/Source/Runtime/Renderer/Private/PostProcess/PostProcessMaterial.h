// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMaterial.h: Post processing Material
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"
#include "ScreenPass.h"

class UMaterialInterface;

const uint32 kPostProcessMaterialInputCountMax = 5;

/** Named post process material slots. Inputs are aliased and have different semantics
 *  based on the post process material blend point, which is documented with the input.
 */
enum class EPostProcessMaterialInput : uint32
{
	// Always Active. Color from the previous stage of the post process chain.
	SceneColor = 0,

	// Always Active.
	SeparateTranslucency = 1,

	// Replace Tonemap Only. Half resolution combined bloom input.
	CombinedBloom = 2,

	// Buffer Visualization Only.
	PreTonemapHDRColor = 2,
	PostTonemapHDRColor = 3,

	// Active if separate velocity pass is used--i.e. not part of base pass; Not active during Replace Tonemap.
	Velocity = 4
};

struct FPostProcessMaterialInputs
{
	inline void SetInput(EPostProcessMaterialInput Input, FRDGTextureRef InTexture, FIntRect InViewport)
	{
		Textures[(uint32)Input] = InTexture;
		Viewports[(uint32)Input] = InViewport;
	}

	inline void GetInput(EPostProcessMaterialInput Input, FRDGTextureRef& OutTexture, FIntRect& OutViewport) const
	{
		OutTexture = Textures[(uint32)Input];
		OutViewport = Viewports[(uint32)Input];
	}

	inline void Validate() const
	{
		ValidateInputExists(EPostProcessMaterialInput::SceneColor);
		ValidateInputExists(EPostProcessMaterialInput::SeparateTranslucency);

		// Either override output format is valid or the override output texture is; not both.
		if (OverrideOutputFormat != PF_Unknown)
		{
			check(OverrideOutputTexture == nullptr);
		}
		if (OverrideOutputTexture)
		{
			check(OverrideOutputFormat == PF_Unknown);
		}
	}

	inline void ValidateInputExists(EPostProcessMaterialInput Input) const
	{
		FRDGTextureRef Texture = nullptr;
		FIntRect Viewport;
		GetInput(EPostProcessMaterialInput::SceneColor, Texture, Viewport);
		check(Texture);
		check(!Viewport.IsEmpty());
	}

	/** Array of input textures bound to the material. The first element represents the output from
	 *  the previous post process and is required. All other inputs are optional.
	 */
	TStaticArray<FRDGTextureRef, kPostProcessMaterialInputCountMax> Textures;

	/** Array of post process input viewports corresponding to @ref Textures */
	TStaticArray<FIntRect, kPostProcessMaterialInputCountMax> Viewports;

	/** The override output texture to use. If this is null, a new texture is created. */
	FRDGTextureRef OverrideOutputTexture = nullptr;

	/** The override output texture format to use if a new texture is created. */
	EPixelFormat OverrideOutputFormat = PF_Unknown;

	/** Custom stencil texture used for stencil operations. */
	FRDGTextureRef CustomDepthTexture = nullptr;

	/** Performs a vertical axis flip if the RHI allows it. */
	bool bFlipYAxis = false;
};

FRDGTextureRef AddPostProcessMaterialChain(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FPostProcessMaterialInputs& Inputs,
	EBlendableLocation Location);

FRDGTextureRef ComputePostProcessMaterial(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const UMaterialInterface* MaterialInterface,
	const FPostProcessMaterialInputs& Inputs);

BEGIN_SHADER_PARAMETER_STRUCT(FPostProcessMaterialInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
	SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
END_SHADER_PARAMETER_STRUCT()

FPostProcessMaterialInput GetPostProcessMaterialInput(FIntRect ViewportRect, FRDGTextureRef Texture, FRHISamplerState* Sampler);

BEGIN_SHADER_PARAMETER_STRUCT(FPostProcessMaterialParameters, )
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PostProcessOutput)
	SHADER_PARAMETER_STRUCT_ARRAY(FPostProcessMaterialInput, PostProcessInput, [kPostProcessMaterialInputCountMax])
	SHADER_PARAMETER_SAMPLER(SamplerState, PostProcessInput_BilinearSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepth)
	SHADER_PARAMETER(uint32, bFlipYAxis)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

//////////////////////////////////////////////////////////////////////////
//! Legacy Composition Graph Methods

FRenderingCompositePass* AddPostProcessMaterialPass(
	const FPostprocessContext& Context,
	const UMaterialInterface* MaterialInterface,
	EPixelFormat OutputFormat = PF_Unknown);

FRenderingCompositeOutputRef AddPostProcessMaterialReplaceTonemapPass(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SeparateTranslucency,
	FRenderingCompositeOutputRef CombinedBloom);

FRenderingCompositeOutputRef AddPostProcessMaterialChain(
	FPostprocessContext& Context,
	EBlendableLocation InLocation,
	FRenderingCompositeOutputRef SeparateTranslucency = FRenderingCompositeOutputRef(),
	FRenderingCompositeOutputRef PreTonemapHDRColor = FRenderingCompositeOutputRef(),
	FRenderingCompositeOutputRef PostTonemapHDRColor = FRenderingCompositeOutputRef(),
	FRenderingCompositeOutputRef PreFlattenVelocity = FRenderingCompositeOutputRef());

void AddHighResScreenshotMask(FPostprocessContext& Context);