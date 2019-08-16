// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMotionBlur.h: Post process MotionBlur implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"

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

//////////////////////////////////////////////////////////////////////////
//! Shim methods to hook into the legacy pipeline until the full RDG conversion is complete.

FRenderingCompositeOutputRef ComputeMotionBlurShim(
	FRenderingCompositionGraph& Graph,
	FRenderingCompositeOutputRef ColorInput,
	FRenderingCompositeOutputRef DepthInput,
	FRenderingCompositeOutputRef VelocityInput,
	bool bVisualizeMotionBlur);

//////////////////////////////////////////////////////////////////////////