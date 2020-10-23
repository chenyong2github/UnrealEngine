// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#define CORETECHEXTENSION_MODULE_NAME TEXT("CoreTechExtension")

/**
 * This module exposes additional features for assets containing CoreTech data.
 */
class FCoreTechExtensionModule : public IModuleInterface
{
public:
    static FCoreTechExtensionModule& Get();
    static bool IsAvailable();

private:
	virtual void StartupModule() override;
};
