// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOpenXRExtensionPlugin.h"
#include "Modules/ModuleInterface.h"

class FHPMotionControllerModule :
	public IModuleInterface,
	public IOpenXRExtensionPlugin
{
public:
	FHPMotionControllerModule() { }

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	bool GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics) override;
};

