// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
#include "TranslucentRendering.h"
#include "SystemTextures.h"
#include "ScenePrivate.h"

class FSceneTextureParameters;

namespace Nanite
{
	struct FRasterResults;
}

enum class EPostProcessAAQuality : uint32
{
	Disabled,
	// Faster FXAA
	VeryLow,
	// FXAA
	Low,
	// Faster Temporal AA
	Medium,
	// Temporal AA
	High,
	VeryHigh,
	MAX
};

// Returns the quality of post process anti-aliasing defined by CVar.
EPostProcessAAQuality GetPostProcessAAQuality();

// Returns whether the full post process pipeline is enabled. Otherwise, the minimal set of operations are performed.
bool IsPostProcessingEnabled(const FViewInfo& View);

// Returns whether the post process pipeline supports using compute passes.
bool IsPostProcessingWithComputeEnabled(ERHIFeatureLevel::Type FeatureLevel);

// Returns whether the post process pipeline supports propagating the alpha channel.
bool IsPostProcessingWithAlphaChannelSupported();

using FPostProcessVS = FScreenPassVS;

struct FPostProcessingInputs
{
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures = nullptr;
	FRDGTextureRef ViewFamilyTexture = nullptr;
	const FSeparateTranslucencyTextures* SeparateTranslucencyTextures = nullptr;

	void Validate() const
	{
		check(SceneTextures);
		check(ViewFamilyTexture);
		check(SeparateTranslucencyTextures);
	}
};

void AddPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPostProcessingInputs& Inputs, const Nanite::FRasterResults* NaniteRasterResults, FInstanceCullingManager& InstanceCullingManager);

void AddDebugViewPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPostProcessingInputs& Inputs, const Nanite::FRasterResults* NaniteRasterResults);

#if !(UE_BUILD_SHIPPING)

void AddVisualizeCalibrationMaterialPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPostProcessingInputs& Inputs, const UMaterialInterface* InMaterialInterface);

#endif


struct FMobilePostProcessingInputs
{
	TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> SceneTextures = nullptr;
	FRDGTextureRef ViewFamilyTexture = nullptr;

	void Validate() const
	{
		check(ViewFamilyTexture);
		check(SceneTextures);
	}
};

void AddMobilePostProcessingPasses(FRDGBuilder& GraphBuilder, FScene* Scene, const FViewInfo& View, const FMobilePostProcessingInputs& Inputs, FInstanceCullingManager& InstanceCullingManager);

void AddBasicPostProcessPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View);

FRDGTextureRef AddProcessPlanarReflectionPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture);
