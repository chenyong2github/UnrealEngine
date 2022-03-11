// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataLayerOutliner.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/HorizontalBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "DataLayer/DataLayerTreeItem.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayerTransaction.h"
#include "Algo/Transform.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

void SDataLayerOutliner::CustomAddToToolbar(TSharedPtr<SHorizontalBox> Toolbar)
{
	Toolbar->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.IsEnabled(this, &SDataLayerOutliner::CanAddSelectedActorsToSelectedDataLayersClicked)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("AddSelectedActorsToSelectedDataLayersTooltip", "Add selected actors to selected Data Layers"))
			.OnClicked(this, &SDataLayerOutliner::OnAddSelectedActorsToSelectedDataLayersClicked)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::Get().GetBrush("DataLayerBrowser.AddSelection"))
			]
		];

	Toolbar->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.IsEnabled(this, &SDataLayerOutliner::CanRemoveSelectedActorsFromSelectedDataLayersClicked)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("RemoveSelectedActorsFromSelectedDataLayersTooltip", "Remove selected actors from selected Data Layers"))
			.OnClicked(this, &SDataLayerOutliner::OnRemoveSelectedActorsFromSelectedDataLayersClicked)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::Get().GetBrush("DataLayerBrowser.RemoveSelection"))
			]
		];
}

TArray<UDataLayer*> SDataLayerOutliner::GetSelectedDataLayers() const
{
	FSceneOutlinerItemSelection ItemSelection(GetSelection());
	TArray<FDataLayerTreeItem*> SelectedDataLayerItems;
	ItemSelection.Get<FDataLayerTreeItem>(SelectedDataLayerItems);
	TArray<UDataLayer*> ValidSelectedDataLayers;
	Algo::TransformIf(SelectedDataLayerItems, ValidSelectedDataLayers, [](const auto Item) { return Item && Item->GetDataLayer(); }, [](const auto Item) { return Item->GetDataLayer(); });
	return ValidSelectedDataLayers;
}

bool SDataLayerOutliner::CanAddSelectedActorsToSelectedDataLayersClicked() const
{
	if (GEditor->GetSelectedActorCount() > 0)
	{
		TArray<UDataLayer*> SelectedDataLayers = GetSelectedDataLayers();
		const bool bSelectedDataLayersContainsLocked = !!SelectedDataLayers.FindByPredicate([](const UDataLayer* DataLayer) { return DataLayer->IsLocked(); });
		return (!SelectedDataLayers.IsEmpty() && !bSelectedDataLayersContainsLocked);
	}
	return false;
}

bool SDataLayerOutliner::CanRemoveSelectedActorsFromSelectedDataLayersClicked() const
{
	return CanAddSelectedActorsToSelectedDataLayersClicked();
}

FReply SDataLayerOutliner::OnAddSelectedActorsToSelectedDataLayersClicked()
{
	if (CanAddSelectedActorsToSelectedDataLayersClicked())
	{
		TArray<UDataLayer*> SelectedDataLayers = GetSelectedDataLayers();
		const FScopedDataLayerTransaction Transaction(LOCTEXT("AddSelectedActorsToSelectedDataLayers", "Add Selected Actors to Selected Data Layers"), SelectedDataLayers[0]->GetWorld());
		UDataLayerEditorSubsystem::Get()->AddSelectedActorsToDataLayers(SelectedDataLayers);
	}
	return FReply::Handled();
}

FReply SDataLayerOutliner::OnRemoveSelectedActorsFromSelectedDataLayersClicked()
{
	if (CanRemoveSelectedActorsFromSelectedDataLayersClicked())
	{
		TArray<UDataLayer*> SelectedDataLayers = GetSelectedDataLayers();
		const FScopedDataLayerTransaction Transaction(LOCTEXT("RemoveSelectedActorsFromSelectedDataLayers", "Remove Selected Actors from Selected Data Layers"), SelectedDataLayers[0]->GetWorld());
		UDataLayerEditorSubsystem::Get()->RemoveSelectedActorsFromDataLayers(SelectedDataLayers);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE