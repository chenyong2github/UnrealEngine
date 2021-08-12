// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeToolkit.h"

#include "IDetailsView.h"
#include "SAssetPlacementPalette.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Toolkits/AssetEditorModeUILayer.h"

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

void FAssetPlacementEdModeToolkit::InvokeUI()
{
	FModeToolkit::InvokeUI();

	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		AssetPaletteTab = ModeUILayerPtr->GetTabManager()->TryInvokeTab(FAssetEditorModeUILayer::BottomLeftTabID);
	}
}

TSharedPtr<SWidget> FAssetPlacementEdModeToolkit::GetInlineContent() const
{
	return 	SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			DetailsView.ToSharedRef()
		]
		+ SScrollBox::Slot()
		[
			ModeDetailsView.ToSharedRef()
		];
}

void FAssetPlacementEdModeToolkit::RequestModeUITabs()
{
	FModeToolkit::RequestModeUITabs();
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		TSharedRef<FWorkspaceItem> MenuGroup = ModeUILayerPtr->GetModeMenuCategory().ToSharedRef();
		AssetPaletteInfo.OnSpawnTab = FOnSpawnTab::CreateSP(SharedThis(this), &FAssetPlacementEdModeToolkit::CreateAssetPalette);
		AssetPaletteInfo.TabLabel = LOCTEXT("AssetPaletteTab", "Asset Palette");
		AssetPaletteInfo.TabTooltip = LOCTEXT("ModesToolboxTabTooltipText", "Open the  Modes tab, which contains the active editor mode's settings.");
		ModeUILayerPtr->SetModePanelInfo(FAssetEditorModeUILayer::BottomLeftTabID, AssetPaletteInfo);
	}
}



TSharedRef<SDockTab> FAssetPlacementEdModeToolkit::CreateAssetPalette(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> CreatedTab = SNew(SDockTab)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SAssetPlacementPalette)
			]
		];

	const FSlateBrush* TabIcon = GetEditorModeIcon().GetSmallIcon();
	if (CreatedTab)
	{
		CreatedTab->SetTabIcon(TabIcon);
	}
	AssetPaletteTab = CreatedTab;
	return CreatedTab.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
