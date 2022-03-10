// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterLightCardList.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "IDisplayClusterOperator.h"

#include "ClassIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterLightCardList"

namespace DisplayClusterLightCardListColumnNames
{
	const static FName LightCardName(TEXT("LightCardName"));
};

class SLightCardTreeItemRow : public SMultiColumnTableRow<TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>>
{
	SLATE_BEGIN_ARGS(SLightCardTreeItemRow) {}
	SLATE_END_ARGS()

public:

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem> InLightCardTreeItem)
	{
		LightCardTreeItem = InLightCardTreeItem;

		SMultiColumnTableRow<TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>>::Construct(
			SMultiColumnTableRow<TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>>::FArguments()
			.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"))
		, InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == DisplayClusterLightCardListColumnNames::LightCardName)
		{
			return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6, 0, 0, 0)
			[
				SNew(SExpanderArrow, SharedThis(this))
				.IndentAmount(12)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0, 1, 6, 1))
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(16)
				.HeightOverride(16)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(this, &SLightCardTreeItemRow::GetLightCardIcon)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(this, &SLightCardTreeItemRow::GetLightCardDisplayName)
			];
		}

		return SNullWidget::NullWidget;
	}

private:
	FText GetLightCardDisplayName() const
	{
		FString ItemName = TEXT("");
		if (LightCardTreeItem.IsValid())
		{
			TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem> LightCardTreeItemPin = LightCardTreeItem.Pin();

			if (LightCardTreeItemPin->IsActorLayer())
			{
				ItemName = LightCardTreeItemPin->ActorLayer.ToString();
			}
			else if (LightCardTreeItemPin->LightCardActor.IsValid())
			{
				ItemName = LightCardTreeItemPin->LightCardActor->GetActorNameOrLabel();
			}
		}

		return FText::FromString(*ItemName);
	}

	const FSlateBrush* GetLightCardIcon() const
	{
		FString ActorName = TEXT("");
		if (LightCardTreeItem.IsValid())
		{
			TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem> LightCardTreeItemPin = LightCardTreeItem.Pin();
			if (LightCardTreeItemPin->IsActorLayer())
			{
				return FAppStyle::Get().GetBrush(TEXT("Layer.Icon16x"));
			}
			else if (LightCardTreeItemPin->LightCardActor.IsValid())
			{
				return FClassIconFinder::FindIconForActor(LightCardTreeItemPin->LightCardActor.Get());
			}
		}

		return nullptr;
	}

private:
	TWeakPtr<SDisplayClusterLightCardList::FLightCardTreeItem> LightCardTreeItem;
};


void SDisplayClusterLightCardList::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(LightCardTreeView, STreeView<TSharedPtr<FLightCardTreeItem>>)
		.TreeItemsSource(&LightCardTree)
		.ItemHeight(28)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateRow(this, &SDisplayClusterLightCardList::GenerateTreeItemRow)
		.OnGetChildren(this, &SDisplayClusterLightCardList::GetChildrenForTreeItem)
		.OnSelectionChanged(this, &SDisplayClusterLightCardList::OnTreeItemSelected)
		.HighlightParentNodesForSelection(true)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(DisplayClusterLightCardListColumnNames::LightCardName)
			.DefaultLabel(LOCTEXT("LightCardName", "Light Card"))
			.FillWidth(0.8f)
		)
	];
}

void SDisplayClusterLightCardList::SetRootActor(ADisplayClusterRootActor* NewRootActor)
{
	RootActor = NewRootActor;
	if (FillLightCardList())
	{
		LightCardTreeView->RebuildList();
	}
	
	for (const TSharedPtr<FLightCardTreeItem>& LightCardTreeItem : LightCardTree)
	{
		if (LightCardTreeItem->IsActorLayer())
		{
			LightCardTreeView->SetItemExpansion(LightCardTreeItem, true);
		}
	}
}

bool SDisplayClusterLightCardList::FillLightCardList()
{
	TArray<TSharedPtr<FLightCardTreeItem>> OriginalTree = MoveTemp(LightCardTree);
	LightCardActors.Empty();

	if (RootActor.IsValid())
	{
		FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = RootActor->GetConfigData()->StageSettings.Lightcard.ShowOnlyList;

		for (const FActorLayer& ActorLayer : RootActorLightCards.ActorLayers)
		{
			TSharedPtr<FLightCardTreeItem> LightCardTreeItem = MakeShared<FLightCardTreeItem>();
			LightCardTreeItem->ActorLayer = ActorLayer.Name;
			LightCardTree.Add(LightCardTreeItem);
		}

		for (const TSoftObjectPtr<AActor>& LightCardActor : RootActorLightCards.Actors)
		{
			if (LightCardActor.IsValid())
			{
				TSharedPtr<FLightCardTreeItem> LightCardTreeItem = MakeShared<FLightCardTreeItem>();
				LightCardTreeItem->LightCardActor = LightCardActor.Get();
				LightCardTree.Add(LightCardTreeItem);
				LightCardActors.Add(LightCardTreeItem);
			}
		}

		// If there are any layers that are specified as light card layers, iterate over all actors in the world and 
		// add any that are members of any of the light card layers to the list. Only add an actor once, even if it is
		// in multiple layers
		if (RootActorLightCards.ActorLayers.Num())
		{
			if (UWorld* World = RootActor->GetWorld())
			{
				for (const TWeakObjectPtr<AActor> WeakActor : FActorRange(World))
				{
					if (WeakActor.IsValid())
					{
						for (const FActorLayer& ActorLayer : RootActorLightCards.ActorLayers)
						{
							if (WeakActor->Layers.Contains(ActorLayer.Name))
							{
								TSharedPtr<FLightCardTreeItem> LightCardReference = MakeShared<FLightCardTreeItem>();
								LightCardReference->LightCardActor = WeakActor.Get();
								LightCardReference->ActorLayer = ActorLayer.Name;
								LightCardActors.Add(LightCardReference);
								break;
							}
						}
					}
				}
			}
		}
	}

	// Check if the tree items have changed.
	{
		if (OriginalTree.Num() != LightCardTree.Num())
		{
			return true;
		}
	
		for (const TSharedPtr<FLightCardTreeItem>& OriginalTreeItem : OriginalTree)
		{
			if (!LightCardTree.ContainsByPredicate([OriginalTreeItem](const TSharedPtr<FLightCardTreeItem>& LightCardTreeItem)
			{
				return OriginalTreeItem.IsValid() && LightCardTreeItem.IsValid() &&
					OriginalTreeItem->LightCardActor.Get() == LightCardTreeItem->LightCardActor.Get() &&
						OriginalTreeItem->ActorLayer == LightCardTreeItem->ActorLayer;
			}))
			{
				return true;
			}
		}
	}

	return false;
}

TSharedRef<ITableRow> SDisplayClusterLightCardList::GenerateTreeItemRow(TSharedPtr<FLightCardTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLightCardTreeItemRow, OwnerTable, Item);
}

void SDisplayClusterLightCardList::GetChildrenForTreeItem(TSharedPtr<FLightCardTreeItem> InItem, TArray<TSharedPtr<FLightCardTreeItem>>& OutChildren)
{
	if (InItem.IsValid())
	{
		if (InItem->IsActorLayer())
		{
			for (const TSharedPtr<FLightCardTreeItem>& LightCardActor : LightCardActors)
			{
				if (LightCardActor->IsInActorLayer() && LightCardActor->ActorLayer == InItem->ActorLayer)
				{
					OutChildren.Add(LightCardActor);
				}
			}
		}
	}
}

void SDisplayClusterLightCardList::OnTreeItemSelected(TSharedPtr<FLightCardTreeItem> InItem, ESelectInfo::Type SelectInfo)
{
	if (InItem.IsValid() && InItem->LightCardActor.IsValid())
	{
		IDisplayClusterOperator::Get().ShowDetailsForObject(InItem->LightCardActor.Get());
	}
	else
	{
		IDisplayClusterOperator::Get().ShowDetailsForObject(nullptr);
	}
}

#undef LOCTEXT_NAMESPACE