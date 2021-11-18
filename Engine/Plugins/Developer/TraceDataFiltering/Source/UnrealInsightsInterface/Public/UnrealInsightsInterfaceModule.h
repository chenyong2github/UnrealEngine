// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(UnrealInsightsInterface, Log, All);

/**
 * Implements the UnrealInsights module.
 * This module contains code for interfacing with the Unreal Insights standalone application.
 * The actual code for Unreal Insights can be found in "Engine\Source\Developer\TraceInsights\".
 */
class FUnrealInsightsInterfaceModule
	: public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<class FUnrealInsightsLauncher> Launcher;
	FDelegateHandle RegisterStartupCallbackHandle;
};
