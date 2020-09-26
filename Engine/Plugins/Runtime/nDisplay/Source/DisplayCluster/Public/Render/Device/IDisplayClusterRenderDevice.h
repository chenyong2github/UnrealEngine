// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"


struct FDisplayClusterRenderViewContext;
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
	* @param CameraId   - ID of a camera (see [camera] in the nDisplay config file
	* @param ViewportId - ID of a viewport to assign the camera (all viewports if empty)
	*/
	virtual void SetViewportCamera(const FString& CameraId = FString(), const FString& ViewportId = FString()) = 0;

	/**
	* Start postprocess settings
	*
	* @param ViewportId - viewport to set PP settings
	* @param StartPostProcessingSettings - PP settings
	*
	*/
	virtual void SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings) = 0;

	/**
	* Override postprocess settings
	*
	* @param ViewportId - viewport to set PP settings
	* @param OverridePostProcessingSettings - PP settings
	*
	*/
	virtual void SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) = 0;

	/**
	* Final postprocess settings
	*
	* @param ViewportId - viewport to set PP settings
	* @param FinalPostProcessingSettings - PP settings
	*
	*/
	virtual void SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings) = 0;

	/**
	* Returns location and size of a viewport
	*
	* @param ViewportId - ID of a viewport
	* @param OutRect    - a rectangle that describes location and size of the viewport
	*/
	virtual bool GetViewportRect(const FString& ViewportId, FIntRect& OutRect) = 0;

	/**
	* Returns projection policy object of a specified viewport
	*
	* @param ViewportId          - ID of a viewport
	* @param OutProjectionPolicy - projection policy instance
	*/
	virtual bool GetViewportProjectionPolicy(const FString& ViewportId, TSharedPtr<IDisplayClusterProjectionPolicy>& OutProjectionPolicy) = 0;

	/**
	* Returns context of a specified viewport
	*
	* @param ViewportId          - ID of a viewport
	* @param ViewIndex           - View index (left/center/right eye)
	* @param OutProjectionPolicy - projection policy instance
	*/
	virtual bool GetViewportContext(const FString& ViewportId, int ViewIndex, FDisplayClusterRenderViewContext& OutViewContext) = 0;

	/**
	* Sets buffer ratio
	*
	* @param ViewportId    - viewport ID to change buffer ratio
	* @param InBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	virtual bool SetBufferRatio(const FString& ViewportId, float InBufferRatio) = 0;

	/**
	* Returns current buffer ratio
	*
	* @param ViewportId     - Viewport ID to get buffer ratio
	* @param OutBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	virtual bool GetBufferRatio(const FString& ViewportId, float& OutBufferRatio) const = 0;

	/**
	* Sets buffer ratio
	*
	* @param ViewportIdx   - viewport index to change buffer ratio
	* @param InBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	virtual bool SetBufferRatio(int32 ViewportIdx, float InBufferRatio) = 0;

	/**
	* Returns current buffer ratio
	*
	* @param ViewportIdx    - Viewport index to get buffer ratio
	* @param OutBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	virtual bool GetBufferRatio(int32 ViewportIdx, float& OutBufferRatio) const = 0;

	/**
	* Returns specified viewport info
	*
	* @param ViewportId - Viewport ID to get info
	*
	* @return - Viewport info
	*/
	virtual const FDisplayClusterRenderViewport* GetRenderViewport(const FString& ViewportId) const = 0;

	/**
	* Returns specified viewport info
	*
	* @param ViewportIdx - Viewport index to get info
	*
	* @return - Viewport info
	*/
	virtual const FDisplayClusterRenderViewport* GetRenderViewport(int32 ViewportIdx) const = 0;

	/**
	* Returns all available viewports with filter applied
	*
	* @param Filter - viewport filter
	*
	* @return - Viewport info
	*/
	virtual const void GetRenderViewports(TArray<FDisplayClusterRenderViewport>& OutViewports) const = 0;

	/**
	* Returns the number of Views per Viewport.
	*
	* @return - Number of Views per Viewport.
	*/
	virtual uint32 GetViewsAmountPerViewport() const = 0;
};
