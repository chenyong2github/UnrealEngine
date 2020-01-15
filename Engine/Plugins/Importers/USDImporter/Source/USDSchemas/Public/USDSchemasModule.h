// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FUsdSchemaTranslatorRegistry;

class IUsdSchemasModule : public IModuleInterface
{
public:
	virtual FUsdSchemaTranslatorRegistry& GetTranslatorRegistry() = 0;
};
