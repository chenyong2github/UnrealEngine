// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessTemporalAA.h: Post process MotionBlur implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"

FRenderingCompositeOutputRef ComputeMitchellNetravaliDownsample(
	FRenderingCompositionGraph& Graph,
	FRenderingCompositeOutputRef Input,
	FIntRect InputViewport,
	FScreenPassTextureViewport OutputViewport);