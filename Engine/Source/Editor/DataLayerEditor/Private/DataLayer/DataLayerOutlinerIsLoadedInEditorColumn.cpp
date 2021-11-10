// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerOutlinerIsLoadedInEditorColumn.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Views/STreeView.h"
#include "DataLayerTransaction.h"
#include "EditorStyleSet.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

FName FDataLayerOutlinerIsLoadedInEditorColumn::GetID()
{
	static FName DataLayerIsLoadedInEditor("Data Layer Loaded In Editor");
	return DataLayerIsLoadedInEditor;
}

SHeaderRow::FColumn::FArguments FDataLayerOutlinerIsLoadedInEditorColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(FText::FromName(GetColumnID()))
		[
			SNew(SImage)
			.Image(FEditorStyle::GetBrush(TEXT("DataLayer.LoadedInEditor")))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

const TSharedRef<SWidget> FDataLayerOutlinerIsLoadedInEditorColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->IsA<FDataLayerTreeItem>())
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 0, 0, 0)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsEnabled_Lambda([this, TreeItem]()
				{
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					const UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
					const UDataLayer* ParentDataLayer = DataLayer ? DataLayer->GetParent() : nullptr;
					const bool bIsParentLoaded = ParentDataLayer ? ParentDataLayer->IsEffectiveLoadedInEditor() : true;
					return bIsParentLoaded && DataLayer && DataLayer->GetWorld() && !DataLayer->GetWorld()->IsPlayInEditor();
				})
				.IsChecked_Lambda([this, TreeItem]()
				{
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
					return DataLayer && DataLayer->IsEffectiveLoadedInEditor() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, TreeItem](ECheckBoxState NewState)
				{
					bool bNewState = (NewState == ECheckBoxState::Checked);
					FDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FDataLayerTreeItem>();
					if (UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer())
					{
						UWorld* World = DataLayer->GetWorld();
						UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
						const auto& Tree = WeakSceneOutliner.Pin()->GetTree();
						if (Tree.IsItemSelected(TreeItem))
						{
							// Toggle IsLoadedInEditor flag of selected DataLayers to the same state as the given DataLayer
							const bool bIsLoadedInEditor = DataLayer->IsLoadedInEditor();

							TArray<UDataLayer*> AllSelectedDataLayers;
							for (auto& SelectedItem : Tree.GetSelectedItems())
							{
								FDataLayerTreeItem* SelectedDataLayerTreeItem = SelectedItem->CastTo<FDataLayerTreeItem>();
								UDataLayer* SelectedDataLayer = SelectedDataLayerTreeItem ? SelectedDataLayerTreeItem->GetDataLayer() : nullptr;
								if (SelectedDataLayer && SelectedDataLayer->IsLoadedInEditor() == bIsLoadedInEditor)
								{
									AllSelectedDataLayers.Add(SelectedDataLayer);
								}
							}

							const FScopedDataLayerTransaction Transaction(LOCTEXT("ToggleDataLayersIsLoadedInEditor", "Toggle Data Layers Dynamically Loaded In Editor Flag"), World);
							DataLayerEditorSubsystem->ToggleDataLayersIsLoadedInEditor(AllSelectedDataLayers, /*bIsFromUserChange*/true);
						}
						else
						{
							const FScopedDataLayerTransaction Transaction(LOCTEXT("ToggleDataLayerIsLoadedInEditor", "Toggle Data Layer Dynamically Loaded In Editor Flag"), World);
							DataLayerEditorSubsystem->ToggleDataLayerIsLoadedInEditor(DataLayer, /*bIsFromUserChange*/true);
						}
					}
				})
				.ToolTipText(LOCTEXT("IsLoadedInEditorCheckBoxToolTip", "Toggle Loaded In Editor Flag"))
				.HAlign(HAlign_Center)
			];
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE