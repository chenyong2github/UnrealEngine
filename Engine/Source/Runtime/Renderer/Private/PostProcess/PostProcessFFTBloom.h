// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"

struct FBloomOutputs;

// Returns whether FFT bloom is enabled for the view.
bool IsFFTBloomEnabled(const FViewInfo& View);

float GetFFTBloomResolutionFraction();

struct FFFTBloomOutput
{
	FScreenPassTexture BloomTexture;
	FRDGBufferRef SceneColorApplyParameters = nullptr;
};

float GetFFTBloomResolutionFraction();

FFFTBloomOutput AddFFTBloomPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassTexture& InputSceneColor, float InputResolutionFraction);
