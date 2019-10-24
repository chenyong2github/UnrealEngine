// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CADToolsModule.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FCADToolsModule"

FCADToolsModule& FCADToolsModule::Get()
{
	return FModuleManager::LoadModuleChecked< FCADToolsModule >(CADTOOLS_MODULE_NAME);
}

bool FCADToolsModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(CADTOOLS_MODULE_NAME);
}

void FCADToolsModule::StartupModule()
{
}

IMPLEMENT_MODULE(FCADToolsModule, CADTools);

#undef LOCTEXT_NAMESPACE

