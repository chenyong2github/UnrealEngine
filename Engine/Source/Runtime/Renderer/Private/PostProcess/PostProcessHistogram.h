// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

class FEyeAdaptationParameters;

FRDGTextureRef AddHistogramPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	const FIntRect SceneColorViewportRect,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef EyeAdaptationTexture);