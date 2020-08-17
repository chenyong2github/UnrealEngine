// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FTypedElementInterfacesModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModuleChecked("TypedElementFramework");
	}
};

IMPLEMENT_MODULE(FTypedElementInterfacesModule, TypedElementInterfaces)
