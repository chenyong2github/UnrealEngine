// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tree/CurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "CurveEditor.h"

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

FCurveEditorTree::FCurveEditorTree()
{
	bReentrantDelegate = false;
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

FCurveEditorTreeItem* FCurveEditorTree::AddItem(FCurveEditorTreeItemID ParentID)
{
	checkf(!bReentrantDelegate, TEXT("Curve editor tree must not be manipulated in response to it changing"));
	TGuardValue<bool> Guard(bReentrantDelegate, true);

	FCurveEditorTreeItemID NewItemID = NextTreeItemID;

	FCurveEditorTreeItem& NewItem = Items.Add(NewItemID);
	NewItem.ThisID   = NewItemID;
	NewItem.ParentID = ParentID;

	FSortedCurveEditorTreeItems& ParentContainer = ParentID.IsValid() ? Items.FindChecked(ParentID).Children : RootItems;
	ParentContainer.ChildIDs.Add(NewItemID);
	ParentContainer.bRequiresSort = true;

	++NextTreeItemID.Value;

	OnChangedEvent.Broadcast();
	return &NewItem;
}

void FCurveEditorTree::RemoveItem(FCurveEditorTreeItemID ItemID, FCurveEditor* CurveEditor)
{
	checkf(!bReentrantDelegate, TEXT("Curve editor tree must not be manipulated in response to it changing"));
	TGuardValue<bool> Guard(bReentrantDelegate, true);

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

	OnChangedEvent.Broadcast();
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