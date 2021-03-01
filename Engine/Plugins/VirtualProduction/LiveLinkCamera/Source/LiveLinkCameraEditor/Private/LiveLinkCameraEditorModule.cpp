// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkCameraEditorModule.h"

#include "LiveLinkCameraController.h"
#include "LiveLinkCameraControllerCustomization.h"
#include "PropertyEditorModule.h"

void FLiveLinkEditorModule::StartupModule()  
{
	RegisterCustomizations();
}

void FLiveLinkEditorModule::ShutdownModule()
{
	UnregisterCustomizations();
}

void FLiveLinkEditorModule::RegisterCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(ULiveLinkCameraController::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLiveLinkCameraControllerCustomization::MakeInstance));
}

void FLiveLinkEditorModule::UnregisterCustomizations()
{
	if (UObjectInitialized() && !IsEngineExitRequested())
	{
		FPropertyEditorModule* PropertyEditorModule = FModuleManager::Get().GetModulePtr<FPropertyEditorModule>("PropertyEditor");
		if (PropertyEditorModule)
		{
			PropertyEditorModule->UnregisterCustomClassLayout(ULiveLinkCameraController::StaticClass()->GetFName());
		}
	}
}

IMPLEMENT_MODULE(FLiveLinkEditorModule, LiveLinkCameraEditor)


