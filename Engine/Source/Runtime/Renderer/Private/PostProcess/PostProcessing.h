// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessing.h: The center for all post processing activities.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "PostProcess/RenderingCompositionGraph.h"
#include "SceneRendering.h"
#include "ScreenPass.h"

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
	bool AllowFullPostProcessing(const FViewInfo& View, ERHIFeatureLevel::Type FeatureLevel);

	void RegisterHMDPostprocessPass(FPostprocessContext& Context, const FEngineShowFlags& EngineShowFlags) const;

	// @param VelocityRT only valid if motion blur is supported
	void Process(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& VelocityRT);

	void ProcessES2(FRHICommandListImmediate& RHICmdList, FScene* Scene, const FViewInfo& View);

	void ProcessPlanarReflection(FRHICommandListImmediate& RHICmdList, FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& VelocityRT, TRefCountPtr<IPooledRenderTarget>& OutFilteredSceneColor);

#if WITH_EDITOR
	void AddSelectionOutline(FPostprocessContext& Context);
#endif // WITH_EDITOR

	void AddGammaOnlyTonemapper(FPostprocessContext& Context);

	void OverrideRenderTarget(FRenderingCompositeOutputRef It, TRefCountPtr<IPooledRenderTarget>& RT, FPooledRenderTargetDesc& Desc);

	// Returns whether the scene color's alpha channel is supported within the post processing.
	static bool HasAlphaChannelSupport();
};

/** The global used for post processing. */
extern FPostProcessing GPostProcessing;
