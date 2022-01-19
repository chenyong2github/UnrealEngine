// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonMenuExtensionsModule.h"
#include "BufferVisualizationMenuCommands.h"
#include "NaniteVisualizationMenuCommands.h"
#include "LumenVisualizationMenuCommands.h"
#include "ShowFlagMenuCommands.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FCommonMenuExtensionsModule, CommonMenuExtensions);

void FCommonMenuExtensionsModule::StartupModule()
{
	FBufferVisualizationMenuCommands::Register();
	FNaniteVisualizationMenuCommands::Register();
	FLumenVisualizationMenuCommands::Register();
	FShowFlagMenuCommands::Register();
}

void FCommonMenuExtensionsModule::ShutdownModule()
{
	FShowFlagMenuCommands::Unregister();
	FNaniteVisualizationMenuCommands::Unregister();
	FLumenVisualizationMenuCommands::Unregister();
	FBufferVisualizationMenuCommands::Unregister();
}