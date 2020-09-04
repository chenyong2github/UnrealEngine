// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"


struct FDisplayClusterRenderViewContext;
class FDisplayClusterViewportArea;
class IDisplayClusterProjectionPolicy;
class FDisplayClusterRenderViewport;
class UWorld;


/**
 * nDisplay render device interface
 */
class IDisplayClusterRenderDevice : public IStereoRendering
{
public:
	virtual ~IDisplayClusterRenderDevice() = 0
	{ }

public:

	/**
	* Device initialization
	*
	* @return - true if success
	*/
	virtual bool Initialize()
	{ return true; }

	/**
	* Called on a scene start to allow a rendering device to initialize any world related content
	*/
	virtual void StartScene(UWorld* World)
	{ }

	/**
	* Called before scene Tick
	*/
	virtual void PreTick(float DeltaSeconds)
	{ }

	/**
	* Called before unload current level
	*/
	virtual void EndScene()
	{ }

	/**
	* Assigns camera to a specified viewport. If InViewportId is empty, all viewports will be assigned to a new camera. Empty camera ID means default active camera.
	*
	* @param InCameraId   - ID of a camera (see [camera] in the nDisplay config file
	* @param InViewportId - ID of a viewport to assign the camera (all viewports if empty)
	*/
	virtual void SetViewportCamera(const FString& InCameraId = FString(), const FString& InViewportId = FString()) = 0;

	/**
	* Start postprocess settings
	*
	* @param ViewportID - viewport to set PP settings
	* @param StartPostProcessingSettings - PP settings
	*
	*/
	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) = 0;

	/**
	* Override postprocess settings
	*
	* @param ViewportID - viewport to set PP settings
	* @param OverridePostProcessingSettings - PP settings
	*
	*/
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) = 0;

	/**
	* Final postprocess settings
	*
	* @param ViewportID - viewport to set PP settings
	* @param FinalPostProcessingSettings - PP settings
	*
	*/
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) = 0;

	/**
	* Returns location and size of a viewport
	*
	* @param InViewportID   - ID of a viewport
	* @param Rect           - a rectangle that describes location and size of the viewport
	*/
	virtual bool GetViewportRect(const FString& InViewportID, FIntRect& Rect) = 0;
	virtual bool GetViewportProjectionPolicy(const FString& InViewportID, TSharedPtr<IDisplayClusterProjectionPolicy>& OutProjectionPolicy) = 0;
	virtual bool GetViewportContext(const FString& InViewportID, int ViewIndex, FDisplayClusterRenderViewContext& OutViewContext) = 0;

	virtual bool SetBufferRatio(const FString& InViewportID, float  InBufferRatio) = 0;

	/**
	* Returns current buffer ratio
	*
	* @param InViewportID  - viewport ID to change buffer ratio
	* @param OutBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	virtual bool GetBufferRatio(const FString& InViewportID, float& OutBufferRatio) const = 0;

	/**
	* Returns current viewport info
	*
	* @param ViewIdx        - viewport index
	*
	* @return - ptr to viewport info
	*/
	virtual const FDisplayClusterRenderViewport* GetRenderViewport(int32 ViewIdx) const = 0;

};
