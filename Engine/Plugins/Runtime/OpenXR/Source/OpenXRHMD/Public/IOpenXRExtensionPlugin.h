// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "ARTypes.h"
#include "ARTextures.h"
#include "ARTraceResult.h"
#include "DefaultSpectatorScreenController.h"

#include <openxr/openxr.h>

class IOpenXRCustomAnchorSupport
{
public:
	/**
	 * Method to add an anchor on tracking space
	 */
	virtual bool OnPinComponent(class UARPin* Pin, XrSession InSession, XrSpace TrackingSpace, XrTime DisplayTime, float worldToMeterScale) = 0;

	/**
	 * Method to remove an anchor from tracking space
	 */
	virtual void OnRemovePin(class UARPin* Pin) = 0;

	virtual void OnUpdatePin(class UARPin* Pin, XrSession InSession, XrSpace TrackingSpace, XrTime DisplayTime, float worldToMeterScale) = 0;

	// ARPin Local Store support.
	// Some Platforms/Devices have the ability to persist AR Anchors (real world positions) to the device or user account.
	// They are saved and loaded with a string identifier.

	virtual bool IsLocalPinSaveSupported() const
	{
		return false;
	}

	virtual bool ArePinsReadyToLoad()
	{
		return false;
	}

	virtual void LoadARPins(XrSession InSession, TFunction<UARPin*(FName)> OnCreatePin)
	{
	}

	virtual bool SaveARPin(XrSession InSession, FName InName, UARPin* InPin)
	{
		return false;
	}

	virtual void RemoveSavedARPin(XrSession InSession, FName InName)
	{
	}

	virtual void RemoveAllSavedARPins(XrSession InSession)
	{
	}
};

class IOpenXRCustomCaptureSupport
{
public:

	virtual bool OnGetCameraIntrinsics(struct FARCameraIntrinsics& OutCameraIntrinsics) const 
	{ 
		return false; 
	}

	/** @return the AR texture for the specified type */
	virtual class UARTexture* OnGetARTexture(EARTextureType TextureType) const
	{ 
		return nullptr; 
	}

	virtual bool OnToggleARCapture(const bool bOnOff) 
	{ 
		return false; 
	}

	virtual FTransform GetCameraTransform() const
	{ 
		return FTransform::Identity; 
	}

	virtual FVector GetWorldSpaceRayFromCameraPoint(FVector2D pixelCoordinate) const
	{
		return FVector::ZeroVector;
	}

	virtual bool IsEnabled() const
	{
		return false;
	}

	virtual TArray<FARTraceResult> OnLineTraceTrackedObjects(const TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARCompositionComponent, const FVector Start, const FVector End, const EARLineTraceChannels TraceChannels)
	{
		return {};
	}
};


class IOpenXRExtensionPlugin : public IModularFeature
{
public:
	virtual ~IOpenXRExtensionPlugin(){}

	static FName GetModularFeatureName()
	{
		static FName OpenXRFeatureName = FName(TEXT("OpenXRExtension"));
		return OpenXRFeatureName;
	}

	/**
	* Register module as an extension on startup.  
	* It is common to do this in StartupModule of your IModuleInterface class (which may also be the class that implements this interface).
	* The module's LoadingPhase must be PostInitConfig or earlier because OpenXRHMD will look for these after it is loaded in that phase.
	*/
	virtual void RegisterOpenXRExtensionModularFeature()
	{
		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	}

	/**
	* Optionally provide a custom loader for the OpenXR plugin.
	*/
	virtual bool GetCustomLoader(PFN_xrGetInstanceProcAddr* OutGetProcAddr)
	{
		return false;
	}

	/**
	* Fill the array with extensions required by the plugin
	* If false is returned the plugin and its extensions will be ignored
	*/
	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
	{
		return true;
	}

	/**
	* Fill the array with extensions optionally supported by the plugin
	* If false is returned the plugin and its extensions will be ignored
	*/
	virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions)
	{
		return true;
	}

	/**
	* Set the output parameters to add an interaction profile to OpenXR Input
	*/
	virtual bool GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics)
	{
		return false;
	}

	/**
	* Set a spectator screen controller specific to the platform
	* If true is returned and OutSpectatorScreenController is nullptr, spectator screen will be disabled
	* If false is returned a default spectator screen controller will be created
	*/
	virtual bool GetSpectatorScreenController(FHeadMountedDisplayBase* InHMDBase, TUniquePtr<FDefaultSpectatorScreenController>& OutSpectatorScreenController)
	{
		return false;
	}

	/**
	* Add any actions provided by the plugin to Actions with suggested bindings.
	* This allows a plugin to 'hard code' an action so that the plugin can use it.
	*/
	virtual void AddActions(XrInstance Instance, TFunction<XrAction(XrActionType InActionType, const FName& InName, const TArray<XrPath>& InSubactionPaths)> AddAction)
	{
	}

	/**
	* Use this callback to handle events that the OpenXR plugin doesn't handle itself
	*/
	virtual void OnEvent(XrSession InSession, const XrEventDataBaseHeader* InHeader)
	{
	}

	/** Get custom anchor interface if provided by this extension. */
	virtual IOpenXRCustomAnchorSupport* GetCustomAnchorSupport() { return nullptr; }

	/** Get custom capture interface if provided by this extension. */
	virtual IOpenXRCustomCaptureSupport* GetCustomCaptureSupport(const EARCaptureType CaptureType) { return nullptr; }
	/**
	* Callback to provide extra view configurations that should be rendered in the main render pass
	*/
	virtual void GetViewConfigurations(XrSystemId InSystem, TArray<XrViewConfigurationView>& OutViews)
	{
	}

	/**
	* Callback to provide the pose and fov of each view that was provided in GetViewConfigurations
	*/
	virtual void GetViewLocations(XrSession InSession, XrTime InDisplayTime, XrSpace InViewSpace, TArray<XrView>& OutViews)
	{
	}

	/**
	* Callbacks with returned pointer added to next chain, do *not* return pointers to structs on the stack.
	* Remember to assign InNext to the next pointer of your struct or otherwise you may break the next chain.
	*/

	virtual const void* OnCreateInstance(class IOpenXRHMDPlugin* InPlugin, const void* InNext)
	{
		return InNext;
	}

	virtual const void* OnGetSystem(XrInstance InInstance, const void* InNext)
	{
		return InNext;
	}

	virtual void PostGetSystem(XrInstance InInstance, XrSystemId InSystem)
	{
	}

	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
	{
		return InNext;
	}

	virtual const void* OnBeginSession(XrSession InSession, const void* InNext)
	{
		return InNext;
	}

	// OpenXRHMD::OnBeginRendering_GameThread
	virtual void* OnWaitFrame(XrSession InSession, void* InNext)
	{
		return InNext;
	}

	// OpenXRHMD::OnBeginRendering_RenderThread
	virtual const void* OnBeginFrame(XrSession InSession, XrTime DisplayTime, const void* InNext)
	{
		return InNext;
	}

	virtual const void* OnBeginProjectionView(XrSession InSession, int32 InLayerIndex, int32 InViewIndex, const void* InNext)
	{
		return InNext;
	}

	virtual const void* OnBeginDepthInfo(XrSession InSession, int32 InLayerIndex, int32 InViewIndex, const void* InNext)
	{
		return InNext;
	}

	virtual const void* OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext, XrCompositionLayerFlags& OutFlags)
	{
		return InNext;
	}

	// FOpenXRRenderBridge::Present, RHI thread
	virtual const void* OnEndFrame(XrSession InSession, XrTime DisplayTime, const TArray<XrSwapchainSubImage> InColorImages, const TArray<XrSwapchainSubImage> InDepthImages, const void* InNext)
	{
		return InNext;
	}

	// FOpenXRInput::Tick, game thread, setting up for xrSyncActions.  This happens near the start of the game frame.
	virtual const void* OnSyncActions(XrSession InSession, const void* InNext)
	{
		return InNext;
	}

	// OpenXRHMD::OnStartGameFrame
	virtual void UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace)
	{
	}

	// FOpenXRInput::Tick, game thread, after xrSyncActions
	virtual void PostSyncActions(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace)
	{
	}


	/**
	 * Start the AR system.
	 *
	 * @param SessionType The type of AR session to create
	 *
	 * @return true if the system was successfully started
	 */
	virtual void OnStartARSession(class UARSessionConfig* SessionConfig) {}

	/** Stop the AR system but leave its internal state intact. */
	virtual void OnPauseARSession() {}

	/** Stop the AR system and reset its internal state; this task must succeed. */
	virtual void OnStopARSession() {}

};
