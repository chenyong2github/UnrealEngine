// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IOpenXRExtensionPlugin.h"

class FSimpleController : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
	virtual void StartupModule() override;
	virtual bool GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics) override;
};
