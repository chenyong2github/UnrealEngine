// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SubsurfaceBurleyNormalized.h: Screenspace Burley subsurface scattering implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"

void ComputeBurleySubsurfaceForView(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTextureRef SceneTexture,
	FRDGTextureRef SceneTextureOutput,
	ERenderTargetLoadAction SceneTextureLoadAction);