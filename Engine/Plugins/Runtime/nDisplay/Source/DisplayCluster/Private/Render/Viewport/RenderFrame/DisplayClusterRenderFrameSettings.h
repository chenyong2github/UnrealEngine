// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameEnums.h"

// Settings for render frame builder
struct FDisplayClusterRenderFrameSettings
{
	// customize mono\stereo render modes
	EDisplayClusterRenderFrameMode RenderMode = EDisplayClusterRenderFrameMode::Mono;

	// Control mGPU for whole cluster
	EDisplayClusterMultiGPUMode MultiGPUMode = EDisplayClusterMultiGPUMode::Enabled;

	// Some frame postprocess require additional render targetable resources
	bool bShouldUseAdditionalFrameTargetableResource = false;

	// Preview RTT size multiplier
	float PreviewRenderTargetRatioMult = 1.f;

	// Multiply all downscale ratio inside all viewports settings for whole cluster
	float ClusterRenderTargetRatioMult = 1.f;

	// Multiply all buffer ratios for whole cluster by this value
	float ClusterBufferRatioMult = 1.f;

	// Allow warpblend render
	bool bAllowWarpBlend = true;

	// Render in Editor mode
	bool bIsRenderingInEditor = false;

	// Performance: Allow merge multiple viewports on single RTT with atlasing (required for bAllowViewFamilyMergeOptimization)
	bool bAllowRenderTargetAtlasing = false;

	// Performance: Allow viewfamily merge optimization (render multiple viewports contexts within single family)
	// [not implemented yet] Experimental
	EDisplayClusterRenderFamilyMode ViewFamilyMode = EDisplayClusterRenderFamilyMode::None;

	// Performance: Allow to use parent ViewFamily from parent viewport 
	// (icvfx has child viewports: lightcard and chromakey with prj_view matrices copied from parent viewport. May sense to use same viewfamily?)
	// [not implemented yet] Experimental
	bool bShouldUseParentViewportRenderFamily = false;
};


