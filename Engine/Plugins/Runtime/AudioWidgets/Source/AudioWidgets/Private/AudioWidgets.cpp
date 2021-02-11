// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioWidgets.h"

#include "AdvancedWidgetsModule.h"

#define LOCTEXT_NAMESPACE "FAudioWidgetsModule"

void FAudioWidgetsModule::StartupModule()
{
	//FModuleManager::Get().LoadModuleChecked<FAdvancedWidgetsModule>("AdvancedWidgets");
}

void FAudioWidgetsModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAudioWidgetsModule, AudioWidgets)