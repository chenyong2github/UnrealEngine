// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"

/** The vertex data used to filter a texture. */
struct FDistortionVertex
{
	FVector2D	Position;
	FVector2D	TexR;
	FVector2D	TexG;
	FVector2D	TexB;
	float		VignetteFactor;
	float		TimewarpFactor;
};

struct FHMDDistortionInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] The input scene color and view rect.
	FScreenPassTexture SceneColor;
};

FScreenPassTexture AddDefaultHMDDistortionPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FHMDDistortionInputs& Inputs);

FScreenPassTexture AddHMDDistortionPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FHMDDistortionInputs& Inputs);

FRenderingCompositeOutputRef AddHMDDistortionPass(FRenderingCompositionGraph& Graph, FRenderingCompositeOutputRef Input);