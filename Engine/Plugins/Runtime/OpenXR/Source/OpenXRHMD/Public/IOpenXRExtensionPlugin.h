// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"

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

	virtual const void* OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext)
	{
		return InNext;
	}

	// FOpenXRRenderBridge::Present, RHI thread
	virtual const void* OnEndFrame(XrSession InSession, XrTime DisplayTime, const void* InNext)
	{
		return InNext;
	}

	// FOpenXRInput::Tick, game thread, setting up for xrSyncActions.  This happens near the start of the game frame.
	virtual const void* OnSyncActions(XrSession InSession, const void* InNext)
	{
		return InNext;
	}

	// FOpenXRInput::Tick, game thread, after xrSyncActions
	virtual void PostSyncActions(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace)
	{
	}
};
