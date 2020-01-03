// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#define DATASMITHCORETECHEXTENSION_MODULE_NAME TEXT("DatasmithCoreTechExtension")

/**
 * This module exposes additional features for assets containing CoreTech data.
 */
class FDatasmithCoreTechExtensionModule : public IModuleInterface
{
public:
    static FDatasmithCoreTechExtensionModule& Get();
    static bool IsAvailable();

private:
	virtual void StartupModule() override;
};
