// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothPresetEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

namespace UE::Chaos::ClothAsset
{
	TOptional<FClothPresetEditorStyle> FClothPresetEditorStyle::Singleton;

	FClothPresetEditorStyle::FClothPresetEditorStyle()
		: FSlateStyleSet("ClothPresetEditorStyle")
	{
		SetContentRoot(IPluginManager::Get().FindPlugin("ChaosClothAssetEditor")->GetBaseDir() / TEXT("Resources"));

		Set("ClassIcon.ChaosClothPreset", new FSlateVectorImageBrush(RootToContentDir(TEXT("ClothPreset_16.svg")), FVector2D(16)));
		Set("ClassThumbnail.ChaosClothPreset", new FSlateVectorImageBrush(RootToContentDir(TEXT("ClothPreset_64.svg")), FVector2D(64)));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	FClothPresetEditorStyle::~FClothPresetEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
}  // End namespace UE::Chaos::ClothAsset
