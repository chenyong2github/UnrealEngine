// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeToolkit.h"
#include "AssetPlacementEdMode.h"
#include "AssetPlacementSettings.h"
#include "SAssetPlacementPalette.h"

#include "Modules/ModuleManager.h"

#include "Widgets/Layout/SExpandableArea.h"

#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdModeToolkit"

FAssetPlacementEdModeToolkit::FAssetPlacementEdModeToolkit(TWeakObjectPtr<UAssetPlacementSettings> InPlacementSettings)
	: PlacementSettings(InPlacementSettings)
{
}

void FAssetPlacementEdModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FAssetPlacementEdModeToolkit::GetToolkitFName() const
{
	return FName("AssetPlacementEdMode");
}

FText FAssetPlacementEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "AssetPlacementEdMode Tool");
}

TSharedPtr<SWidget> FAssetPlacementEdModeToolkit::GetInlineContent() const
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FModeToolkit::GetInlineContent().ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("AssetPaletteHeader", "Asset Palette"))
			.BodyContent()
			[
				SNew(SAssetPlacementPalette)
				.PlacementSettings(PlacementSettings)
			]
		];
}

#undef LOCTEXT_NAMESPACE
