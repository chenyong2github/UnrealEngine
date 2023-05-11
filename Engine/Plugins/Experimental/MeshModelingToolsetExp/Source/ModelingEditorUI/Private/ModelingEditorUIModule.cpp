// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingEditorUIModule.h"

#include "SkinWeightDetailCustomization.h"
#include "SkinWeightsPaintTool.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FModelingEditorUIModule"

void FModelingEditorUIModule::StartupModule()
{
	// register detail customizations
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(USkinWeightsPaintToolProperties::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FSkinWeightDetailCustomization::MakeInstance));
	
}

void FModelingEditorUIModule::ShutdownModule()
{
	// unregister detail customizations
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.UnregisterCustomClassLayout(USkinWeightsPaintToolProperties::StaticClass()->GetFName());
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FModelingEditorUIModule, ModelingEditorUI)