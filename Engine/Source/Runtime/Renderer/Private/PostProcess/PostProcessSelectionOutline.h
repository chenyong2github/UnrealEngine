// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "ScreenPass.h"
#include "OverridePassSequence.h"
#include "PostProcess/RenderingCompositionGraph.h"

struct FSelectionOutlineInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] The scene color to composite with selection outlines.
	FScreenPassTexture SceneColor;

	// [Required] The scene depth to composite with selection outlines.
	FScreenPassTexture SceneDepth;
};

FScreenPassTexture AddSelectionOutlinePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FSelectionOutlineInputs& Inputs);

FRenderingCompositeOutputRef AddSelectionOutlinePass(FRenderingCompositionGraph& Graph, FRenderingCompositeOutputRef Input);

#endif
