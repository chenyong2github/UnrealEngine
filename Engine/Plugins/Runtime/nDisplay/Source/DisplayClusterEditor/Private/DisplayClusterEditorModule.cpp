// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterEditorModule.h"

#include "DisplayClusterRootActor.h"
#include "DetailsCustomization/DisplayClusterRootActorDetailsCustomization.h"

#include "Components/DisplayClusterPreviewComponent.h"
#include "DetailsCustomization/DisplayClusterPreviewComponentDetailsCustomization.h"

#include "Settings/DisplayClusterEditorSettings.h"

#include "ISettingsModule.h"
#include "PropertyEditorModule.h"


#define LOCTEXT_NAMESPACE "DisplayClusterEditor"

void FDisplayClusterEditorModule::StartupModule()
{
	RegisterSettings();
	RegisterCustomizations();
}

void FDisplayClusterEditorModule::ShutdownModule()
{
	UnregisterSettings();
	UnregisterCustomizations();
}


void FDisplayClusterEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			UDisplayClusterEditorSettings::Container,
			UDisplayClusterEditorSettings::Category,
			UDisplayClusterEditorSettings::Section,
			LOCTEXT("RuntimeSettingsName", "nDisplay"),
			LOCTEXT("RuntimeSettingsDescription", "Configure nDisplay"),
			GetMutableDefault<UDisplayClusterEditorSettings>()
		);
	}
}

void FDisplayClusterEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings(UDisplayClusterEditorSettings::Container, UDisplayClusterEditorSettings::Category, UDisplayClusterEditorSettings::Section);
	}
}


void FDisplayClusterEditorModule::RegisterCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyEditorModule.RegisterCustomClassLayout(ADisplayClusterRootActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterRootActorDetailsCustomization::MakeInstance));
	PropertyEditorModule.RegisterCustomClassLayout(UDisplayClusterPreviewComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterPreviewComponentDetailsCustomization::MakeInstance));
}

void FDisplayClusterEditorModule::UnregisterCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyEditorModule.UnregisterCustomClassLayout(ADisplayClusterRootActor::StaticClass()->GetFName());
	PropertyEditorModule.UnregisterCustomClassLayout(UDisplayClusterPreviewComponent::StaticClass()->GetFName());
}

IMPLEMENT_MODULE(FDisplayClusterEditorModule, DisplayClusterEditor);

#undef LOCTEXT_NAMESPACE
