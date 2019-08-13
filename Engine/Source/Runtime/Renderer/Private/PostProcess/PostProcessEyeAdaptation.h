// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessEyeAdaptation.h: Post processing eye adaptation implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RendererInterface.h"
#include "SceneRendering.h"
#include "PostProcess/RenderingCompositionGraph.h"

FORCEINLINE float EV100ToLuminance(float EV100)
{
	return 1.2 * FMath::Pow(2.0f, EV100);
}

FORCEINLINE float EV100ToLog2(float EV100)
{
	return EV100 + 0.263f; // Where .263 is log2(1.2)
}

FORCEINLINE float LuminanceToEV100(float Luminance)
{
	return FMath::Log2(Luminance / 1.2f);
}

FORCEINLINE float Log2ToEV100(float Log2)
{
	return Log2 - 0.263f; // Where .263 is log2(1.2)
}

bool IsAutoExposureMethodSupported(ERHIFeatureLevel::Type FeatureLevel, EAutoExposureMethod AutoExposureMethodId);

EAutoExposureMethod GetAutoExposureMethod(const FViewInfo& View);

BEGIN_SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, )
	SHADER_PARAMETER(float, ExposureLowPercent)
	SHADER_PARAMETER(float, ExposureHighPercent)
	SHADER_PARAMETER(float, MinAverageLuminance)
	SHADER_PARAMETER(float, MaxAverageLuminance)
	SHADER_PARAMETER(float, ExposureCompensation)
	SHADER_PARAMETER(float, DeltaWorldTime)
	SHADER_PARAMETER(float, ExposureSpeedUp)
	SHADER_PARAMETER(float, ExposureSpeedDown)
	SHADER_PARAMETER(float, HistogramScale)
	SHADER_PARAMETER(float, HistogramBias)
	SHADER_PARAMETER(float, LuminanceMin)
	SHADER_PARAMETER(float, CalibrationConstantInverse)
	SHADER_PARAMETER(float, WeightSlope)
END_SHADER_PARAMETER_STRUCT()

FEyeAdaptationParameters GetEyeAdaptationParameters(const FViewInfo& ViewInfo, ERHIFeatureLevel::Type MinFeatureLevel = ERHIFeatureLevel::SM5);

// Computes the eye-adaptation from HDRHistogram.
// ePId_Input0: HDRHistogram or nothing
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessEyeAdaptation : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessEyeAdaptation(bool bInIsComputePass)
	{
		bIsComputePass = bInIsComputePass;
		bPreferAsyncCompute = false;
		bPreferAsyncCompute &= (GNumAlternateFrameRenderingGroups == 1); // Can't handle multi-frame updates on async pipe
	}

	// Computes the a fix exposure to be used to replace the dynamic exposure when it's not available (< SM5).
	static float GetFixedExposure(const FViewInfo& View);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	virtual FRHIComputeFence* GetComputePassEndFence() const override { return AsyncEndFence; }

private:
	template <typename TRHICmdList>
	void DispatchCS(TRHICmdList& RHICmdList, FRenderingCompositePassContext& Context, FRHIUnorderedAccessView* DestUAV, IPooledRenderTarget* LastEyeAdaptation);

	FComputeFenceRHIRef AsyncEndFence;
};

// Write Log2(Luminance) in the alpha channel.
// ePId_Input0: Half-Res HDR scene color
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessBasicEyeAdaptationSetUp : public TRenderingCompositePassBase<1, 1>
{
public:
	
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
};

// ePId_Input0: Downsampled SceneColor Log
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessBasicEyeAdaptation : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessBasicEyeAdaptation(FIntPoint InDownsampledViewRect)
	: DownsampledViewRect(InDownsampledViewRect) 
	{
	}

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	FIntPoint DownsampledViewRect;
};