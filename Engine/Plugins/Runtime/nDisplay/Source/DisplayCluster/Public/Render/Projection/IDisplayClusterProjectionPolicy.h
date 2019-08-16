// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"

class FRHICommandListImmediate;


/**
 * nDisplay projection policy
 */
class IDisplayClusterProjectionPolicy
{
public:
	virtual ~IDisplayClusterProjectionPolicy() = 0
	{ }

public:
	/**
	* Called each time a new game level starts
	*
	* @param World - A new world that is being started
	*/
	virtual void StartScene(UWorld* World)
	{ }

	/**
	* Called when current level is going to be closed (i.e. before loading a new map)
	*/
	virtual void EndScene()
	{ }

	/**
	* Called once the viewport is added
	*
	* @param ViewportSize - Size of a new viewport
	* @param ViewsAmount  - Amount of views depending on a rendering device
	*
	* @return - True if success
	*/
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) = 0;

	/**
	* Called before remove the viewport
	*/
	virtual void HandleRemoveViewport()
	{ }

	/**
	* @param ViewIdx           - Index of view that is being processed for this viewport
	* @param InOutViewLocation - (in/out) View location with ViewOffset (i.e. left eye pre-computed location)
	* @param InOutViewRotation - (in/out) View rotation
	* @param ViewOffset        - Offset applied ot a camera location that gives us InOutViewLocation (i.e. right offset in world to compute right eye location)
	* @param WorldToMeters     - Current world scale (units (cm) in meter)
	* @param NCP               - Distance to the near clipping plane
	* @param FCP               - Distance to the far  clipping plane
	*
	* @return - True if success
	*/
	virtual bool CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) = 0;

	/**
	* @param ViewIdx      - Index of view that is being processed for this viewport
	* @param OutPrjMatrix - (out) projection matrix
	*
	* @return - True if success
	*/
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix) = 0;

	/**
	* Returns if a policy provides warp&blend feature
	*
	* @return - True if warp&blend operations are supported by the policy implementation
	*/
	virtual bool IsWarpBlendSupported()
	{ return false; }

	/**
	* Performs warp&blend. Called if IsWarpBlendSupported() returns true
	*
	* @param ViewIdx      - Index of view that is being processed for this viewport
	* @param RHICmdList   - RHI commands
	* @param SrcTexture   - Source texture
	* @param ViewportRect - Region of the SrcTexture to perform warp&blend operations
	*
	*/
	virtual void ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
	{ }
};
