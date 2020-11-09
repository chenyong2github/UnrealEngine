// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerOutlinerIsDynamicallyLoadedColumn.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
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
		.FixedWidth(40.f)
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
			.Padding(2, 0, 0, 0)
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
						bool bSuccess = false;
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

							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayersIsDynamicallyLoaded", "Toggle DataLayers Runtime Dynamically Loaded Flag"));
							bSuccess = DataLayerEditorSubsystem->ToggleDataLayersIsDynamicallyLoaded(AllSelectedDataLayers);
						}
						else
						{
							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayerIsDynamicallyLoaded", "Toggle DataLayer Runtime Dynamically Loaded Flag"));
							bSuccess = DataLayerEditorSubsystem->ToggleDataLayerIsDynamicallyLoaded(DataLayer);
						}
						if (!bSuccess)
						{
							// Cancelled : Undo last transaction
							GEditor->Trans->Undo();
						}
					}
					return FReply::Handled();
				})
				.ToolTipText(LOCTEXT("IsDynamicallyLoadedButtonToolTip", "Toggle DataLayer Runtime Dynamically Loaded Flag"))
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
			]
			+ SHorizontalBox::Slot()
			.Padding(2, 0, 0, 0)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsEnabled_Lambda([this, TreeItem]()
				{
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
					return DataLayer && DataLayer->IsDynamicallyLoaded();
				})
				.IsChecked_Lambda([this, TreeItem]()
				{
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
					return DataLayer && DataLayer->IsDynamicallyLoadedInEditor() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, TreeItem](ECheckBoxState NewState)
				{
					bool bNewState = (NewState == ECheckBoxState::Checked);
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					if (UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer())
					{
						bool bSuccess = false;
						UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
						const auto& Tree = WeakSceneOutliner.Pin()->GetTree();
						if (Tree.IsItemSelected(TreeItem))
						{
							// Toggle IsDynamicallyLoadedInEditor flag of selected DataLayers to the same state as the given DataLayer
							bool bIsDynamicallyLoadedInEditor = DataLayer->IsDynamicallyLoadedInEditor();

							TArray<UDataLayer*> AllSelectedDataLayers;
							for (auto& SelectedItem : Tree.GetSelectedItems())
							{
								FDataLayerTreeItem* SelectedDataLayerTreeItem = SelectedItem->CastTo<FDataLayerTreeItem>();
								UDataLayer* SelectedDataLayer = SelectedDataLayerTreeItem ? SelectedDataLayerTreeItem->GetDataLayer() : nullptr;
								if (SelectedDataLayer && SelectedDataLayer->IsDynamicallyLoadedInEditor() == bIsDynamicallyLoadedInEditor)
								{
									AllSelectedDataLayers.Add(SelectedDataLayer);
								}
							}

							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayersIsDynamicallyLoadedInEditor", "Toggle DataLayers Editor Dynamically Loaded Flag"));
							bSuccess = DataLayerEditorSubsystem->ToggleDataLayersIsDynamicallyLoadedInEditor(AllSelectedDataLayers);
						}
						else
						{
							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayerIsDynamicallyLoadedInEditor", "Toggle DataLayer Editor Dynamically Loaded Flag"));
							bSuccess = DataLayerEditorSubsystem->ToggleDataLayerIsDynamicallyLoadedInEditor(DataLayer);
						}
						if (!bSuccess)
						{
							// Cancelled : Undo last transaction
							GEditor->Trans->Undo();
						}
					}
				})
				.ToolTipText(LOCTEXT("IsDynamicallyLoadedInEditorCheckBoxToolTip", "Toggle DataLayer Editor Dynamically Loaded Flag"))
				.HAlign(HAlign_Center)
			];
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE