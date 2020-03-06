// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessPassThrough.h: Post processing pass through implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "RendererInterface.h"
#include "PostProcessParameters.h"
#include "PostProcess/RenderingCompositionGraph.h"

/** Encapsulates a simple copy pixel shader. */
class FPostProcessPassThroughPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessPassThroughPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/** Default constructor. */
	FPostProcessPassThroughPS() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter)

	/** Initialization constructor. */
	FPostProcessPassThroughPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	// FShader interface.
	//virtual bool Serialize(FArchive& Ar) override;

	template <typename TRHICmdList>
	void SetParameters(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context);
};

// ePId_Input0: Input image
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessPassThrough : public TRenderingCompositePassBase<1, 1>
{
public:
	// constructor
	// @param InDest - 0 if a new intermediate target should be created
	FRCPassPostProcessPassThrough(IPooledRenderTarget* InDest, bool bInAdditiveBlend = false);
	// constructor
	FRCPassPostProcessPassThrough(FPooledRenderTargetDesc InNewDesc);

	// interface FRenderingCompositePass ---------

	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

protected:

	// Override this function in derived classes to draw custom UI like legends. This is called after the fullscreen copy.
	virtual void DrawCustom(FRenderingCompositePassContext& Context) {}

private:
	// 0 if a new intermediate should be created
	IPooledRenderTarget* Dest;
	bool bAdditiveBlend;
	FPooledRenderTargetDesc NewDesc;
};