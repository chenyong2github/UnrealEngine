// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerEditor.h"
#include "MLDeformerAssetActions.h"
#include "MLDeformerAsset.h"
#include "MLDeformerAssetDetails.h"
#include "MLDeformerVizSettingsDetails.h"
#include "MLDeformerEditorMode.h"
#include "MLDeformer.h"

#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"
#include "PropertyEditorDelegates.h"

#define LOCTEXT_NAMESPACE "MLDeformerEditorModule"

IMPLEMENT_MODULE(FMLDeformerEditor, MLDeformerEditor)

void FMLDeformerEditor::StartupModule()
{
	MLDeformerAssetActions = MakeShareable(new FMLDeformerAssetActions);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(MLDeformerAssetActions.ToSharedRef());
	FEditorModeRegistry::Get().RegisterMode<FMLDeformerEditorMode>(FMLDeformerEditorMode::ModeName, LOCTEXT("MLDeformerEditorMode", "MLDeformer"), FSlateIcon(), false);

	// Register object detail customizations.
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("MLDeformerAsset", FOnGetDetailCustomizationInstance::CreateStatic(&FMLDeformerAssetDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("MLDeformerVizSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FMLDeformerVizSettingsDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FMLDeformerEditor::ShutdownModule()
{
	FEditorModeRegistry::Get().UnregisterMode(FMLDeformerEditorMode::ModeName);

	if (MLDeformerAssetActions.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get().UnregisterAssetTypeActions(MLDeformerAssetActions.ToSharedRef());
		}
		MLDeformerAssetActions.Reset();
	}

	// Unregister object detail customizations.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked< FPropertyEditorModule >(TEXT("PropertyEditor"));
		PropertyModule.UnregisterCustomClassLayout(TEXT("MLDeformerAsset"));
		PropertyModule.UnregisterCustomClassLayout(TEXT("MLDeformerVizSettings"));
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

#undef LOCTEXT_NAMESPACE
