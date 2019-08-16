// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessSubsurface.h: Screenspace subsurface scattering implementation
=============================================================================*/

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"

// Returns whether subsurface scattering is globally enabled.
bool IsSubsurfaceEnabled();

// Returns whether subsurface scattering is required for the provided view.
bool IsSubsurfaceRequiredForView(const FViewInfo& View);
	
// Returns whether checkerboard rendering is enabled for the provided format.
bool IsSubsurfaceCheckerboardFormat(EPixelFormat SceneColorFormat);

//////////////////////////////////////////////////////////////////////////
//! Shim methods to hook into the legacy pipeline until the full RDG conversion is complete.

void ComputeSubsurfaceShim(FRHICommandListImmediate& RHICmdList, const TArray<FViewInfo>& Views);

FRenderingCompositeOutputRef VisualizeSubsurfaceShim(
	FRHICommandListImmediate& RHICmdList,
	FRenderingCompositionGraph& Graph,
	FRenderingCompositeOutputRef Input);

//////////////////////////////////////////////////////////////////////////