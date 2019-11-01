// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
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
	const FSceneTextureParameters* SceneTextures = nullptr;
	FRDGTextureRef ViewFamilyTexture = nullptr;
	FRDGTextureRef SceneColor = nullptr;
	FRDGTextureRef SeparateTranslucency = nullptr;
	FRDGTextureRef CustomDepth = nullptr;

	void Validate() const
	{
		check(ViewFamilyTexture);
		check(SceneTextures);
		check(SceneColor);
		check(SeparateTranslucency);
	}
};

void AddPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPostProcessingInputs& Inputs);

void AddDebugPostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPostProcessingInputs& Inputs);

// For compatibility with composition graph passes until they are ported to Render Graph.
class FPostProcessVS : public FScreenPassVS
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
	void ProcessES2(FRHICommandListImmediate& RHICmdList, FScene* Scene, const FViewInfo& View);

	void ProcessPlanarReflection(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& OutFilteredSceneColor);

#if WITH_EDITOR
	void AddSelectionOutline(FPostprocessContext& Context);
#endif // WITH_EDITOR

	void AddGammaOnlyTonemapper(FPostprocessContext& Context);

	void OverrideRenderTarget(FRenderingCompositeOutputRef It, TRefCountPtr<IPooledRenderTarget>& RT, FPooledRenderTargetDesc& Desc);
};

/** The global used for post processing. */
extern FPostProcessing GPostProcessing;
