// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsEditor.h"
#include "HairStrandsCore.h"

#include "GroomActions.h"
#include "GroomBindingActions.h"
#include "GroomCacheActions.h"

#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"
#include "FbxHairTranslator.h"

#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomBindingDetailsCustomization.h"
#include "GroomCacheImportOptions.h"
#include "GroomCacheImportSettingsCustomization.h"
#include "GroomCacheStreamingManager.h"
#include "GroomCacheTrackEditor.h"
#include "GroomComponentDetailsCustomization.h"
#include "GroomCreateBindingOptions.h"
#include "GroomEditorCommands.h"
#include "GroomEditorMode.h"
#include "GroomPluginSettings.h"

#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "ISequencerModule.h"

IMPLEMENT_MODULE(FGroomEditor, HairStrandsEditor);

#define LOCTEXT_NAMESPACE "GroomEditor"

FName FGroomEditor::GroomEditorAppIdentifier(TEXT("GroomEditor"));


///////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions

void CreateFilename(const FString& InAssetName, const FString& Suffix, FString& OutPackageName, FString& OutAssetName)
{
	// Get a unique package and asset name
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(InAssetName, Suffix, OutPackageName, OutAssetName);
}

void RegisterAsset(UObject* Out)
{
	FAssetRegistryModule::AssetCreated(Out);
}

void SaveAsset(UObject* Object)
{
	UPackage* Package = Object->GetOutermost();
	Object->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Object);

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Package);
	bool bCheckDirty = true;
	bool bPromptToSave = false;
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void FGroomEditor::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedRef<IAssetTypeActions> GroomAssetActions = MakeShared<FGroomActions>();
	TSharedRef<IAssetTypeActions> BindingAssetActions = MakeShared<FGroomBindingActions>();
	TSharedRef<IAssetTypeActions> GroomCacheActions = MakeShared<FGroomCacheActions>();

	AssetTools.RegisterAssetTypeActions(GroomAssetActions);
	AssetTools.RegisterAssetTypeActions(BindingAssetActions);
	AssetTools.RegisterAssetTypeActions(GroomCacheActions);
	RegisteredAssetTypeActions.Add(GroomAssetActions);
	RegisteredAssetTypeActions.Add(BindingAssetActions);

	RegisterHairTranslator<FFbxHairTranslator>();

	// Only register once
	if (!StyleSet.IsValid())
	{
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);
		const FVector2D Icon64x64(64.0f, 64.0f);
		FString HairStrandsContent = IPluginManager::Get().FindPlugin("HairStrands")->GetBaseDir() + "/Content";

		StyleSet = MakeShared<FSlateStyleSet>("Groom");
		StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));
		
		StyleSet->Set("ClassIcon.GroomComponent", new FSlateImageBrush(HairStrandsContent + "/Icons/S_Groom_16.png", Icon16x16));
		StyleSet->Set("ClassThumbnail.GroomComponent", new FSlateImageBrush(HairStrandsContent + "/Icons/S_Groom_64.png", Icon64x64));
		
		StyleSet->Set("ClassIcon.GroomActor", new FSlateImageBrush(HairStrandsContent + "/Icons/S_Groom_16.png", Icon16x16));
		StyleSet->Set("ClassThumbnail.GroomActor", new FSlateImageBrush(HairStrandsContent + "/Icons/S_Groom_64.png", Icon64x64));

		StyleSet->Set("ClassIcon.GroomAsset", new FSlateImageBrush(HairStrandsContent + "/Icons/S_Groom_16.png", Icon16x16));
		StyleSet->Set("ClassThumbnail.GroomAsset", new FSlateImageBrush(HairStrandsContent + "/Icons/S_Groom_64.png", Icon64x64));

		StyleSet->Set("ClassIcon.GroomBindingAsset", new FSlateImageBrush(HairStrandsContent + "/Icons/S_GroomBinding_16.png", Icon16x16));
		StyleSet->Set("ClassThumbnail.GroomBindingAsset", new FSlateImageBrush(HairStrandsContent + "/Icons/S_GroomBinding_64.png", Icon64x64));
		
		StyleSet->Set("GroomEditor.SimulationOptions", new FSlateImageBrush(HairStrandsContent + "/Icons/S_SimulationOptions_40x.png", Icon40x40));
		StyleSet->Set("GroomEditor.SimulationOptions.Small", new FSlateImageBrush(HairStrandsContent + "/Icons/S_SimulationOptions_40x.png", Icon20x20));

		StyleSet->Set("ClassIcon.GroomCache", new FSlateImageBrush(HairStrandsContent + "/Icons/S_GroomCache_64.png", Icon16x16));
		StyleSet->Set("ClassThumbnail.GroomCache", new FSlateImageBrush(HairStrandsContent + "/Icons/S_GroomCache_64.png", Icon64x64));

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());

		// Custom widget for groom component (Group desc override, ...)
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(UGroomComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FGroomComponentDetailsCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UGroomBindingAsset::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FGroomBindingDetailsCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UGroomCreateBindingOptions::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FGroomCreateBindingDetailsCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FGroomCacheImportSettings::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGroomCacheImportSettingsCustomization::MakeInstance));
	}

	FGroomEditorCommands::Register();
	FEditorModeRegistry::Get().RegisterMode<FGroomEditorMode>(
		FGroomEditorMode::EM_GroomEditorModeId,
		LOCTEXT("GroomEditorMode", "Groom Editor"),
		FSlateIcon(),
		false);

	// Asset create/edition helper/wrapper for creating/edition asset within the HairStrandsCore 
	// project without any editor dependencies
	FHairAssetHelper Helper;
	Helper.CreateFilename = CreateFilename;
	Helper.RegisterAsset = RegisterAsset;
	Helper.SaveAsset = SaveAsset;
	FHairStrandsCore::RegisterAssetHelper(Helper);

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	TrackEditorBindingHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FGroomCacheTrackEditor::CreateTrackEditor));

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "Groom",
			LOCTEXT("GroomPluginSettingsName", "Groom"),
			LOCTEXT("GroomPluginSettingsDescription", "Configure the Groom plug-in."),
			GetMutableDefault<UGroomPluginSettings>()
		);
	}

	IGroomCacheStreamingManager::Register();
}

void FGroomEditor::ShutdownModule()
{
	IGroomCacheStreamingManager::Unregister();

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Groom");
	}

	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnRegisterTrackEditor(TrackEditorBindingHandle);
	}

	if (UObjectInitialized())
	{
		FEditorModeRegistry::Get().UnregisterMode(FGroomEditorMode::EM_GroomEditorModeId);

		FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
		if (PropertyModule)
		{
			PropertyModule->UnregisterCustomClassLayout(UGroomComponent::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomClassLayout(UGroomBindingAsset::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomClassLayout(UGroomCreateBindingOptions::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomPropertyTypeLayout(FGroomCacheImportSettings::StaticStruct()->GetFName());
		}
	}

	// #ueent_todo: Unregister the translators
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

	if (AssetToolsModule != nullptr)
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();

		for (auto Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}

	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

TArray<TSharedPtr<IGroomTranslator>> FGroomEditor::GetHairTranslators()
{
	TArray<TSharedPtr<IGroomTranslator>> Translators;
	for (TFunction<TSharedPtr<IGroomTranslator>()>& SpawnTranslator : TranslatorSpawners)
	{
		Translators.Add(SpawnTranslator());
	}

	return Translators;
}

#undef LOCTEXT_NAMESPACE