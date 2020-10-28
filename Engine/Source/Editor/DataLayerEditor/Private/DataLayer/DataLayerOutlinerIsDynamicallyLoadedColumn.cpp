// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerOutlinerIsDynamicallyLoadedColumn.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STreeView.h"
#include "ScopedTransaction.h"
#include "EditorStyleSet.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

FName FDataLayerOutlinerIsDynamicallyLoadedColumn::GetID()
{
	static FName DataLayerIsDynamicallyLoaded("DataLayerIsDynamicallyLoaded");
	return DataLayerIsDynamicallyLoaded;
}

SHeaderRow::FColumn::FArguments FDataLayerOutlinerIsDynamicallyLoadedColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

const TSharedRef<SWidget> FDataLayerOutlinerIsDynamicallyLoadedColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->IsA<FDataLayerTreeItem>())
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(0)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, TreeItem]()
				{
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					if (UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer())
					{
						UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
						const auto& Tree = WeakSceneOutliner.Pin()->GetTree();
						if (Tree.IsItemSelected(TreeItem))
						{
							// Toggle IsDynamicallyLoaded flag of selected DataLayers to the same state as the given DataLayer
							bool bIsDynamicallyLoaded = DataLayer->IsDynamicallyLoaded();

							TArray<UDataLayer*> AllSelectedDataLayers;
							for (auto& SelectedItem : Tree.GetSelectedItems())
							{
								FDataLayerTreeItem* SelectedDataLayerTreeItem = SelectedItem->CastTo<FDataLayerTreeItem>();
								UDataLayer* SelectedDataLayer = SelectedDataLayerTreeItem ? SelectedDataLayerTreeItem->GetDataLayer() : nullptr;
								if (SelectedDataLayer && SelectedDataLayer->IsDynamicallyLoaded() == bIsDynamicallyLoaded)
								{
									AllSelectedDataLayers.Add(SelectedDataLayer);
								}
							}

							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayersIsDynamicallyLoaded", "Toggle DataLayers Actors Loading"));
							DataLayerEditorSubsystem->ToggleDataLayersIsDynamicallyLoaded(AllSelectedDataLayers);
						}
						else
						{
							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayerIsDynamicallyLoaded", "Toggle DataLayer Actors Loading"));
							DataLayerEditorSubsystem->ToggleDataLayerIsDynamicallyLoaded(DataLayer);
						}
					}
					return FReply::Handled();
				})
				.ToolTipText(LOCTEXT("IsDynamicallyLoadedButtonToolTip", "Toggle DataLayer Actors Loading"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Content()
				[
					SNew(SImage)
					.Image_Lambda([this, TreeItem]()
					{
						FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
						const UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
						if (DataLayer && DataLayer->IsDynamicallyLoaded())
						{
							return FEditorStyle::GetBrush("DataLayer.DynamicallyLoaded");
						}
						else
						{
							return FEditorStyle::GetBrush("DataLayer.NotDynamicallyLoaded");
						}
					})
				]
			];
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE