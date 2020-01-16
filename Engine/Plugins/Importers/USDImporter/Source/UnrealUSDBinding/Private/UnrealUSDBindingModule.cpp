// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealUSDBindingModule.h"

#include "USDMemory.h" // For IMPLEMENT_MODULE_USD
#include "Modules/ModuleManager.h"

class FUnrealUSDBindingModule : public IUnrealUSDBindingModule
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE_USD( FUnrealUSDBindingModule, UnrealUSDBinding );
