// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameEnums.h"

class IDisplayClusterPresentation;
struct FDisplayClusterRenderViewContext;
class FDisplayClusterRenderFrame;
class IDisplayClusterProjectionPolicy;
class UWorld;
class FViewport;


/**
 * nDisplay render device interface
 */
class IDisplayClusterRenderDevice : public IStereoRendering
{
public:
	virtual ~IDisplayClusterRenderDevice() = default;

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

	// update settings from root actor config data, and build new frame structure
	virtual bool BeginNewFrame(FViewport* InViewport, UWorld* InWorld, FDisplayClusterRenderFrame& OutRenderFrame) = 0;
	virtual void FinalizeNewFrame() = 0;

	/**
	* Assigns camera to a specified viewport. If InViewportId is empty, all viewports will be assigned to a new camera. Empty camera ID means default active camera.
	*
	* @param CameraId   - ID of a camera (see [camera] in the nDisplay config file
	* @param ViewportId - ID of a viewport to assign the camera (all viewports if empty)
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual void SetViewportCamera(const FString& CameraId = FString(), const FString& ViewportId = FString()) 
	{}

	/**
	* Start postprocess settings
	*
	* @param ViewportId - viewport to set PP settings
	* @param StartPostProcessingSettings - PP settings
	*
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual void SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings)
	{}

	/**
	* Override postprocess settings
	*
	* @param ViewportId - viewport to set PP settings
	* @param OverridePostProcessingSettings - PP settings
	*
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f)
	{}

	/**
	* Final postprocess settings
	*
	* @param ViewportId - viewport to set PP settings
	* @param FinalPostProcessingSettings - PP settings
	*
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings)
	{}

	/**
	* Returns location and size of a viewport
	*
	* @param ViewportId - ID of a viewport
	* @param OutRect    - a rectangle that describes location and size of the viewport
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool GetViewportRect(const FString& ViewportId, FIntRect& OutRect)
	{
		return false;
	}

	/**
	* Returns projection policy object of a specified viewport
	*
	* @param ViewportId          - ID of a viewport
	* @param OutProjectionPolicy - projection policy instance
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool GetViewportProjectionPolicy(const FString& ViewportId, TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& OutProjectionPolicy)
	{
		return false;
	}

	/**
	* Returns context of a specified viewport
	*
	* @param ViewportId          - ID of a viewport
	* @param ViewIndex           - View index (left/center/right eye)
	* @param OutProjectionPolicy - projection policy instance
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool GetViewportContext(const FString& ViewportId, int ViewIndex, FDisplayClusterRenderViewContext& OutViewContext)
	{
		return false;
	}

	/**
	* Sets buffer ratio
	*
	* @param ViewportId    - viewport ID to change buffer ratio
	* @param InBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool SetBufferRatio(const FString& ViewportId, float InBufferRatio)
	{
		return false;
	};
	
	
	/**
	* Returns current buffer ratio
	*
	* @param ViewportId     - Viewport ID to get buffer ratio
	* @param OutBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool GetBufferRatio(const FString& ViewportId, float& OutBufferRatio) const
	{
		return false;
	}

	/**
	* Sets buffer ratio
	*
	* @param ViewportIdx   - viewport index to change buffer ratio
	* @param InBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool SetBufferRatio(int32 ViewportIdx, float InBufferRatio)
	{
		return false;
	}

	/**
	* Returns current buffer ratio
	*
	* @param ViewportIdx    - Viewport index to get buffer ratio
	* @param OutBufferRatio - buffer ratio
	*
	* @return - true if succeeded
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual bool GetBufferRatio(int32 ViewportIdx, float& OutBufferRatio) const
	{
		return false;
	}

	/**
	* Returns specified viewport info
	*
	* @param ViewportId - Viewport ID to get info
	*
	* @return - Viewport info
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual const class FDisplayClusterRenderViewport* GetRenderViewport(const FString& ViewportId) const
	{
		return nullptr;
	}

	/**
	* Returns specified viewport info
	*
	* @param ViewportIdx - Viewport index to get info
	*
	* @return - Viewport info
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual const class FDisplayClusterRenderViewport* GetRenderViewport(int32 ViewportIdx) const
	{
		return nullptr;
	}

	/**
	* Returns all available viewports with filter applied
	*
	* @param Filter - viewport filter
	*
	* @return - Viewport info
	*/
	UE_DEPRECATED(4.27, "This function has been moved to FDisplayClusterViewport. Use GetViewportManager() to access  that interface.")
	virtual const void GetRenderViewports(TArray<class FDisplayClusterRenderViewport*>& OutViewports) const
	{}

	/**
	* Callback triggered when custom present handler was created
	*/
	DECLARE_EVENT(IDisplayClusterRenderDevice, FDisplayClusterRenderCustomPresentCreated);
	virtual FDisplayClusterRenderCustomPresentCreated& OnDisplayClusterRenderCustomPresentCreated() = 0;

	/**
	* Returns current presentation handler
	*
	* @return - nullptr if failed
	*/
	virtual IDisplayClusterPresentation* GetPresentation() const = 0;

};
