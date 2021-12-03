// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

class FSceneDownsampleChain;

enum class EBloomQuality : uint32
{
	Disabled,
	Q1,
	Q2,
	Q3,
	Q4,
	Q5,
	MAX
};

EBloomQuality GetBloomQuality();

FScreenPassTexture AddGaussianBloomPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FSceneDownsampleChain* SceneDownsampleChain);

struct FBloomSetupInputs
{
	// [Required]: The intermediate scene color being processed.
	FScreenPassTexture SceneColor;

	// [Required]: The scene eye adaptation texture.
	FRDGTextureRef EyeAdaptationTexture = nullptr;

	// [Required]: The bloom threshold to apply. Must be >0.
	float Threshold = 0.0f;
};

FScreenPassTexture AddBloomSetupPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FBloomSetupInputs& Inputs);