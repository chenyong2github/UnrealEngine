// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#define DATASMITHCORETECHPARAMETRICSURFACEDATA_MODULE_NAME TEXT("DatasmithCoreTechParametricSurfaceData")

/**
 * This module exposes additional features for assets containing CoreTech data.
 */
class FDatasmithCoreTechParametricSurfaceDataModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;

	static FDatasmithCoreTechParametricSurfaceDataModule& Get();
	static bool IsAvailable();
};
