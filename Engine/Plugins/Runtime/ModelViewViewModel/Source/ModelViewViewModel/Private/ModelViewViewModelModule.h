// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class IConsoleVariable;

/**
 *
 */
class FModelViewViewModelModule : public IModuleInterface
{
public:
	FModelViewViewModelModule() = default;

	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

private:
	void HandleDefaultExecutionModeChanged(IConsoleVariable* Variable);
};
