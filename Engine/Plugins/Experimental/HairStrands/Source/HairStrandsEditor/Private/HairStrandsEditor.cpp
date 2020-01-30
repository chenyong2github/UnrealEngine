// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsEditor.h"
#include "GroomActions.h"

#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"
#include "FbxHairTranslator.h"

IMPLEMENT_MODULE(FHairStrandsEditor, HairStrandsEditor);

void FHairStrandsEditor::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedRef<IAssetTypeActions> AssetActions = MakeShareable(new FGroomActions());

	AssetTools.RegisterAssetTypeActions(AssetActions);
	RegisteredAssetTypeActions.Add(AssetActions);

	RegisterHairTranslator<FFbxHairTranslator>();

	// Only register once
	if (!StyleSet.IsValid())
	{
		const FVector2D Icon16x16(16.0f, 16.0f);
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
		
		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
	}
}

void FHairStrandsEditor::ShutdownModule()
{
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

TArray<TSharedPtr<IHairStrandsTranslator>> FHairStrandsEditor::GetHairTranslators()
{
	TArray<TSharedPtr<IHairStrandsTranslator>> Translators;
	for (TFunction<TSharedPtr<IHairStrandsTranslator>()>& SpawnTranslator : TranslatorSpawners)
	{
		Translators.Add(SpawnTranslator());
	}

	return Translators;
}
