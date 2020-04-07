// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMobile.h: Mobile uber post processing.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"

class FViewInfo;

// return Depth of Field Scale if Gaussian DoF mode is active. 0.0f otherwise.
float GetMobileDepthOfFieldScale(const FViewInfo& View);

// Used to indicate the final PP stage which needs to be flipped on platforms that 'RHINeedsToSwitchVerticalAxis'
void SetMobilePassFlipVerticalAxis(const FRenderingCompositePass* FlipPass);
bool ShouldMobilePassFlipVerticalAxis(const FRenderingCompositePassContext& Context, const FRenderingCompositePass* ShouldFlipPass);

class FRCPassPostProcessBloomSetupES2 : public TRenderingCompositePassBase<2, 3>
{
public:
	FRCPassPostProcessBloomSetupES2(FIntRect InPrePostSourceViewportRect, bool bInUseViewRectSource, bool bInUseBloom, bool bInUseSun, bool bInUseDof, bool bInUseEyeAdaptation, bool bInUseMetalMSAAHDRDecode)
	: PrePostSourceViewportRect(InPrePostSourceViewportRect)
	, bUseViewRectSource(bInUseViewRectSource)
	, bUseBloom(bInUseBloom)
	, bUseSun(bInUseSun)
	, bUseDof(bInUseDof)
	, bUseEyeAdaptation(bInUseEyeAdaptation)
	, bUseMetalMSAAHDRDecode(bInUseMetalMSAAHDRDecode)
	{ }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntRect PrePostSourceViewportRect;
	bool bUseViewRectSource;
	bool bUseBloom;
	bool bUseSun;
	bool bUseDof;
	bool bUseEyeAdaptation;
	bool bUseMetalMSAAHDRDecode;
};

class FRCPassPostProcessDofNearES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessDofNearES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	void SetShader(const FRenderingCompositePassContext& Context);
};

class FRCPassPostProcessDofDownES2 : public TRenderingCompositePassBase<2, 1>
{
public:
	FRCPassPostProcessDofDownES2(FIntRect InPrePostSourceViewportRect, bool bInUseViewRectSource) : PrePostSourceViewportRect(InPrePostSourceViewportRect), bUseViewRectSource(bInUseViewRectSource) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntRect PrePostSourceViewportRect;
	bool bUseViewRectSource;
	void SetShader(const FRenderingCompositePassContext& Context);
};

class FRCPassPostProcessDofBlurES2 : public TRenderingCompositePassBase<2, 1>
{
public:
	FRCPassPostProcessDofBlurES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
};

class FRCPassIntegrateDofES2 : public TRenderingCompositePassBase<3, 1>
{
public:
	FRCPassIntegrateDofES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
};

class FRCPassPostProcessBloomDownES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessBloomDownES2(FIntPoint InPrePostSourceViewportSize, float InScale) : PrePostSourceViewportSize(InPrePostSourceViewportSize), Scale(InScale) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	float Scale;
};

class FRCPassPostProcessBloomUpES2 : public TRenderingCompositePassBase<2, 1>
{
public:
	FRCPassPostProcessBloomUpES2(FIntPoint InPrePostSourceViewportSize, FVector2D InScaleAB, FVector4& InTintA, FVector4& InTintB) : PrePostSourceViewportSize(InPrePostSourceViewportSize), ScaleAB(InScaleAB), TintA(InTintA), TintB(InTintB) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	FVector2D ScaleAB;
	FVector4 TintA;
	FVector4 TintB;
};

class FRCPassPostProcessSunMaskES2 : public TRenderingCompositePassBase<1, 2>
{
public:
	FRCPassPostProcessSunMaskES2(FIntPoint InPrePostSourceViewportSize, bool bInUseSun, bool bInUseDof, bool bInUseDepthTexture, bool bInUseMetalMSAAHDRDecode)
	: PrePostSourceViewportSize(InPrePostSourceViewportSize)
	, bUseSun(bInUseSun)
	, bUseDof(bInUseDof)
	, bUseDepthTexture(bInUseDepthTexture)
	, bUseMetalMSAAHDRDecode(bInUseMetalMSAAHDRDecode)
	{ }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	bool bUseSun;
	bool bUseDof;
	bool bUseDepthTexture;
	bool bUseMetalMSAAHDRDecode;
};

class FRCPassPostProcessSunAlphaES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessSunAlphaES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	void SetShader(const FRenderingCompositePassContext& Context);
};

class FRCPassPostProcessSunBlurES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessSunBlurES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
};

class FRCPassPostProcessSunMergeES2 : public TRenderingCompositePassBase<3, 1>
{
public:
	FRCPassPostProcessSunMergeES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	TShaderRef<FShader> SetShader(const FRenderingCompositePassContext& Context);
};

class FRCPassPostProcessSunAvgES2 : public TRenderingCompositePassBase<2, 1>
{
public:
	FRCPassPostProcessSunAvgES2(FIntPoint InPrePostSourceViewportSize) : PrePostSourceViewportSize(InPrePostSourceViewportSize) { }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	FIntPoint PrePostSourceViewportSize;
	void SetShader(const FRenderingCompositePassContext& Context);
};

class FRCPassPostProcessAaES2 : public TRenderingCompositePassBase<2, 1>
{
public:
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
private:
	void SetShader(const FRenderingCompositePassContext& Context);
};

class FRCPassPostProcessAverageLuminanceES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
};

class FRCPassPostProcessBasicEyeAdaptationES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
};

class FRCPassPostProcessHistogramES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
};

// Computes the eye-adaptation from HDRHistogram.
// ePId_Input0: HDRHistogram or nothing
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessHistogramEyeAdaptationES2 : public TRenderingCompositePassBase<1, 1>
{
public:
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
};

/** Pixel shader to decode the input color and  copy pixels from src to dst only for mobile metal platform. */
class FMSAADecodeAndCopyRectPS_ES2 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMSAADecodeAndCopyRectPS_ES2);
	SHADER_USE_PARAMETER_STRUCT(FMSAADecodeAndCopyRectPS_ES2, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMetalMobilePlatform(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};