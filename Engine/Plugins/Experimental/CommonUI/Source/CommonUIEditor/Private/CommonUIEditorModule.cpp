// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUIPrivatePCH.h"
#include "Modules/ModuleInterface.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "CommonVideoPlayerCustomization.h"
#include "GameplayTagsEditorModule.h"

class FCommonUIEditorModule
	: public IModuleInterface
{
public:
	virtual void StartupModule() override 
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyModule.RegisterCustomClassLayout(
			TEXT("CommonVideoPlayer"),
			FOnGetDetailCustomizationInstance::CreateStatic(&FCommonVideoPlayerCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(TEXT("UITag"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGameplayTagCustomizationPublic::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(TEXT("UIActionTag"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGameplayTagCustomizationPublic::MakeInstance));
	}
};

IMPLEMENT_MODULE(FCommonUIEditorModule, CommonUIEditor);
