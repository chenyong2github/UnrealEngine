// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateEditorModule.h"

#include "MediaPlate.h"
#include "MediaPlateCustomization.h"
#include "PropertyEditorModule.h"

DEFINE_LOG_CATEGORY(LogMediaPlateEditor);

void FMediaPlateEditorModule::StartupModule()
{
	// Register customizations.
	MediaPlateName = AMediaPlate::StaticClass()->GetFName();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(MediaPlateName,
		FOnGetDetailCustomizationInstance::CreateStatic(&FMediaPlateCustomization::MakeInstance));
}

void FMediaPlateEditorModule::ShutdownModule()
{
	// Unregister customizations.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(MediaPlateName);
}

IMPLEMENT_MODULE(FMediaPlateEditorModule, MediaPlateEditor)
