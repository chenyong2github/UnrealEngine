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

// Computes subsurface scattering on the scene texture and produces a new scene texture as output.
FRDGTextureRef ComputeSubsurface(
	FRDGBuilder& GraphBuilder,
	FScreenPassContextRef Context,
	FRDGTextureRef SceneTexture);

// Visualizes subsurface scattering profiles by overlaying an image on the provided scene texture.
// Produces a new scene texture as output.
FRDGTextureRef VisualizeSubsurface(
	FRDGBuilder& GraphBuilder,
	FScreenPassContextRef Context,
	FRDGTextureRef SceneTexture);

// An adapter to connect the new Render Graph implementation to the legacy Composition Graph.
class FSubsurfaceVisualizeCompositePass : public TRenderingCompositePassBase<1, 1>
{
public:
	FSubsurfaceVisualizeCompositePass(FRHICommandList& CmdList);
	virtual ~FSubsurfaceVisualizeCompositePass() = default;

private:
	void Process(FRenderingCompositePassContext& Context) override;
	FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	void Release() override { delete this; }
};