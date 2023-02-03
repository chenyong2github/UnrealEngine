// Copyright Epic Games, Inc. All Rights Reserved.

// #include "ToolkitStyle.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

// @TODO: ~enable ToolkitStyle set 

class FWidgetRegistrationModule
	: public IModuleInterface
{
public:

	// IModuleInterface interface
	
	virtual void StartupModule() override
	{
	//	FToolkitStyle::Initialize();
	}
	
	virtual void ShutdownModule() override
	{
	//	FToolkitStyle::Shutdown();
	}

};


IMPLEMENT_MODULE(FWidgetRegistrationModule, WidgetRegistration);
