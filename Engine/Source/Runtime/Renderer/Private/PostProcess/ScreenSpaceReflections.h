// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScreenSpaceReflections.h: Post processing Screen Space Reflections implementation.
=============================================================================*/

#pragma once

#include "RenderGraph.h"

class FViewInfo;
class FSceneViewFamilyBlackboard;

FRDGTextureRef RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneViewFamilyBlackboard& SceneBlackboard,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View);

bool ShouldRenderScreenSpaceReflections(const FViewInfo& View);

bool IsSSRTemporalPassRequired(const FViewInfo& View, bool bCheckSSREnabled = true);
