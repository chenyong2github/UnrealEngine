// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

// Returns the motion blur quality level set by the 'r.MotionBlurQuality' CVar.
EMotionBlurQuality GetMotionBlurQuality();

FRDGTextureRef AddMotionBlurPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FIntRect ColorViewportRect,
	FIntRect VelocityViewportRect,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef VelocityTexture);

FRDGTextureRef AddVisualizeMotionBlurPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FIntRect ColorViewportRect,
	FIntRect VelocityViewportRect,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef VelocityTexture);