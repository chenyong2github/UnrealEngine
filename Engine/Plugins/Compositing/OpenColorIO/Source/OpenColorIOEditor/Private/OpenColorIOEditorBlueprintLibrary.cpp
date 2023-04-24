// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOEditorBlueprintLibrary.h"

#include "Modules/ModuleManager.h"

#include "OpenColorIOEditorModule.h"
#include "OpenColorIOColorSpace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OpenColorIOEditorBlueprintLibrary)


void UOpenColorIOEditorBlueprintLibrary::SetActiveViewportConfiguration(const FOpenColorIODisplayConfiguration& InConfiguration)
{
	FOpenColorIOEditorModule& OpenColorIOEditorModule = FModuleManager::LoadModuleChecked<FOpenColorIOEditorModule>("OpenColorIOEditor");

	OpenColorIOEditorModule.SetActiveViewportConfiguration(InConfiguration);
}
