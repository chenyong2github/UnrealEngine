// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleInterface.h"

#define CADINTERFACES_MODULE_NAME TEXT("WorkerDataManagement")

class FCADInterfacesModule : public IModuleInterface
{
public:
	static FCADInterfacesModule& Get();
	static bool IsAvailable();

private:
	virtual void StartupModule() override;
};
