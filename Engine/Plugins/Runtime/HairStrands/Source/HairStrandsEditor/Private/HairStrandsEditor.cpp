// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsEditor.h"
#include "HairStrandsCore.h"

#include "GroomActions.h"
#include "GroomBindingActions.h"

#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"
#include "FbxHairTranslator.h"

#include "GroomEditorCommands.h"
#include "GroomEditorMode.h"
#include "GroomAsset.h"
#include "GroomComponentDetailsCustomization.h"

#include "AssetRegistryModule.h"
#include "FileHelpers.h"

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
	TSharedRef<IAssetTypeActions> GroomAssetActions = MakeShareable(new FGroomActions());
	TSharedRef<IAssetTypeActions> BindingAssetActions = MakeShareable(new FGroomBindingActions());

	AssetTools.RegisterAssetTypeActions(GroomAssetActions);
	AssetTools.RegisterAssetTypeActions(BindingAssetActions);
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

		StyleSet = MakeShareable(new FSlateStyleSet("Groom"));
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

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());

		// Custom widget for groom component (Group desc override, ...)
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(UGroomComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FGroomComponentDetailsCustomization::MakeInstance));
	}

	FGroomEditorCommands::Register();
	FEditorModeRegistry::Get().RegisterMode<FGroomEditorMode>(
		FGroomEditorMode::EM_GroomEditorModeId,
		LOCTEXT("GroomEditorMode", "Groom Editor"),
		FSlateIcon(),
		false);

	// Asset create/edition helper/wrapper for creating/edition asset withn the HairStrandsCore 
	// project without any editor dependencies
	FHairAssetHelper Helper;
	Helper.CreateFilename = CreateFilename;
	Helper.RegisterAsset = RegisterAsset;
	Helper.SaveAsset = SaveAsset;
	FHairStrandsCore::RegisterAssetHelper(Helper);
}

void FGroomEditor::ShutdownModule()
{
	FEditorModeRegistry::Get().UnregisterMode(FGroomEditorMode::EM_GroomEditorModeId);

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