// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/IDisplayClusterRenderDevice.h"

class IDisplayClusterPostProcess;
class IDisplayClusterProjectionPolicy;
class IDisplayClusterProjectionPolicyFactory;
class IDisplayClusterRenderDeviceFactory;
class IDisplayClusterRenderSyncPolicy;
class IDisplayClusterRenderSyncPolicyFactory;


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
	* Registers rendering device factory
	*
	* @param InDeviceType - Type of rendering device
	* @param InFactory    - Factory instance
	*
	* @return - True if success
	*/
	virtual bool RegisterRenderDeviceFactory(const FString& InDeviceType, TSharedPtr<IDisplayClusterRenderDeviceFactory>& InFactory) = 0;

	/**
	* Unregisters rendering device factory
	*
	* @param InDeviceType - Type of rendering device
	*
	* @return - True if success
	*/
	virtual bool UnregisterRenderDeviceFactory(const FString& InDeviceType) = 0;

	/**
	* Registers synchronization policy factory
	*
	* @param InSyncPolicyType - Type of synchronization policy
	* @param InFactory        - Factory instance
	*
	* @return - True if success
	*/
	virtual bool RegisterSynchronizationPolicyFactory(const FString& InSyncPolicyType, TSharedPtr<IDisplayClusterRenderSyncPolicyFactory>& InFactory) = 0;

	/**
	* Unregisters synchronization policy factory
	*
	* @param InSyncPolicyType - Type of synchronization policy
	*
	* @return - True if success
	*/
	virtual bool UnregisterSynchronizationPolicyFactory(const FString& InSyncPolicyType) = 0;

	/**
	* Returns currently active rendering synchronization policy object
	*
	* @return - Rendering synchronization policy object
	*/
	virtual TSharedPtr<IDisplayClusterRenderSyncPolicy> GetCurrentSynchronizationPolicy() = 0;

	/**
	* Registers projection policy factory
	*
	* @param InProjectionType - Type of projection data (MPCDI etc.)
	* @param InFactory        - Factory instance
	*
	* @return - True if success
	*/
	virtual bool RegisterProjectionPolicyFactory(const FString& InProjectionType, TSharedPtr<IDisplayClusterProjectionPolicyFactory>& InFactory) = 0;

	/**
	* Unregisters projection policy factory
	*
	* @param ProjectionType - Type of synchronization policy
	*
	* @return - True if success
	*/
	virtual bool UnregisterProjectionPolicyFactory(const FString& InProjectionType) = 0;

	/**
	* Returns a projection policy factory of specified type (if it has been registered previously)
	*
	* @param ProjectionType - Projection policy type
	*
	* @return - Projection policy factory pointer or nullptr if not registered
	*/
	virtual TSharedPtr<IDisplayClusterProjectionPolicyFactory> GetProjectionPolicyFactory(const FString& InProjectionType) = 0;

	/**
	* Registers a post process operation
	*
	* @param InName      - A unique PP operation name
	* @param InOperation - PP operation implementation
	* @param InPriority  - PP order in chain (the calling order is from the smallest to the largest: -N...0...N)
	*
	* @return - True if success
	*/
	virtual bool RegisterPostprocessOperation(const FString& InName, TSharedPtr<IDisplayClusterPostProcess>& InOperation, int InPriority = 0) = 0;

	/**
	* Registers a post process operation
	*
	* @param InName - A unique PP operation name
	* @param PPInfo - PP info wrapper (see IDisplayClusterRenderManager::FDisplayClusterPPInfo)
	*
	* @return - True if success
	*/
	virtual bool RegisterPostprocessOperation(const FString& InName, FDisplayClusterPPInfo& InPPInfo) = 0;

	/**
	* Unregisters a post process operation
	*
	* @param InName - PP operation name
	*
	* @return - True if success
	*/
	virtual bool UnregisterPostprocessOperation(const FString& InName) = 0;

	/**
	* Returns all registered post-process operations
	*
	* @return - PP operations
	*/
	virtual TMap<FString, FDisplayClusterPPInfo> GetRegisteredPostprocessOperations() const = 0;

	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) = 0;
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) = 0;
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) = 0;

	/**
	* Assigns camera to a specified viewport. If InViewportId is empty, all viewports will be assigned to a new camera. Empty camera ID means default active camera.
	*
	* @param InCameraId   - ID of a camera (see [camera] in the nDisplay config file
	* @param InViewportId - ID of a viewport to assign the camera (all viewports if empty)
	*/
	virtual void SetViewportCamera(const FString& InCameraId = FString(), const FString& InViewportId = FString()) = 0;

	/**
	* Returns location and size of a viewport
	*
	* @param InViewportID   - ID of a viewport
	* @param Rect           - a rectangle that describes location and size of the viewport
	*/
	virtual bool GetViewportRect(const FString& InViewportID, FIntRect& Rect) = 0;
	
	/**
	* Scales buffer of a viewport
	*
	* @param InViewportID   - ID of a viewport which buffer we're going to scale
	* @param InBufferRatio  - Buffer ratio (1 - same as viewport size, 0.25 - is 0.25 width and height of viewport size)
	*/
	virtual bool SetBufferRatio(const FString& InViewportID, float InBufferRatio) = 0;

	/**
	* Returns current buffer ratio of a viewport
	*
	* @param InViewportID   - ID of a viewport
	* @param OutBufferRatio - Current buffer ratio (1 - same as viewport size, 0.25 - is 0.25 width and height of viewport size)
	*/
	virtual bool GetBufferRatio(const FString& InViewportID, float &OutBufferRatio) const = 0;

	/**
	* Configuration of interpupillary (interocular) distance
	*
	* @param dist - distance between eyes (UE4 units).
	*/
	virtual void  SetInterpupillaryDistance(const FString& CameraId, float EyeDistance) = 0;

	/**
	* Returns currently used interpupillary distance.
	*
	* @return - distance between eyes (UE4 units)
	*/
	virtual float GetInterpupillaryDistance(const FString& CameraId) const = 0;

	/**
	* Configure eyes swap state
	*
	* @param swap - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	virtual void SetEyesSwap(const FString& CameraId, bool EyeSwapped) = 0;

	/**
	* Returns currently used eyes swap
	*
	* @return - eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	virtual bool GetEyesSwap(const FString& CameraId) const = 0;

	/**
	* Toggles eyes swap state
	*
	* @return - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	virtual bool ToggleEyesSwap(const FString& CameraId) = 0;
};
