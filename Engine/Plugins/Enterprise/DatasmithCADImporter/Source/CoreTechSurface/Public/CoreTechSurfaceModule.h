// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#define CORETECHSURFACE_MODULE_NAME TEXT("CoreTechSurface")

/**
 * This module exposes additional features for assets containing CoreTech data.
 */
class FCoreTechSurfaceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;

	static FCoreTechSurfaceModule& Get();
	static bool IsAvailable();
};
