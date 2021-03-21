// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonMenuExtensionsModule.h"
#include "BufferVisualizationMenuCommands.h"
#include "NaniteVisualizationMenuCommands.h"
#include "ShowFlagMenuCommands.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FCommonMenuExtensionsModule, CommonMenuExtensions);

void FCommonMenuExtensionsModule::StartupModule()
{
	FBufferVisualizationMenuCommands::Register();
#if 0 // TODO: NANITE_VIEW_MODES
	FNaniteVisualizationMenuCommands::Register();
#endif
	FShowFlagMenuCommands::Register();
}

void FCommonMenuExtensionsModule::ShutdownModule()
{
	FShowFlagMenuCommands::Unregister();
#if 0 // TODO: NANITE_VIEW_MODES
	FNaniteVisualizationMenuCommands::Unregister();
#endif
	FBufferVisualizationMenuCommands::Unregister();
}