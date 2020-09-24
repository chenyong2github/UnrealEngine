// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDUtilitiesModule.h"

#include "USDMemory.h"

#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"

class FUsdUtilitiesModule : public IUsdUtilitiesModule
{
	virtual void StartupModule() override
	{
		FMessageLogInitializationOptions InitOptions;
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked< FMessageLogModule >( TEXT("MessageLog") );
		MessageLogModule.RegisterLogListing( TEXT( "USD" ), NSLOCTEXT( "USDUtilitiesModule", "USDLogListing", "USD" ), InitOptions );
	}

	virtual void ShutdownModule() override
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked< FMessageLogModule >( TEXT("MessageLog") );
		MessageLogModule.UnregisterLogListing( TEXT( "USD" ) );
	}
};

IMPLEMENT_MODULE_USD( FUsdUtilitiesModule, USDUtilities );
