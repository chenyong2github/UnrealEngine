// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/TypeHash.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Templates/SharedPointer.h"
#include "Delegates/Delegate.h"
#include "CurveEditorTypes.h"

struct ICurveEditorTreeItem;

class FCurveEditor;
class FCurveEditorTree;

/**
 * Container specifying a linear set of child identifiers and 
 */
struct FSortedCurveEditorTreeItems
{
	/** (default: false) Whether the child ID array needs re-sorting or not */
	bool bRequiresSort = false;

	/** Sorted list of child IDs */
	TArray<FCurveEditorTreeItemID> ChildIDs;
};

/**
 * Concrete type used as a tree item for the curve editor. No need to derive from this type - custom behaviour is implemented through ICurveEditorTreeItem.
 * Implemented in this way to ensure that all hierarchical information can be reasoned about within the curve editor itself, and allow for mixing of tree item types from any usage domain.
 */
struct CURVEEDITOR_API FCurveEditorTreeItem
{
	/** @return This item's unique identifier within the tree */
	FCurveEditorTreeItemID GetID() const
	{
		return ThisID;
	}

	/** @return This parent's unique identifier within the tree, or FCurveEditorTreeItemID::Invalid for root items */
	FCurveEditorTreeItemID GetParentID() const
	{
		return ParentID;
	}

	/**
	 * Access the sorted list of children for this item
	 *
	 * @return An array view to this item's children
	 */
	TArrayView<const FCurveEditorTreeItemID> GetChildren() const
	{
		return Children.ChildIDs;
	}

	/**
	 * Access the user-specified implementation for this tree item
	 * @return A strong pointer to the implementation or null if it has expired, or was never assigned
	 */
	TSharedPtr<ICurveEditorTreeItem> GetItem() const
	{
		return StrongItemImpl.IsValid() ? StrongItemImpl : WeakItemImpl.Pin();
	}

	/**
	 * Overwrite this item's implementation with an externally held implementation to this tree item. Does not hold a strong reference.
	 */
	void SetWeakItem(TWeakPtr<ICurveEditorTreeItem> InItem)
	{
		WeakItemImpl = InItem;
		StrongItemImpl = nullptr;
	}

	/**
	 * Overwrite this item's implementation, holding a strong reference to it for the lifetime of this tree item.
	 */
	void SetStrongItem(TSharedPtr<ICurveEditorTreeItem> InItem)
	{
		WeakItemImpl = nullptr;
		StrongItemImpl = InItem;
	}

	/**
	 * Get all the curves currently represented by this tree item. Items may not be created until the tree item has been selected
	 */
	TArrayView<const FCurveModelID> GetCurves() const
	{
		return Curves;
	}

	/**
	 * Retrieve all the curves for this tree item, creating them through ICurveEditorTreeItem::CreateCurveModels if there are none
	 */
	TArrayView<const FCurveModelID> GetOrCreateCurves(FCurveEditor* CurveEditor);

	/**
	 * Destroy any previously constructed curve models that this tree item owns
	 */
	void DestroyCurves(FCurveEditor* CurveEditor);

	/**
	 * Destroy any previously constructed unpinned curve models that this tree item owns
	 */
	void DestroyUnpinnedCurves(FCurveEditor* CurveEditor);

private:

	friend FCurveEditorTree;

	/** This item's ID */
	FCurveEditorTreeItemID ThisID;
	/** This parent's ID or FCurveEditorTreeItemID::Invalid() for root nodes */
	FCurveEditorTreeItemID ParentID;
	/** A weak pointer to an externally held implementation. Mutually exclusive to StrongItemImpl. */
	TWeakPtr<ICurveEditorTreeItem> WeakItemImpl;
	/** A strong pointer to an implementation for this tree item. Mutually exclusive to WeakItemImpl. */
	TSharedPtr<ICurveEditorTreeItem> StrongItemImpl;
	/** All the curves currently added to the curve editor from this tree item. */
	TArray<FCurveModelID, TInlineAllocator<1>> Curves;
	/** This item's sorted children. */
	FSortedCurveEditorTreeItems Children;
};


/** 
 * Complete implementation of a curve editor tree. Only really defines the hierarchy and selection states for tree items.
 */
class FCurveEditorTree
{
public:

	FCurveEditorTree();

	/**
	 * Retrieve an item from its ID, assuming it is definitely valid
	 */
	FCurveEditorTreeItem& GetItem(FCurveEditorTreeItemID ItemID);

	/**
	 * Retrieve an item from its ID, assuming it is definitely valid
	 */
	const FCurveEditorTreeItem& GetItem(FCurveEditorTreeItemID ItemID) const;

	/**
	 * Retrieve an item from its ID or nullptr if the ID is not valid
	 */
	FCurveEditorTreeItem* FindItem(FCurveEditorTreeItemID ItemID);

	/** 
	 * Retrieve this curve editor's root items
	 */
	const TArray<FCurveEditorTreeItemID>& GetRootItems() const;

	/**
	 * Add a new empty item to the tree
	 *
	 * @param ParentID The ID of the desired parent for the new item, or FCurveEditorTreeItemID::Invalid for root nodes
	 */
	FCurveEditorTreeItem* AddItem(FCurveEditorTreeItemID ParentID);

	/**
	 * Remove an item and all its children from this tree, destroying any curves it may have created.
	 *
	 * @param ItemID The ID of the item to remove
	 * @param CurveEditor (required) Pointer to the curve editor that owns this tree to remove curves from
	 */
	void RemoveItem(FCurveEditorTreeItemID ItemID, FCurveEditor* CurveEditor);

	/**
	 * Access a delegate that is executed whenever this tree's hiararchy has changed in some way.
	 */
	FSimpleMulticastDelegate& OnChanged()
	{
		return OnChangedEvent;
	}

	/**
	 * Inform this tree that the specified tree item IDs have been directly selected on the UI.
	 * @note: This populates both implicit and explicit selection state for the supplied items and any children/parents
	 */
	void SetDirectSelection(TArray<FCurveEditorTreeItemID>&& TreeItems);

	/**
	 * Access the selection state for this tree. Items that are neither implicitly or explicitly selected are not present in the map.
	 */
	const TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState>& GetSelection() const;

	/**
	 * Check a specific tree item's selection state
	 */
	ECurveEditorTreeSelectionState GetSelectionState(FCurveEditorTreeItemID InTreeItemID) const;

private:

	// Recursively removes children without removing them from the parent (assuming the parent is also being removed)
	void RemoveChildrenRecursive(TArray<FCurveEditorTreeItemID>&& Children, FCurveEditor* CurveEditor);

	/** Safety check to ensure that invocations of OnChangedEvent are never re-entrant */
	bool bReentrantDelegate;

	/** Incrememnting ID for the next tree item to be created */
	FCurveEditorTreeItemID NextTreeItemID;

	FSimpleMulticastDelegate OnChangedEvent;

	/** Map of all tree items by their ID */
	TMap<FCurveEditorTreeItemID, FCurveEditorTreeItem> Items;

	/** Hierarchical information for the tree */
	FSortedCurveEditorTreeItems RootItems;
	TMap<FCurveEditorTreeItemID, FSortedCurveEditorTreeItems> ChildItemIDs;

	/** Selection state map. Items with no implicit or explicit selection are not present */
	TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState> Selection;
};