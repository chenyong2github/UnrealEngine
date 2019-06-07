// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tree/CurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/CurveEditorTreeFilter.h"
#include "CurveEditor.h"

#include "Algo/AnyOf.h"

class FCurveModel;

TArrayView<const FCurveModelID> FCurveEditorTreeItem::GetOrCreateCurves(FCurveEditor* CurveEditor)
{
	if (Curves.Num() == 0)
	{
		TSharedPtr<ICurveEditorTreeItem> ItemPtr = GetItem();
		if (ItemPtr.IsValid())
		{
			TArray<TUniquePtr<FCurveModel>> NewCurveModels;
			ItemPtr->CreateCurveModels(NewCurveModels);

			for (TUniquePtr<FCurveModel>& NewCurve : NewCurveModels)
			{
				FCurveModelID NewModelID = CurveEditor->AddCurveForTreeItem(MoveTemp(NewCurve), ThisID);
				Curves.Add(NewModelID);
			}
		}
	}
	return Curves;
}

void FCurveEditorTreeItem::DestroyCurves(FCurveEditor* CurveEditor)
{
	for (FCurveModelID CurveID : Curves)
	{
		// Remove the curve from the curve editor
		CurveEditor->RemoveCurve(CurveID);
	}
	Curves.Empty();
}

void FCurveEditorTreeItem::DestroyUnpinnedCurves(FCurveEditor* CurveEditor)
{
	for (int32 Index = Curves.Num()-1; Index >= 0; --Index)
	{
		// Remove the curve from the curve editor
		if (!CurveEditor->IsCurvePinned(Curves[Index]))
		{
			CurveEditor->RemoveCurve(Curves[Index]);
			Curves.RemoveAtSwap(Index, 1, false);
		}
	}
}

FScopedCurveEditorTreeUpdateGuard::FScopedCurveEditorTreeUpdateGuard(FCurveEditorTree* InTree)
	: Tree(InTree)
{
	Tree->OnChanged()->UpdateGuardCounter += 1;
}

FScopedCurveEditorTreeUpdateGuard::FScopedCurveEditorTreeUpdateGuard(FScopedCurveEditorTreeUpdateGuard&& RHS)
	: Tree(RHS.Tree)
{
	Tree->OnChanged()->UpdateGuardCounter += 1;
}

FScopedCurveEditorTreeUpdateGuard& FScopedCurveEditorTreeUpdateGuard::operator=(FScopedCurveEditorTreeUpdateGuard&& RHS)
{
	if (&RHS != this)
	{
		Tree = RHS.Tree;
		Tree->OnChanged()->UpdateGuardCounter += 1;
	}
	return *this;
}

FScopedCurveEditorTreeUpdateGuard::~FScopedCurveEditorTreeUpdateGuard()
{
	FCurveEditorOnChangedEvent* OnChangedEvent = Tree->OnChanged();
	if (--OnChangedEvent->UpdateGuardCounter == 0)
	{
		TGuardValue<bool> Guard(OnChangedEvent->bIsBroadcastInProgress, true);
		OnChangedEvent->Delegate.Broadcast();
	}
}

FCurveEditorTree::FCurveEditorTree()
{
	NextTreeItemID.Value = 1;
}

FCurveEditorTreeItem& FCurveEditorTree::GetItem(FCurveEditorTreeItemID ItemID)
{
	return Items.FindChecked(ItemID);
}

const FCurveEditorTreeItem& FCurveEditorTree::GetItem(FCurveEditorTreeItemID ItemID) const
{
	return Items.FindChecked(ItemID);
}

FCurveEditorTreeItem* FCurveEditorTree::FindItem(FCurveEditorTreeItemID ItemID)
{
	return Items.Find(ItemID);
}

const TArray<FCurveEditorTreeItemID>& FCurveEditorTree::GetRootItems() const
{
	return RootItems.ChildIDs;
}

const TMap<FCurveEditorTreeItemID, FCurveEditorTreeItem>& FCurveEditorTree::GetAllItems() const
{
	return Items;
}

FCurveEditorTreeItem* FCurveEditorTree::AddItem(FCurveEditorTreeItemID ParentID)
{
	checkf(!OnChangedEvent.IsBroadcastInProgress(), TEXT("Curve editor tree must not be manipulated in response to it changing"));
	FScopedCurveEditorTreeUpdateGuard BroadcastChangeUpdate(this);

	FCurveEditorTreeItemID NewItemID = NextTreeItemID;

	FCurveEditorTreeItem& NewItem = Items.Add(NewItemID);
	NewItem.ThisID   = NewItemID;
	NewItem.ParentID = ParentID;

	FSortedCurveEditorTreeItems& ParentContainer = ParentID.IsValid() ? Items.FindChecked(ParentID).Children : RootItems;
	ParentContainer.ChildIDs.Add(NewItemID);
	ParentContainer.bRequiresSort = true;

	++NextTreeItemID.Value;

	return &NewItem;
}

void FCurveEditorTree::RemoveItem(FCurveEditorTreeItemID ItemID, FCurveEditor* CurveEditor)
{
	checkf(!OnChangedEvent.IsBroadcastInProgress(), TEXT("Curve editor tree must not be manipulated in response to it changing"));
	FScopedCurveEditorTreeUpdateGuard BroadcastChangeUpdate(this);

	FCurveEditorTreeItem* Item = Items.Find(ItemID);
	if (!Item)
	{
		return;
	}

	// Remove the item from its parent
	FSortedCurveEditorTreeItems& ParentContainer = Item->ParentID.IsValid() ? Items.FindChecked(Item->ParentID).Children : RootItems;
	ParentContainer.ChildIDs.Remove(ItemID);

	Item->DestroyCurves(CurveEditor);

	// Item is going away now (and may be reallocated) so move its children into the function
	RemoveChildrenRecursive(MoveTemp(Item->Children.ChildIDs), CurveEditor);

	// Item is no longer valid

	Items.Remove(ItemID);
	Selection.Remove(ItemID);
}

void FCurveEditorTree::RemoveChildrenRecursive(TArray<FCurveEditorTreeItemID>&& LocalChildren, FCurveEditor* CurveEditor)
{
	for (FCurveEditorTreeItemID ChildID : LocalChildren)
	{
		if (FCurveEditorTreeItem* ChildItem = Items.Find(ChildID))
		{
			// Destroy its curves while we know ChildItem is still a valid ptr
			ChildItem->DestroyCurves(CurveEditor);

			RemoveChildrenRecursive(MoveTemp(ChildItem->Children.ChildIDs), CurveEditor);

			Items.Remove(ChildID);
			Selection.Remove(ChildID);
		}
	}
}


bool FCurveEditorTree::FilterSpecificItems(TArrayView<FCurveEditorTreeFilter* const> FilterPtrs, TArrayView<const FCurveEditorTreeItemID> ItemsToFilter, ECurveEditorTreeFilterState InheritedState)
{
	bool bAnyMatched = false;

	for (FCurveEditorTreeItemID ItemID : ItemsToFilter)
	{
		const FCurveEditorTreeItem& TreeItem = GetItem(ItemID);

		// Retrieve the existing filter state for this item. This may have been set by preceeding filters.
		ECurveEditorTreeFilterState FilterState         = InheritedState;
		ECurveEditorTreeFilterState ChildInheritedState = InheritedState;

		// If this has already not been matched, run it through our filter
		TSharedPtr<ICurveEditorTreeItem> TreeItemImpl = TreeItem.GetItem();
		if (TreeItemImpl)
		{
			const bool bMatchesFilter = Algo::AnyOf(FilterPtrs, [TreeItemImpl](const FCurveEditorTreeFilter* Filter){ return TreeItemImpl->PassesFilter(Filter); });
			if (bMatchesFilter)
			{
				bAnyMatched = true;
				FilterState = ECurveEditorTreeFilterState::Match;
				ChildInheritedState = ECurveEditorTreeFilterState::ImplicitChild;
			}
		}

		// Run the filter on all child nodes
		const bool bMatchedChildren = FilterSpecificItems(FilterPtrs, TreeItem.GetChildren(), ChildInheritedState);

		// If we matched children we become an implicit parent if not already matched
		if (bMatchedChildren && FilterState != ECurveEditorTreeFilterState::Match)
		{
			bAnyMatched = true;
			FilterState = ECurveEditorTreeFilterState::ImplicitParent;
		}

		if (FilterState != ECurveEditorTreeFilterState::NoMatch)
		{
			FilterStates.SetFilterState(ItemID, FilterState);
		}
	}

	return bAnyMatched;
}

void FCurveEditorTree::RunFilters()
{
	checkf(!OnChangedEvent.IsBroadcastInProgress(), TEXT("Curve editor tree must not be manipulated in response to it changing"));

	FScopedCurveEditorTreeUpdateGuard BroadcastChangeUpdate(this);

	// Reset all the filter states back to the default
	FilterStates.Reset();

	if (Filters.Num())
	{
		FilterStates.Activate();

		TArray<FCurveEditorTreeFilter*> FilterPtrs;

		for (int32 Index = Filters.Num() - 1; Index >= 0; --Index)
		{
			TSharedPtr<FCurveEditorTreeFilter> Filter = Filters[Index].Pin();
			if (!Filter)
			{
				// Remove invalid filters
				Filters.RemoveAtSwap(Index, 1, false);
			}
			else
			{
				FilterPtrs.Add(Filter.Get());
			}
		}

		FilterSpecificItems(FilterPtrs, RootItems.ChildIDs, ECurveEditorTreeFilterState::NoMatch);
	}
	else
	{
		FilterStates.Deactivate();
	}
}

void FCurveEditorTree::AddFilter(TWeakPtr<FCurveEditorTreeFilter> NewFilter)
{
	Filters.AddUnique(NewFilter);
}

void FCurveEditorTree::RemoveFilter(TWeakPtr<FCurveEditorTreeFilter> FilterToRemove)
{
	Filters.Remove(FilterToRemove);
}

const FCurveEditorFilterStates& FCurveEditorTree::GetFilterStates() const
{
	return FilterStates;
}

ECurveEditorTreeFilterState FCurveEditorTree::GetFilterState(FCurveEditorTreeItemID InTreeItemID) const
{
	return FilterStates.Get(InTreeItemID);
}

void FCurveEditorTree::SetDirectSelection(TArray<FCurveEditorTreeItemID>&& TreeItems)
{
	Selection.Reset();

	// Recursively add child items
	int32 LastDirectlySelected = TreeItems.Num();
	for (int32 Index = 0; Index < TreeItems.Num(); ++Index)
	{
		FCurveEditorTreeItemID ItemID = TreeItems[Index];

		if (Index < LastDirectlySelected)
		{
			Selection.Add(ItemID, ECurveEditorTreeSelectionState::Explicit);
		}
		else
		{
			Selection.Add(ItemID, ECurveEditorTreeSelectionState::ImplicitChild);
		}

		for (FCurveEditorTreeItemID ChildID : Items.FindChecked(ItemID).GetChildren())
		{
			TreeItems.Add(ChildID);
		}
	}
}

const TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState>& FCurveEditorTree::GetSelection() const
{
	return Selection;
}

ECurveEditorTreeSelectionState FCurveEditorTree::GetSelectionState(FCurveEditorTreeItemID InTreeItemID) const
{
	const ECurveEditorTreeSelectionState* State = Selection.Find(InTreeItemID);
	return State ? *State : ECurveEditorTreeSelectionState::None;
}
