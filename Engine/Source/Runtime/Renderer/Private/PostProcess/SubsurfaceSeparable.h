// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SubsurfaceSeparable.h: Screenspace Burley subsurface scattering implementation.
=============================================================================*/

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"
#include "PostProcess/SubsurfaceCommon.h"

bool IsSeparableSubsurfaceCheckerboardFormat(EPixelFormat SceneColorFormat);

void ComputeSeparableSubsurfaceForView(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTextureRef SceneTexture,
	FRDGTextureRef SceneTextureOutput,
	ERenderTargetLoadAction SceneTextureLoadAction);