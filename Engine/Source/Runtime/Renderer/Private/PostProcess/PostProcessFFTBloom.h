// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"

struct FBloomOutputs;

// Returns whether FFT bloom is enabled for the view.
bool IsFFTBloomEnabled(const FViewInfo& View);

struct FFFTBloomInputs
{
	FRDGTextureRef FullResolutionTexture;
	FIntRect FullResolutionViewRect;

	FRDGTextureRef HalfResolutionTexture;
	FIntRect HalfResolutionViewRect;
};

struct FFFTBloomOutput
{
	FScreenPassTexture BloomTexture;
	FRDGBufferRef SceneColorApplyParameters = nullptr;
};

bool IsFFTBloomFullResolutionEnabled();
bool IsFFTBloomQuarterResolutionEnabled();

FFFTBloomOutput AddFFTBloomPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FFFTBloomInputs& Inputs);
