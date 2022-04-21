// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "IOpenXRExtensionPlugin.h"

#include "OpenXRCore.h"


class VIRTUALSCOUTINGOPENXR_API FVirtualScoutingOpenXRExtension : public IOpenXRExtensionPlugin
{
public:
	FVirtualScoutingOpenXRExtension();
	virtual ~FVirtualScoutingOpenXRExtension();

	//~ Begin IOpenXRExtensionPlugin interface
	virtual FString GetDisplayName() override
	{
		return FString(TEXT("VirtualScouting"));
	}

	virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual void PostCreateInstance(XrInstance InInstance) override;
	virtual void AddActions(XrInstance InInstance, FCreateActionSetFunc CreateActionSet, FCreateActionFunc CreateAction) override;
	virtual void GetActiveActionSetsForSync(TArray<XrActiveActionSet>& OutActiveSets) override;
	//~ End IOpenXRExtensionPlugin interface

private:
	void OnVREditingModeEnter();
	void OnVREditingModeExit();

private:
	FDelegateHandle InitCompleteDelegate;

	XrDebugUtilsMessengerEXT Messenger = XR_NULL_HANDLE;

	XrInstance Instance = XR_NULL_HANDLE;
	XrActionSet ActionSet = XR_NULL_HANDLE;

	bool bIsVrEditingModeActive = false;

private:
	static XrBool32 XRAPI_CALL XrDebugUtilsMessengerCallback_Trampoline(
		XrDebugUtilsMessageSeverityFlagsEXT InMessageSeverity,
		XrDebugUtilsMessageTypeFlagsEXT InMessageTypes,
		const XrDebugUtilsMessengerCallbackDataEXT* InCallbackData,
		void* InUserData);
};

#endif // #if WITH_EDITOR
