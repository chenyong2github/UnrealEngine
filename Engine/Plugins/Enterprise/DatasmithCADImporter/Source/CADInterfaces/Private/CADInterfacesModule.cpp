// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CADInterfacesModule.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "CADInterfacesModule"

FCADInterfacesModule& FCADInterfacesModule::Get()
{
	return FModuleManager::LoadModuleChecked< FCADInterfacesModule >(CADINTERFACES_MODULE_NAME);
}

bool FCADInterfacesModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(CADINTERFACES_MODULE_NAME);
}

void FCADInterfacesModule::StartupModule()
{
}

IMPLEMENT_MODULE(FCADInterfacesModule, CADInterfaces);

#undef LOCTEXT_NAMESPACE // "CADInterfacesModule"

