// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMotionBlur.h: Post process MotionBlur implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"

// Returns whether motion blur is enabled for the requested view.
bool IsMotionBlurEnabled(const FViewInfo& View);

// Returns whether visualization of motion blur is enabled for the requested view.
bool IsVisualizeMotionBlurEnabled(const FViewInfo& View);

// The quality level of the motion blur pass.
enum class EMotionBlurQuality : uint32
{
	Low,
	Medium,
	High,
	VeryHigh,
	MAX
};

enum class EMotionBlurFilter : uint32
{
	Unified,
	Separable
};

// Returns the global setting for motion blur quality.
EMotionBlurQuality GetMotionBlurQuality();

// Returns the global setting for motion blur filter.
EMotionBlurFilter GetMotionBlurFilter();

struct FMotionBlurInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] The input scene color and view rect.
	FScreenPassTexture SceneColor;

	// [Required] The input scene depth and view rect.
	FScreenPassTexture SceneDepth;

	// [Required] The input scene velocity and view rect.
	FScreenPassTexture SceneVelocity;

	// [Required] Quality to use when processing motion blur.
	EMotionBlurQuality Quality = EMotionBlurQuality::VeryHigh;

	// [Required] Filter to use when processing motion blur.
	EMotionBlurFilter Filter = EMotionBlurFilter::Separable;
};

FScreenPassTexture AddMotionBlurPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMotionBlurInputs& Inputs);
FScreenPassTexture AddVisualizeMotionBlurPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMotionBlurInputs& Inputs);