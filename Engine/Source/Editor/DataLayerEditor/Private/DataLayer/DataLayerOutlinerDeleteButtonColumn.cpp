// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerOutlinerDeleteButtonColumn.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Images/SImage.h"
#include "DataLayerActorTreeItem.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "SceneOutlinerPublicTypes.h"
#include "ScopedTransaction.h"
#include "EditorStyleSet.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

FName FDataLayerOutlinerDeleteButtonColumn::GetID()
{
	static FName DataLayeDeleteButton("Remove Actor");
	return DataLayeDeleteButton;
}

SHeaderRow::FColumn::FArguments FDataLayerOutlinerDeleteButtonColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
	.FixedWidth(40.f)
	.DefaultTooltip(FText::FromName(GetColumnID()))
	[
		SNew(SSpacer)
	];
}

const TSharedRef<SWidget> FDataLayerOutlinerDeleteButtonColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (FDataLayerActorTreeItem* DataLayerActorItem = TreeItem->CastTo<FDataLayerActorTreeItem>())
	{
		return SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(FEditorStyle::Get(), "DataLayerBrowserButton")
			.ContentPadding(0)
			.Visibility_Lambda([this, TreeItem, DataLayerActorItem]()
			{
				AActor* Actor = DataLayerActorItem->GetActor();
				const UDataLayer* DataLayer = DataLayerActorItem->GetDataLayer();
				return (Actor && DataLayer && !DataLayer->IsLocked()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnClicked_Lambda([this, TreeItem, DataLayerActorItem]()
			{
				AActor* Actor = DataLayerActorItem->GetActor();
				const UDataLayer* DataLayer = DataLayerActorItem->GetDataLayer();
				if (Actor && DataLayer)
				{
					UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
					if (auto SceneOutliner = WeakSceneOutliner.IsValid() ? WeakSceneOutliner.Pin() : nullptr)
					{
						const auto& Tree = SceneOutliner->GetTree();
						if (SceneOutliner->GetSharedData().CustomDelete.IsBound())
						{
							TArray<TWeakPtr<ISceneOutlinerTreeItem>> SelectedItems;
							if (Tree.IsItemSelected(TreeItem))
							{
								for (auto& SelectedItem : Tree.GetSelectedItems())
								{
									if (FDataLayerActorTreeItem* SelectedDataLayerActorTreeItem = SelectedItem->CastTo<FDataLayerActorTreeItem>())
									{
										SelectedItems.Add(SelectedDataLayerActorTreeItem->AsShared());
									}
								}
							}
							else
							{
								SelectedItems.Add(TreeItem);
							}
							SceneOutliner->GetSharedData().CustomDelete.Execute(SelectedItems);
						}
					}
				}
				return FReply::Handled();
			})
			.ToolTipText(LOCTEXT("RemoveFromDataLayerButtonText", "Remove from Data Layer"))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("DataLayerBrowser.Actor.RemoveFromDataLayer")))
			];
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE