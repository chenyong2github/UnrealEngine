// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorModule.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterLightCardEditorCommands.h"
#include "DisplayClusterRootActor.h"
#include "SDisplayClusterLightCardEditor.h"
#include "DetailCustomizations/DisplayClusterLightCardActorDetails.h"
#include "Settings/DisplayClusterLightCardEditorSettings.h"

#include "ISettingsModule.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditor"

void FDisplayClusterLightCardEditorModule::StartupModule()
{
	RegisterTabSpawners();
	RegisterSettings();
	RegisterDetailCustomizations();
	FDisplayClusterLightCardEditorCommands::Register();
}

void FDisplayClusterLightCardEditorModule::ShutdownModule()
{
	UnregisterTabSpawners();
	UnregisterSettings();
	UnregisterDetailCustomizations();

	FDisplayClusterLightCardEditorCommands::Unregister();
}

void FDisplayClusterLightCardEditorModule::ShowLabels(const FLabelArgs& InArgs)
{
	check(InArgs.RootActor != nullptr);
	
	UDisplayClusterLightCardEditorProjectSettings* ProjectSettings = GetMutableDefault<UDisplayClusterLightCardEditorProjectSettings>();
	
	ProjectSettings->Modify();
	ProjectSettings->bDisplayLightCardLabels = InArgs.bVisible;
	ProjectSettings->LightCardLabelScale = InArgs.Scale;

	if (UDisplayClusterConfigurationData* ConfigData = InArgs.RootActor->GetConfigData())
	{
		FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = ConfigData->StageSettings.Lightcard.ShowOnlyList;
		for (TSoftObjectPtr<AActor> Actor : RootActorLightCards.Actors)
		{
			if (ADisplayClusterLightCardActor* LightCardActor = Cast<ADisplayClusterLightCardActor>(Actor.Get()))
			{
				LightCardActor->Modify(false);
				LightCardActor->ShowLightCardLabel(InArgs.bVisible, InArgs.Scale, InArgs.RootActor);
			}
		}
	}
}

void FDisplayClusterLightCardEditorModule::RegisterTabSpawners()
{
	SDisplayClusterLightCardEditor::RegisterTabSpawner();
}

void FDisplayClusterLightCardEditorModule::UnregisterTabSpawners()
{
	SDisplayClusterLightCardEditor::UnregisterTabSpawner();
}

void FDisplayClusterLightCardEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		UDisplayClusterLightCardEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLightCardEditorProjectSettings>();
		// Needs transactional for undo/redo support in the light card editor
		Settings->SetFlags(RF_Transactional);
		SettingsModule->RegisterSettings("Project", "Plugins", "nDisplayLightCardEditor",
			LOCTEXT("nDisplayLightCardEditorName", "nDisplay Light Card Editor"),
			LOCTEXT("nDisplayLightCardEditorDescription", "Configure settings for the nDisplay Light Card Editor."),
			Settings);
	}
}

void FDisplayClusterLightCardEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "nDisplayLightCardEditor");
	}
}

void FDisplayClusterLightCardEditorModule::RegisterDetailCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		ADisplayClusterLightCardActor::StaticClass()->GetFName(), 
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterLightCardActorDetails::MakeInstance)
	);
}

void FDisplayClusterLightCardEditorModule::UnregisterDetailCustomizations()
{
	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyModule)
	{
		PropertyModule->UnregisterCustomClassLayout(ADisplayClusterLightCardActor::StaticClass()->GetFName());
	}
}

IMPLEMENT_MODULE(FDisplayClusterLightCardEditorModule, DisplayClusterLightCardEditor);

#undef LOCTEXT_NAMESPACE
