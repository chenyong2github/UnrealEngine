// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "ScreenPass.h"

class FSceneTextureParameters;

struct FPixelInspectorInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] The tonemapped scene color.
	FScreenPassTexture SceneColor;

	// [Required] The scene color before tonemapping in HDR.
	FScreenPassTexture SceneColorBeforeTonemap;

	// [Required] The original scene color before processing.
	FScreenPassTexture OriginalSceneColor;

	// [Required] The scene textures with GBuffer data.
	const FSceneTextureParameters* SceneTextures = nullptr;
};

FScreenPassTexture AddPixelInspectorPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FPixelInspectorInputs& Inputs);

#endif