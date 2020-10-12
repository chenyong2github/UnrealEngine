// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/IDisplayClusterRenderDevice.h"

class IDisplayClusterRenderDevice;
class IDisplayClusterPostProcess;
class IDisplayClusterProjectionPolicy;
class IDisplayClusterProjectionPolicyFactory;
class IDisplayClusterRenderDeviceFactory;
class IDisplayClusterRenderSyncPolicy;
class IDisplayClusterRenderSyncPolicyFactory;
class UCineCameraComponent;
struct FPostProcessSettings;

/**
 * Public render manager interface
 */
class IDisplayClusterRenderManager
{
public:
	virtual ~IDisplayClusterRenderManager() = 0
	{ }

public:
	// Post-process operation wrapper
	struct FDisplayClusterPPInfo
	{
		FDisplayClusterPPInfo(TSharedPtr<IDisplayClusterPostProcess>& InOperation, int InPriority)
			: Operation(InOperation)
			, Priority(InPriority)
		{ }

		TSharedPtr<IDisplayClusterPostProcess> Operation;
		int Priority;
	};

public:
	/**
	* Returns current rendering device
	*
	* @return - nullptr if failed
	*/
	virtual IDisplayClusterRenderDevice* GetRenderDevice() const = 0;

	/**
	* Registers rendering device factory
	*
	* @param DeviceType - Type of rendering device
	* @param Factory    - Factory instance
	*
	* @return - True if success
	*/
	virtual bool RegisterRenderDeviceFactory(const FString& DeviceType, TSharedPtr<IDisplayClusterRenderDeviceFactory>& Factory) = 0;

	/**
	* Unregisters rendering device factory
	*
	* @param DeviceType - Type of rendering device
	*
	* @return - True if success
	*/
	virtual bool UnregisterRenderDeviceFactory(const FString& DeviceType) = 0;

	/**
	* Registers synchronization policy factory
	*
	* @param SyncPolicyType - Type of synchronization policy
	* @param Factory        - Factory instance
	*
	* @return - True if success
	*/
	virtual bool RegisterSynchronizationPolicyFactory(const FString& SyncPolicyType, TSharedPtr<IDisplayClusterRenderSyncPolicyFactory>& Factory) = 0;

	/**
	* Unregisters synchronization policy factory
	*
	* @param SyncPolicyType - Type of synchronization policy
	*
	* @return - True if success
	*/
	virtual bool UnregisterSynchronizationPolicyFactory(const FString& SyncPolicyType) = 0;

	/**
	* Returns currently active rendering synchronization policy object
	*
	* @return - Rendering synchronization policy object
	*/
	virtual TSharedPtr<IDisplayClusterRenderSyncPolicy> GetCurrentSynchronizationPolicy() = 0;

	/**
	* Registers projection policy factory
	*
	* @param ProjectionType - Type of projection data (MPCDI etc.)
	* @param Factory        - Factory instance
	*
	* @return - True if success
	*/
	virtual bool RegisterProjectionPolicyFactory(const FString& ProjectionType, TSharedPtr<IDisplayClusterProjectionPolicyFactory>& Factory) = 0;

	/**
	* Unregisters projection policy factory
	*
	* @param ProjectionType - Type of synchronization policy
	*
	* @return - True if success
	*/
	virtual bool UnregisterProjectionPolicyFactory(const FString& ProjectionType) = 0;

	/**
	* Returns a projection policy factory of specified type (if it has been registered previously)
	*
	* @param ProjectionType - Projection policy type
	*
	* @return - Projection policy factory pointer or nullptr if not registered
	*/
	virtual TSharedPtr<IDisplayClusterProjectionPolicyFactory> GetProjectionPolicyFactory(const FString& ProjectionType) = 0;

	/**
	* Returns all registered projection policy types
	*
	* @param OutPolicyIDs - (out) array to put registered IDs
	*/
	virtual void GetRegisteredProjectionPolicies(TArray<FString>& OutPolicyIDs) const = 0;

	/**
	* Registers a post process operation
	*
	* @param Name      - A unique PP operation name
	* @param Operation - PP operation implementation
	* @param Priority  - PP order in chain (the calling order is from the smallest to the largest: -N...0...N)
	*
	* @return - True if success
	*/
	virtual bool RegisterPostprocessOperation(const FString& Name, TSharedPtr<IDisplayClusterPostProcess>& Operation, int Priority = 0) = 0;

	/**
	* Registers a post process operation
	*
	* @param Name - A unique PP operation name
	* @param PPInfo - PP info wrapper (see IDisplayClusterRenderManager::FDisplayClusterPPInfo)
	*
	* @return - True if success
	*/
	virtual bool RegisterPostprocessOperation(const FString& Name, FDisplayClusterPPInfo& PPInfo) = 0;

	/**
	* Unregisters a post process operation
	*
	* @param Name - PP operation name
	*
	* @return - True if success
	*/
	virtual bool UnregisterPostprocessOperation(const FString& Name) = 0;

	/**
	* Returns all registered post-process operations
	*
	* @return - PP operations
	*/
	virtual TMap<FString, FDisplayClusterPPInfo> GetRegisteredPostprocessOperations() const = 0;


	/////////////////////////////////////////////////////////////////////////////////////////
	// DEPRECATED
	/////////////////////////////////////////////////////////////////////////////////////////

	/**
	* Start postprocess settings
	*
	* @param ViewportID - viewport to set PP settings
	* @param StartPostProcessingSettings - PP settings
	*
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings)
	{ }

	/**
	* Override postprocess settings
	*
	* @param ViewportID - viewport to set PP settings
	* @param OverridePostProcessingSettings - PP settings
	*
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f)
	{ }

	/**
	* Final postprocess settings
	*
	* @param ViewportID - viewport to set PP settings
	* @param FinalPostProcessingSettings - PP settings
	*
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings)
	{ }

	/**
	* Assigns camera to a specified viewport. If InViewportId is empty, all viewports will be assigned to a new camera. Empty camera ID means default active camera.
	*
	* @param CameraID   - ID of a camera (see [camera] in the nDisplay config file
	* @param ViewportID - ID of a viewport to assign the camera (all viewports if empty)
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual void SetViewportCamera(const FString& CameraID = FString(), const FString& ViewportID = FString())
	{ }

	/**
	* Returns location and size of a viewport
	*
	* @param ViewportID - ID of a viewport
	* @param Rect       - a rectangle that describes location and size of the viewport
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual bool GetViewportRect(const FString& ViewportID, FIntRect& Rect) const
	{
		return false;
	}

	/**
	* Returns projection policy interface of a viewport
	*
	* @param ViewportID          - ID of a viewport
	* @param OutProjectionPolicy - Projection policy interface
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual bool GetViewportProjectionPolicy(const FString& ViewportID, TSharedPtr<IDisplayClusterProjectionPolicy>& OutProjectionPolicy)
	{
		return false;
	}


	/**
	* Scales buffer of a viewport
	*
	* @param ViewportID   - ID of a viewport which buffer we're going to scale
	* @param BufferRatio  - Buffer ratio (1 - same as viewport size, 0.25 - is 0.25 width and height of viewport size)
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual bool SetBufferRatio(const FString& ViewportID, float BufferRatio)
	{
		return false;
	}

	/**
	* Returns current buffer ratio of a viewport
	*
	* @param ViewportID     - ID of a viewport
	* @param OutBufferRatio - Current buffer ratio (1 - same as viewport size, 0.25 - is 0.25 width and height of viewport size)
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual bool GetBufferRatio(const FString& ViewportID, float& OutBufferRatio) const
	{
		return false;
	}

	/**
	* Returns context data of a viewport
	*
	* @param ViewportID     - ID of a viewport
	* @param ViewIndex      - eye view index
	* @param OutViewContext - Context data
	*/
	UE_DEPRECATED(4.26, "This function has been moved to IDisplayClusterRenderDevice. Use GetRenderDevice to access that interface.")
	virtual bool GetViewportContext(const FString& ViewportID, int ViewIndex, FDisplayClusterRenderViewContext& OutViewContext)
	{
		return false;
	}

	/**
	* Configuration of interpupillary (interocular) distance
	*
	* @param CameraID    - Camera ID to modify
	* @param EyeDistance - distance between eyes (UE4 units).
	*/
	UE_DEPRECATED(4.26, "This function has been moved to UDisplayClusterCameraComponent")
	virtual void  SetInterpupillaryDistance(const FString& CameraID, float EyeDistance)
	{ }

	/**
	* Returns currently used interpupillary distance.
	*
	* @param CameraID    - Camera ID to get data
	*
	* @return - distance between eyes (UE4 units)
	*/
	UE_DEPRECATED(4.26, "This function has been moved to UDisplayClusterCameraComponent")
	virtual float GetInterpupillaryDistance(const FString& CameraID) const
	{
		return 0.f;
	}

	/**
	* Configure eyes swap state
	*
	* @param CameraID - Camera ID to modify
	* @param SwapEyes - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	UE_DEPRECATED(4.26, "This function has been moved to UDisplayClusterCameraComponent")
	virtual void SetSwapEyes(const FString& CameraID, bool SwapEyes)
	{ }

	/**
	* Returns currently used eyes swap
	*
	* @param CameraID    - Camera ID to get data
	*
	* @return - eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	UE_DEPRECATED(4.26, "This function has been moved to UDisplayClusterCameraComponent")
	virtual bool GetSwapEyes(const FString& CameraID) const
	{
		return false;
	}

	/**
	* Toggles eyes swap state
	*
	* @param CameraID - Camera ID to modify
	*
	* @return - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	UE_DEPRECATED(4.26, "This function has been moved to UDisplayClusterCameraComponent")
	virtual bool ToggleSwapEyes(const FString& CameraID)
	{
		return false;
	}
};
