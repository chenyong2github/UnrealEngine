// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

class FDisplayClusterViewport_Context
{
public:
	FDisplayClusterViewport_Context(const uint32 InContextNum, const enum EStereoscopicPass InStereoscopicEye, const EStereoscopicPass InStereoscopicPass)
		: ContextNum(InContextNum)
		, StereoscopicEye(InStereoscopicEye)
		, StereoscopicPass(InStereoscopicPass)
	{}

public:
	const uint32            ContextNum;

	const EStereoscopicPass StereoscopicEye;
	const EStereoscopicPass StereoscopicPass;

	// View index in view
	uint32 RenderFrameViewIndex = 0;

	// Camera location and orientation
	FVector  ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;

	// Projection Matrix
	FMatrix ProjectionMatrix = FMatrix::Identity;

	// Overscan Projection Matrix (internal use)
	FMatrix OverscanProjectionMatrix = FMatrix::Identity;

	// World scale
	float WorldToMeters = 100.f;

	//////////////////
	// Rendering data, for internal usage

	// GPU index for this context render target
	int GPUIndex = -1;

	bool bAllowGPUTransferOptimization = false;
	bool bEnabledGPUTransferLockSteps = true;

	// Location and size on a render target texture
	FIntRect RenderTargetRect;

	// Context size
	FIntPoint ContextSize;

	// Location and size on a frame target texture
	FIntRect FrameTargetRect;

	// Mips number for additional MipsShader resources
	int NumMips = 1;

	// Disable render for this viewport (Overlay)
	bool bDisableRender = false;
};
