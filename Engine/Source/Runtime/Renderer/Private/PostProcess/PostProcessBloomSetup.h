// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

struct FBloomInputs
{
	FScreenPassTexture SceneColor;

	const FSceneDownsampleChain* SceneDownsampleChain = nullptr;
};

struct FBloomOutputs
{
	FScreenPassTexture SceneColor;
	FScreenPassTexture Bloom;
};

FBloomOutputs AddBloomPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FBloomInputs& Inputs);

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