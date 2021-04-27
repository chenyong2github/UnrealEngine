// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#define PARAMETRICSURFACEEXTENSION_MODULE_NAME TEXT("ParametricSurfaceExtension")

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
