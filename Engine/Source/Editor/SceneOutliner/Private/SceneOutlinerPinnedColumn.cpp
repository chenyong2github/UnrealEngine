// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerPinnedColumn.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerPinnedActorColumn"

bool FSceneOutlinerPinnedColumn::FSceneOutlinerPinnedStateCache::CheckChildren(const ISceneOutlinerTreeItem& Item) const
{
	if (const bool* const State = PinnedStateInfo.Find(&Item))
	{
		return *State;
	}

	bool bIsPinned = false;
	for (const auto& ChildPtr : Item.GetChildren())
	{
		FSceneOutlinerTreeItemPtr Child = ChildPtr.Pin();
		if (Child.IsValid() && GetPinnedState(*Child))
		{
			bIsPinned = true;
			break;
		}
	}
	PinnedStateInfo.Add(&Item, bIsPinned);
	
	return bIsPinned;
}

bool FSceneOutlinerPinnedColumn::FSceneOutlinerPinnedStateCache::GetPinnedState(const ISceneOutlinerTreeItem& Item) const
{
	if (Item.HasPinnedStateInfo())
	{
		if (const bool* const State = PinnedStateInfo.Find(&Item))
		{
			return *State;
		}

		const bool bIsPinned = Item.GetPinnedState();
		PinnedStateInfo.Add(&Item, bIsPinned);
		return bIsPinned;
	}

	return CheckChildren(Item);
}

void FSceneOutlinerPinnedColumn::FSceneOutlinerPinnedStateCache::Empty()
{
	PinnedStateInfo.Empty();
}

class SPinnedWidget : public SImage
{
public:
	SLATE_BEGIN_ARGS(SPinnedWidget) {}
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs, TWeakPtr<ISceneOutliner> InWeakOutliner, TWeakPtr<ISceneOutlinerTreeItem> InWeakTreeItem, const TWeakPtr<FSceneOutlinerPinnedColumn>& InWeakColumn, const STableRow<FSceneOutlinerTreeItemPtr>* InRow)
	{
		WeakTreeItem = InWeakTreeItem;
		WeakOutliner = InWeakOutliner;
		WeakColumn = InWeakColumn;
		Row = InRow;

		SImage::Construct(
			SImage::FArguments()
            .ColorAndOpacity(this, &SPinnedWidget::GetForegroundColor)
            .Image(this, &SPinnedWidget::GetBrush)
        );

		static const FName NAME_PinnedHoveredBrush = TEXT("SceneOutliner.PinnedHighlighIcon");
		static const FName NAME_PinnedNotHoveredBrush = TEXT("SceneOutliner.PinnedIcon");
		static const FName NAME_UnpinnedHoveredBrush = TEXT("SceneOutliner.UnpinnedHighlighIcon");
		static const FName NAME_UnpinnedNotHoveredBrush = TEXT("SceneOutliner.UnpinnedIcon");

		PinnedHoveredBrush = FEditorStyle::GetBrush(NAME_PinnedHoveredBrush);
		PinnedNotHoveredBrush = FEditorStyle::GetBrush(NAME_PinnedNotHoveredBrush);
		UnpinnedHoveredBrush = FEditorStyle::GetBrush(NAME_UnpinnedHoveredBrush);
		UnpinnedNotHoveredBrush = FEditorStyle::GetBrush(NAME_UnpinnedNotHoveredBrush);
	}

private:
	bool IsPinned() const
	{
		return WeakTreeItem.IsValid() && WeakColumn.IsValid() ? WeakColumn.Pin()->IsItemPinned(*WeakTreeItem.Pin()) : false;
	}
	
	FReply HandleClick() const
	{
		if (!WeakTreeItem.IsValid() || !WeakOutliner.IsValid())
		{
			return FReply::Unhandled();
		}

		const auto Outliner = WeakOutliner.Pin();
		const auto TreeItem = WeakTreeItem.Pin();
		const auto& Tree = Outliner->GetTree();

		if (!IsPinned())
		{
			if (Tree.IsItemSelected(TreeItem.ToSharedRef()))
			{
				Outliner->PinSelectedItems();
			}
			else
			{
				Outliner->PinItem(TreeItem);
			}
		}
		else
		{
			if (Tree.IsItemSelected(TreeItem.ToSharedRef()))
			{
				Outliner->UnpinSelectedItems();
			}
			else
			{
				Outliner->UnpinItem(TreeItem);
			}
		}

		return FReply::Handled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		return HandleClick();
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			return HandleClick();
		}
		
		return FReply::Unhandled();
	}

	const FSlateBrush* GetBrush() const
	{
		if (IsPinned())
		{
			return IsHovered() ? PinnedHoveredBrush : PinnedNotHoveredBrush;
		}
		else
		{
			return IsHovered() ? UnpinnedHoveredBrush : UnpinnedNotHoveredBrush;
		}
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const auto Outliner = WeakOutliner.Pin();
		const auto TreeItem = WeakTreeItem.Pin();
		const bool bIsSelected = Outliner->GetTree().IsItemSelected(TreeItem.ToSharedRef());
		
		if (!IsPinned())
		{
			if (!Row->IsHovered() && !bIsSelected)
			{
				return FLinearColor::Transparent;
			}
		}
		
		return IsHovered() ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
	}

	/** The tree item we relate to */
	TWeakPtr<ISceneOutlinerTreeItem> WeakTreeItem;

	/** Weak pointer back to the outliner */
	TWeakPtr<ISceneOutliner> WeakOutliner;
	/** Weak pointer back to the column to check cached pinned state */
	TWeakPtr<FSceneOutlinerPinnedColumn> WeakColumn;

	/** Weak pointer back to the row */
	const STableRow<FSceneOutlinerTreeItemPtr>* Row = nullptr;

	const FSlateBrush* PinnedHoveredBrush = nullptr;
	const FSlateBrush* PinnedNotHoveredBrush = nullptr;
	const FSlateBrush* UnpinnedHoveredBrush = nullptr;
	const FSlateBrush* UnpinnedNotHoveredBrush = nullptr;
};

FName FSceneOutlinerPinnedColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FSceneOutlinerPinnedColumn::ConstructHeaderRowColumn()
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
			.Image(this, &FSceneOutlinerPinnedColumn::GetHeaderIcon)
		];
}

const TSharedRef<SWidget> FSceneOutlinerPinnedColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->ShouldShowPinnedState())
	{
		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SPinnedWidget, WeakSceneOutliner, TreeItem, SharedThis(this), &Row)
		];
	}
	return SNullWidget::NullWidget;
}

void FSceneOutlinerPinnedColumn::Tick(double InCurrentTime, float InDeltaTime)
{
	PinnedStateCache.Empty();
}

bool FSceneOutlinerPinnedColumn::IsItemPinned(const ISceneOutlinerTreeItem& Item) const
{
	return PinnedStateCache.GetPinnedState(Item);
}

const FSlateBrush* FSceneOutlinerPinnedColumn::GetHeaderIcon() const
{
	return FEditorStyle::GetBrush("SceneOutliner.PinnedHighlighIcon");
}

#undef LOCTEXT_NAMESPACE