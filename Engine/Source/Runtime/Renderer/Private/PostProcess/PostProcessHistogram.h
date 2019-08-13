// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessHistogram.h: Post processing histogram implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"

FRDGTextureRef AddHistogramPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FIntRect SceneColorViewportRect,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef EyeAdaptationTexture);