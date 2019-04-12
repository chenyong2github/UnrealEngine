// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMotionBlur.h: Post process MotionBlur implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"

// Returns whether motion blur is enabled for the requested view.
bool IsMotionBlurEnabled(const FViewInfo& View);

// The quality level of the motion blur pass.
enum class EMotionBlurQuality : uint32
{
	Low,
	Medium,
	High,
	VeryHigh,
	MAX
};

// Returns the motion blur quality level set by the 'r.MotionBlurQuality' cvar.
EMotionBlurQuality GetMotionBlurQuality();

/**
 * Computes motion blur using the provided color, depth, and velocity textures. Returns
 * the blurred color result. The Color viewport does not need to match Depth / Velocity
 * viewports, but Depth / Velocity must match each other.
 */
FScreenPassTexture ComputeMotionBlur(
	FRDGBuilder& GraphBuilder,
	FScreenPassContextRef Context,
	const FScreenPassTexture& ColorTexture,
	const FScreenPassTexture& DepthTexture,
	const FScreenPassTexture& VelocityTexture);

/**
 * Visualizes motion blur velocities and outputs the result. The Color viewport does not
 * need to match Depth / Velocity viewports, but Depth / Velocity must match each other.
 */
FScreenPassTexture VisualizeMotionBlur(
	FRDGBuilder& GraphBuilder,
	FScreenPassContextRef Context,
	const FScreenPassTexture& ColorTexture,
	const FScreenPassTexture& DepthTexture,
	const FScreenPassTexture& VelocityTexture);