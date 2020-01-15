// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tree/SCurveEditorTree.h"
#include "Tree/CurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"

#include "CurveEditor.h"

#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SExpanderArrow.h"


struct SCurveEditorTableRow : SMultiColumnTableRow<FCurveEditorTreeItemID>
{
	FCurveEditorTreeItemID TreeItemID;
	TWeakPtr<FCurveEditor> WeakCurveEditor;

	void Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID)
	{
		TreeItemID = InTreeItemID;
		WeakCurveEditor = InCurveEditor;

		SMultiColumnTableRow::Construct(InArgs, OwnerTableView);

		SetForegroundColor(MakeAttributeSP(this, &SCurveEditorTableRow::GetForegroundColorByFilterState));
	}

	FSlateColor GetForegroundColorByFilterState() const
	{
		TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();

		const bool bIsMatch = CurveEditor.IsValid() && ( CurveEditor->GetTree()->GetFilterState(TreeItemID) == ECurveEditorTreeFilterState::Match );
		return bIsMatch ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
		TSharedPtr<ICurveEditorTreeItem> TreeItem = CurveEditor ? CurveEditor->GetTreeItem(TreeItemID).GetItem() : nullptr;

		TSharedRef<ITableRow> TableRow = SharedThis(this);

		TSharedPtr<SWidget> Widget;
		if (TreeItem.IsValid())
		{
			Widget = TreeItem->GenerateCurveEditorTreeWidget(InColumnName, WeakCurveEditor, TreeItemID, TableRow);
		}

		if (!Widget.IsValid())
		{
			Widget = SNullWidget::NullWidget;
		}

		if (InColumnName == ICurveEditorTreeItem::ColumnNames.Label)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot()
				.Padding(FMargin(3.f, 0.f, 0.f, 0.f))
				.VAlign(VAlign_Center)
				[
					Widget.ToSharedRef()
				];
		}

		return Widget.ToSharedRef();
	}
};


void SCurveEditorTree::Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> InCurveEditor)
{
	bFilterWasActive = false;
	HeaderRow = SNew(SHeaderRow)
		.Visibility(EVisibility::Collapsed)

		+ SHeaderRow::Column(ICurveEditorTreeItem::ColumnNames.Label)

		+ SHeaderRow::Column(ICurveEditorTreeItem::ColumnNames.PinHeader)
		.FixedWidth(24.f);

	CurveEditor = InCurveEditor;

	STreeView<FCurveEditorTreeItemID>::Construct(
		STreeView<FCurveEditorTreeItemID>::FArguments()
		.SelectionMode(ESelectionMode::Multi)
		.HeaderRow(HeaderRow)
		.HighlightParentNodesForSelection(true)
		.TreeItemsSource(&RootItems)
		.OnGetChildren(this, &SCurveEditorTree::GetTreeItemChildren)
		.OnGenerateRow(this, &SCurveEditorTree::GenerateRow)
		.OnSetExpansionRecursive(this, &SCurveEditorTree::SetItemExpansionRecursive)
		.OnSelectionChanged_Lambda(
			[this](TListTypeTraits<FCurveEditorTreeItemID>::NullableType InItemID, ESelectInfo::Type Type)
			{
				this->OnTreeSelectionChanged(InItemID, Type);
			}
		)
	);

	CurveEditor->GetTree()->Events.OnItemsChanged.AddSP(this, &SCurveEditorTree::RefreshTree);
}

void SCurveEditorTree::RefreshTree()
{
	RootItems.Reset();

	const FCurveEditorTree* CurveEditorTree = CurveEditor->GetTree();
	const FCurveEditorFilterStates& FilterStates = CurveEditorTree->GetFilterStates();

	// When changing to/from a filtered state, we save and restore expansion states
	if (FilterStates.IsActive() && !bFilterWasActive)
	{
		// Save expansion states
		PreFilterExpandedItems.Reset();
		GetExpandedItems(PreFilterExpandedItems);
	}
	else if (!FilterStates.IsActive() && bFilterWasActive)
	{
		// Add any currently selected items' parents to the expanded items array.
		// This ensures that items that were selected during a filter operation remain expanded and selected when finished
		for (FCurveEditorTreeItemID SelectedItemID : GetSelectedItems())
		{
			// Add the selected item's parent and any grandparents to the list
			const FCurveEditorTreeItem* ParentItem = CurveEditorTree->FindItem(SelectedItemID);
			if (ParentItem)
			{
				ParentItem = CurveEditorTree->FindItem(ParentItem->GetParentID());
			}

			while (ParentItem)
			{
				PreFilterExpandedItems.Add(ParentItem->GetID());
				ParentItem = CurveEditorTree->FindItem(ParentItem->GetParentID());
			}
		}

		// Restore expansion states
		ClearExpandedItems();
		for (FCurveEditorTreeItemID ExpandedItem : PreFilterExpandedItems)
		{
			SetItemExpansion(ExpandedItem, true);
		}
		PreFilterExpandedItems.Reset();
	}

	// Repopulate root tree items based on filters
	for (FCurveEditorTreeItemID RootItemID : CurveEditor->GetRootTreeItems())
	{
		if (FilterStates.Get(RootItemID) != ECurveEditorTreeFilterState::NoMatch)
		{
			RootItems.Add(RootItemID);
		}
	}

	RootItems.Shrink();
	RequestTreeRefresh();

	if (FilterStates.IsActive())
	{
		// If a filter is active, all matched items and their parents are expanded
		ClearExpandedItems();
		for (const TTuple<FCurveEditorTreeItemID, FCurveEditorTreeItem>& Pair : CurveEditorTree->GetAllItems())
		{
			ECurveEditorTreeFilterState FilterState = FilterStates.Get(Pair.Key);

			// Expand any matched items or parents of matched items
			if (FilterState == ECurveEditorTreeFilterState::Match || FilterState == ECurveEditorTreeFilterState::ImplicitParent)
			{
				SetItemExpansion(Pair.Key, true);
			}
		}
	}

	bFilterWasActive = FilterStates.IsActive();
}

FReply SCurveEditorTree::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		ClearSelection();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<ITableRow> SCurveEditorTree::GenerateRow(FCurveEditorTreeItemID ItemID, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SCurveEditorTableRow, OwnerTable, CurveEditor, ItemID);
}

void SCurveEditorTree::GetTreeItemChildren(FCurveEditorTreeItemID Parent, TArray<FCurveEditorTreeItemID>& OutChildren)
{
	const FCurveEditorFilterStates& FilterStates = CurveEditor->GetTree()->GetFilterStates();

	for (FCurveEditorTreeItemID ChildID : CurveEditor->GetTreeItem(Parent).GetChildren())
	{
		if (FilterStates.Get(ChildID) != ECurveEditorTreeFilterState::NoMatch)
		{
			OutChildren.Add(ChildID);
		}
	}
}

void SCurveEditorTree::OnTreeSelectionChanged(FCurveEditorTreeItemID, ESelectInfo::Type)
{
	CurveEditor->GetTree()->SetDirectSelection(GetSelectedItems(), CurveEditor.Get());
}

void SCurveEditorTree::SetItemExpansionRecursive(FCurveEditorTreeItemID Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		SetItemExpansion(Model, bInExpansionState);

		TArray<FCurveEditorTreeItemID> Children;
		GetTreeItemChildren(Model, Children);

		for (FCurveEditorTreeItemID Child : Children)
		{
			if (Child.IsValid())
			{
				SetItemExpansionRecursive(Child, bInExpansionState);
			}
		}
	}
}