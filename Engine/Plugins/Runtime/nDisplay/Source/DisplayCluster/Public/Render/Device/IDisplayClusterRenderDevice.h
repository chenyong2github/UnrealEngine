// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"


class FDisplayClusterViewportArea;
class IDisplayClusterProjectionPolicy;
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

	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) = 0;
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) = 0;
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) = 0;

	virtual bool GetViewportRect(const FString& InViewportID, FIntRect& Rect) = 0;

	virtual bool SetBufferRatio(const FString& InViewportID, float  InBufferRatio) = 0;
	virtual bool GetBufferRatio(const FString& InViewportID, float& OutBufferRatio) const = 0;
	virtual bool GetBufferRatio(int32 ViewIdx, float& OutBufferRatio) const = 0;
};
