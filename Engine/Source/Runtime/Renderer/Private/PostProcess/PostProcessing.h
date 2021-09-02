// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
#include "TranslucentRendering.h"
#include "PostProcess/RenderingCompositionGraph.h"

class FSceneTextureParameters;

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

struct FPostProcessingInputs
{
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures = nullptr;
	FRDGTextureRef ViewFamilyTexture = nullptr;
	const FSeparateTranslucencyTextures* SeparateTranslucencyTextures = nullptr;
	const struct FHairStrandsRenderingData* HairDatas = nullptr;

	void Validate() const
	{
		check(SceneTextures);
		check(ViewFamilyTexture);
		check(SeparateTranslucencyTextures);
	}
};

void AddPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, int32 ViewIndex, const FPostProcessingInputs& Inputs);

void AddDebugViewPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPostProcessingInputs& Inputs);

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

void AddMobilePostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMobilePostProcessingInputs& Inputs, bool bMobileMSAA);

void AddBasicPostProcessPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View);

// For compatibility with composition graph passes until they are ported to Render Graph.
class RENDERER_API FPostProcessVS : public FScreenPassVS
{
public:
	FPostProcessVS() = default;
	FPostProcessVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FScreenPassVS(Initializer)
	{}

	void SetParameters(const FRenderingCompositePassContext&) {}
	void SetParameters(FRHICommandList&, FRHIUniformBuffer*) {}
};

/** The context used to setup a post-process pass. */
class FPostprocessContext
{
public:
	FPostprocessContext(FRHICommandListImmediate& InRHICmdList, FRenderingCompositionGraph& InGraph, const FViewInfo& InView);

	FRHICommandListImmediate& RHICmdList;
	FRenderingCompositionGraph& Graph;
	const FViewInfo& View;

	// 0 if there was no scene color available at constructor call time
	FRenderingCompositePass* SceneColor;
	// never 0
	FRenderingCompositePass* SceneDepth;

	FRenderingCompositeOutputRef FinalOutput;
};

/**
 * The center for all post processing activities.
 */
class FPostProcessing
{
public:
	void ProcessPlanarReflection(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& OutFilteredSceneColor);

	void OverrideRenderTarget(FRenderingCompositeOutputRef It, TRefCountPtr<IPooledRenderTarget>& RT, FPooledRenderTargetDesc& Desc);
};

/** The global used for post processing. */
extern FPostProcessing GPostProcessing;
