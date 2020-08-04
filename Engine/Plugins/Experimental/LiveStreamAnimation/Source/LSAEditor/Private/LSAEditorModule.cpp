// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSAEditorModule.h"
#include "LSAHandleDetailCustomization.h"
#include "LiveStreamAnimationHandle.h"
#include "PropertyEditorModule.h"

IMPLEMENT_MODULE(FLSAEditorModule, LSAEditor)

static const FName PropertyEditorModuleName(TEXT("PropertyEditor"));

void FLSAEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
	PropertyModule.RegisterCustomPropertyTypeLayout(FLiveStreamAnimationHandleWrapper::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(FLSAHandleDetailCustomization::MakeInstance));

}

void FLSAEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(PropertyEditorModuleName))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLiveStreamAnimationHandleWrapper::StaticStruct()->GetFName());
	}
}