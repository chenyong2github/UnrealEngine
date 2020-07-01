// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshEditorModule.h"

#include "Interfaces/IPluginManager.h"

IMPLEMENT_MODULE(FVirtualHeightfieldMeshEditorModule, VirtualHeightfieldMeshEditor);

void FVirtualHeightfieldMeshEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("VirtualHeightfieldMeshComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FVirtualHeightfieldMeshComponentDetailsCustomization::MakeInstance));
}

void FVirtualHeightfieldMeshEditorModule::ShutdownModule()
{
}
