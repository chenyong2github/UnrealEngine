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

// Returns whether the auto exposure method is supported by the feature level.
bool IsAutoExposureMethodSupported(ERHIFeatureLevel::Type FeatureLevel, EAutoExposureMethod AutoExposureMethodId);

// Returns the auto exposure method enabled by the view (including CVar override).
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

// Computes the a fixed exposure to be used to replace the dynamic exposure when it's not supported (< SM5).
float GetEyeAdaptationFixedExposure(const FViewInfo& View);

FRenderingCompositeOutputRef AddBasicEyeAdaptationSetupPass(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SceneColor,
	uint32 SceneColorDownsampleFactor);

FRenderingCompositeOutputRef AddBasicEyeAdaptationPass(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SceneColor,
	uint32 SceneColorDownsampleFactor);

FRenderingCompositeOutputRef AddHistogramEyeAdaptationPass(FPostprocessContext& Context, FRenderingCompositeOutputRef Histogram);