// Copyright Epic Games, Inc. All Rights Reserved.


#include "CameraCalibrationCoreEditorModule.h"

#include "CoreMinimal.h"

#include "DistortionHandlerPickerDetailCustomization.h"
#include "Editor.h"
#include "LensFile.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "CameraCalibrationCoreEditor"

DEFINE_LOG_CATEGORY(LogCameraCalibrationCoreEditor);


void FCameraCalibrationCoreEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FDistortionHandlerPicker::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDistortionHandlerPickerDetailCustomization::MakeInstance));
}

void FCameraCalibrationCoreEditorModule::ShutdownModule()
{
	if (!IsEngineExitRequested() && GEditor && UObjectInitialized())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ULensFile::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDistortionHandlerPicker::StaticStruct()->GetFName());
	}
}


IMPLEMENT_MODULE(FCameraCalibrationCoreEditorModule, CameraCalibrationCoreEditor);


#undef LOCTEXT_NAMESPACE
