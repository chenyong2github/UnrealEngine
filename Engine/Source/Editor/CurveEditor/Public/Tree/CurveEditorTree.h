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
struct FCurveEditorTreeFilter;

class FCurveEditor;
class FCurveEditorTree;

/** Enumeration specifying how a specific tree item has matched the current set of filters */
enum class ECurveEditorTreeFilterState : uint8
{
	/** The item did not match any filter, and neither did any of its parents or children */
	NoMatch,

	/** Neither this item nor any of its children match filters, but one of its parents did (ie it resides within a matched item) */
	ImplicitChild,

	/** Neither this item nor any of its parents match the filters, but one of its descendant children did (ie it is a parent of a matched item) */
	ImplicitParent,

	/** This item itself matched one or more of the filters */
	Match,
};

/**
 * Scoped guard that will trigger the tree OnChanged event when all scoped guards have been exited
 */
struct CURVEEDITOR_API FScopedCurveEditorTreeUpdateGuard
{
	explicit FScopedCurveEditorTreeUpdateGuard(FCurveEditorTree* InTree);
	~FScopedCurveEditorTreeUpdateGuard();

	FScopedCurveEditorTreeUpdateGuard(FScopedCurveEditorTreeUpdateGuard&& RHS);
	FScopedCurveEditorTreeUpdateGuard& operator=(FScopedCurveEditorTreeUpdateGuard&& RHS);

private:
	FCurveEditorTree* Tree;
};

/**
 * Struct that represents an event for when the tree has been changed.
 * This type carefully only allows FScopedCurveEditorTreeUpdateGuard to broadcast the event, and makes special checks for re-entrancy
 */
struct FCurveEditorOnChangedEvent
{
	/**
	 * @return true if the event is currently being broadcast
	 */
	bool IsBroadcastInProgress() const
	{
		return bIsBroadcastInProgress;
	}

	/**
	 * Bind a new handler to this OnChanged event
	 */
	FDelegateHandle Bind(FSimpleDelegate&& Handler) { return Delegate.Add(MoveTemp(Handler)); }

	/**
	 * Unbind a previously bound handler from this event
	 */
	void Unbind(FDelegateHandle Handle) { return Delegate.Remove(Handle); }

private:

	friend FScopedCurveEditorTreeUpdateGuard;

	/** Safety check to ensure that invocations of OnChangedEvent are never re-entrant */
	bool bIsBroadcastInProgress = false;
	/** Counter that is incremented for each living instance of FCurveEditorOnChangedEvent */
	uint32 UpdateGuardCounter = 0;
	/** The actual multi-cast delegate */
	FSimpleMulticastDelegate Delegate;
};

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
 * Sparse map of filter states specifying items that have matched a filter
 */
struct FCurveEditorFilterStates
{
	/**
	 * Reset all the filter states currently being tracked (does not affect IsActive
	 */
	void Reset()
	{
		FilterStates.Reset();
	}

	/**
	 * Retrieve the filter state for a specific tree item ID
	 * @return The item's filter state, or ECurveEditorTreeFilterState::Match if filters are not currently active.
	 */
	ECurveEditorTreeFilterState Get(FCurveEditorTreeItemID ItemID) const
	{
		if (!bIsActive)
		{
			// If not active, everything is treated as having matched the (non-existent) filters
			return ECurveEditorTreeFilterState::Match;
		}

		const ECurveEditorTreeFilterState* State = FilterStates.Find(ItemID);
		return State ? *State : ECurveEditorTreeFilterState::NoMatch;
	}

	/**
	 * Assign a new filter state to an item
	 */
	void SetFilterState(FCurveEditorTreeItemID ItemID, ECurveEditorTreeFilterState NewState)
	{
		FilterStates.Add(ItemID, NewState);
	}

	/**
	 * Check whether filters are active or not
	 */
	bool IsActive() const
	{
		return bIsActive;
	}

	/**
	 * Activate the filters so that they begin to take effect
	 */
	void Activate()
	{
		bIsActive = true;
	}

	/**
	 * Deactivate the filters so that they no longer take effect
	 */
	void Deactivate()
	{
		bIsActive = false;
	}

private:

	/** Whether filters should be active or not */
	bool bIsActive = false;

	/** Filter state map. Items with no implicit or explicit filter state are not present */
	TMap<FCurveEditorTreeItemID, ECurveEditorTreeFilterState> FilterStates;
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
	 * Retrieve this curve editor's root items irrespective of filter state
	 */
	const TArray<FCurveEditorTreeItemID>& GetRootItems() const;

	/** 
	 * Retrieve all the items stored in this tree irrespective of filter state
	 */
	const TMap<FCurveEditorTreeItemID, FCurveEditorTreeItem>& GetAllItems() const;

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
	 * Run all the filters on this tree, updating filter state for all tree items
	 */
	void RunFilters();

	/**
	 * Add a new filter to this tree. Does not run the filter (and thus update any tree views) until RunFilters is called.
	 *
	 * @param NewFilter The new filter to add to this tree
	 */
	void AddFilter(TWeakPtr<FCurveEditorTreeFilter> NewFilter);

	/**
	 * Remove an existing filter from this tree. Does not re-run the filters (and thus update any tree views) until RunFilters is called.
	 *
	 * @param FilterToRemove The filter to remove from this tree
	 */
	void RemoveFilter(TWeakPtr<FCurveEditorTreeFilter> FilterToRemove);

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

	/**
	 * Access the filter state for this tree. Items that are neither implicitly or explicitly filtered-in are not present in the map.
	 */
	const FCurveEditorFilterStates& GetFilterStates() const;

	/**
	 * Check a specific tree item's filter state
	 */
	ECurveEditorTreeFilterState GetFilterState(FCurveEditorTreeItemID InTreeItemID) const;

	/**
	 * Retrieve this tree's on-changed event
	 */
	FCurveEditorOnChangedEvent* OnChanged()
	{
		return &OnChangedEvent;
	}

	/**
	 * Retrieve a scoped guard that will broadcast the on changed handlers for this tree when it goes out of scope (along with all other scoped guards on the stack)
	 * Can be used to defer such broadcasts in situations where many changes are made to the tree at a time.
	 */
	FScopedCurveEditorTreeUpdateGuard ScopedUpdateGuard()
	{
		return FScopedCurveEditorTreeUpdateGuard(this);
	}

	/**
	 * Add a handler for when this tree structure is changed in some way (items added/removed, tree filters changed etc)
	 */
	FDelegateHandle Bind_OnChanged(FSimpleDelegate&& Handler)
	{
		return OnChangedEvent.Bind(MoveTemp(Handler));
	}

	/**
	 * Remove a handler for when this tree structure is changed in some way (items added/removed, tree filters changed etc)
	 */
	void Unbind_OnChanged(FDelegateHandle Handle)
	{
		return OnChangedEvent.Unbind(Handle);
	}

private:

	// Recursively removes children without removing them from the parent (assuming the parent is also being removed)
	void RemoveChildrenRecursive(TArray<FCurveEditorTreeItemID>&& Children, FCurveEditor* CurveEditor);

	/**
	 * Run the specified filters over the specified items and their recursive children, storing the results in this instance's FilterStates struct.
	 *
	 * @param FilterPtrs     Array of non-null pointers to filters to use. Items are considered matched if they match any filter in this array.
	 * @param Items          Array item IDs to filter
	 * @param InheritedState The filter state for each item to receive if it does not directly match a filter (either ECurveEditorTreeFilterState::NoMatch or ECurveEditorTreeFilterState::InheritedChild)
	 * @return Whether any of the items or any their recursive children matched any filter
	 */
	bool FilterSpecificItems(TArrayView<FCurveEditorTreeFilter* const> FilterPtrs, TArrayView<const FCurveEditorTreeItemID> Items, ECurveEditorTreeFilterState InheritedState);

	/** Incrementing ID for the next tree item to be created */
	FCurveEditorTreeItemID NextTreeItemID;

	/** Container housing the machinery required for deferred broadcast of changes to the tree */
	FCurveEditorOnChangedEvent OnChangedEvent;

	/** Map of all tree items by their ID */
	TMap<FCurveEditorTreeItemID, FCurveEditorTreeItem> Items;

	/** Map of all tree items by their ID */
	TArray<TWeakPtr<FCurveEditorTreeFilter>> Filters;

	/** Hierarchical information for the tree */
	FSortedCurveEditorTreeItems RootItems;
	TMap<FCurveEditorTreeItemID, FSortedCurveEditorTreeItems> ChildItemIDs;

	/** Selection state map. Items with no implicit or explicit selection are not present */
	TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState> Selection;

	/** Filter state map. Items with no implicit or explicit filter state are not present */
	FCurveEditorFilterStates FilterStates;
};