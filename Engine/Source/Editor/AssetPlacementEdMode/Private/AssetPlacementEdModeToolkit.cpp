// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeToolkit.h"

#include "IDetailsView.h"
#include "SAssetPlacementPalette.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdModeToolkit"

FAssetPlacementEdModeToolkit::FAssetPlacementEdModeToolkit()
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
		.FillHeight(.6)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				DetailsView.ToSharedRef()
			]
			+ SScrollBox::Slot()
			[
				ModeDetailsView.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("AssetPaletteHeader", "Asset Palette"))
			.BodyContent()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SAssetPlacementPalette)
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
