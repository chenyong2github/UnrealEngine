// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "ScenePrivate.h"

// Returns whether FFT bloom is enabled for the view.
bool IsFFTBloomEnabled(const FViewInfo& View);

struct FFFTBloomInputs
{
	FRDGTextureRef FullResolutionTexture;
	FIntRect FullResolutionViewRect;

	FRDGTextureRef HalfResolutionTexture;
	FIntRect HalfResolutionViewRect;
};

FRDGTextureRef AddFFTBloomPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FFFTBloomInputs& Inputs);