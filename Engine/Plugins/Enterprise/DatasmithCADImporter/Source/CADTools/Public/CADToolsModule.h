// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleInterface.h"

#define CADTOOLS_MODULE_NAME TEXT("CADTools")

class FCADToolsModule : public IModuleInterface
{
public:
	static FCADToolsModule& Get();
	static bool IsAvailable();

private:
	virtual void StartupModule() override;
};
