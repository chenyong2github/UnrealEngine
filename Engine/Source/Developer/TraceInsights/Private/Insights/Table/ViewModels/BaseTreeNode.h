// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Templates/Function.h"

#include "Insights/Common/SimpleRtti.h"

struct FSlateBrush;

namespace Insights
{

class ITableCellValueSorter;
enum class ESortMode;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FBaseTreeNode;

/** Type definition for shared pointers to instances of FBaseTreeNode. */
typedef TSharedPtr<class FBaseTreeNode> FBaseTreeNodePtr;

/** Type definition for shared references to instances of FBaseTreeNode. */
typedef TSharedRef<class FBaseTreeNode> FBaseTreeNodeRef;

/** Type definition for shared references to const instances of FBaseTreeNode. */
typedef TSharedRef<const class FBaseTreeNode> FBaseTreeNodeRefConst;

/** Type definition for weak references to instances of FBaseTreeNode. */
typedef TWeakPtr<class FBaseTreeNode> FBaseTreeNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FBaseTreeNode : public TSharedFromThis<FBaseTreeNode>
{
	INSIGHTS_DECLARE_RTTI_BASE(FBaseTreeNode)

protected:
	struct FGroupNodeData
	{
		/** Children of the group node. */
		TArray<FBaseTreeNodePtr> Children;

		/** Filtered children of the group node. */
		TArray<FBaseTreeNodePtr> FilteredChildren;

		/** Descriptive text for the group node to display in a tooltip. */
		FText Tooltip;

		/** A generic pointer to a data context for the group node. Can be nullptr. */
		void* Context = nullptr;

		/** Whether the group node should be expanded or not. */
		bool bIsExpanded = false;
	};

public:
	/** Initialization constructor for the node. */
	explicit FBaseTreeNode(const FName InName, bool bInIsGroup)
		: DefaultSortOrder(0)
		, Name(InName)
		, GroupData(bInIsGroup ? new FGroupNodeData() : &DefaultGroupData)
	{
	}

	virtual ~FBaseTreeNode()
	{
		if (GroupData != &DefaultGroupData)
		{
			delete GroupData;
			GroupData = nullptr;
		}
	}

	uint32 GetDefaultSortOrder() const
	{
		return DefaultSortOrder;
	}
	void SetDefaultSortOrder(uint32 Order)
	{
		DefaultSortOrder = Order;
	}

	/**
	 * @returns a name of this node.
	 */
	const FName& GetName() const
	{
		return Name;
	}

	/**
	 * @returns a name of this node to display in tree view.
	 */
	virtual const FText GetDisplayName() const;

	/**
	 * @returns a name suffix for this node to display in tree view. Ex.: group nodes may include additional info, like the number of visible / total children.
	 */
	virtual const FText GetExtraDisplayName() const;

	/**
	 * @returns true if the node has an extra display name (a name suffix to display in tree view).
	 */
	virtual bool HasExtraDisplayName() const;

	/**
	 * @returns a descriptive text for this node to display in a tooltip.
	 */
	virtual const FText GetTooltip() const
	{
		return GroupData->Tooltip;
	}

	/**
	 * Sets a descriptive text for this node to display in a tooltip.
	 */
	virtual void SetTooltip(const FText& InTooltip)
	{
		if (IsGroup())
		{
			GroupData->Tooltip = InTooltip;
		}
	}

	/**
	 * @returns the default icon for a group/leaf node.
	 */
	static const FSlateBrush* GetDefaultIcon(bool bIsGroupNode);

	/**
	 * @returns a brush icon for this node.
	 */
	virtual const FSlateBrush* GetIcon() const
	{
		return GetDefaultIcon(IsGroup());
	}

	/**
	 * @returns the default color tint for a group/leaf node.
	 */
	static FLinearColor GetDefaultColor(bool bIsGroupNode);

	/**
	 * @returns the color tint to be used for the icon and the name text of this node.
	 */
	virtual FLinearColor GetColor() const
	{
		return GetDefaultColor(IsGroup());
	}

	/**
	 * @returns a pointer to a data context for this node. Only available for group nodes.
	 */
	virtual void* GetContext() const
	{
		return GroupData->Context;
	}

	/**
	 * Sets a pointer to a data context for this node. Only available for group nodes.
	 */
	virtual void SetContext(void* InContext)
	{
		if (IsGroup())
		{
			GroupData->Context = InContext;
		}
	}

	/**
	 * @returns true if this node is a group node.
	 */
	bool IsGroup() const
	{
		return GroupData != &DefaultGroupData;
	}

	/**
	 * Initializes the group data, allowing this node to accept children.
	 */
	void InitGroupData()
	{
		if (GroupData == &DefaultGroupData)
		{
			GroupData = new FGroupNodeData();
		}
	}

	/**
	 * @returns the number of children nodes.
	 */
	int32 GetChildrenCount() const
	{
		return GroupData->Children.Num();
	}

	/**
	 * @returns the number of filtered children nodes.
	 */
	int32 GetFilteredChildrenCount() const
	{
		return GroupData->FilteredChildren.Num();
	}

	/**
	 * Enumerates the children nodes.
	 */
	void EnumerateChildren(TFunction<bool(const FBaseTreeNodePtr&)> Callback) const
	{
		for (const FBaseTreeNodePtr& ChildNode : GroupData->Children)
		{
			if (!Callback(ChildNode))
			{
				break;
			}
		}
	}

	/**
	 * Enumerates the filtered children nodes.
	 */
	void EnumerateFilteredChildren(TFunction<bool(const FBaseTreeNodePtr&)> Callback) const
	{
		for (const FBaseTreeNodePtr& ChildNode : GroupData->FilteredChildren)
		{
			if (!Callback(ChildNode))
			{
				break;
			}
		}
	}

	/**
	 * @returns a const reference to the children nodes of this group.
	 */
	const TArray<FBaseTreeNodePtr>& GetChildren() const
	{
		return GroupData->Children;
	}

	/**
	 * @returns a const reference to the children nodes that should be visible to the UI based on filtering.
	 */
	const TArray<FBaseTreeNodePtr>& GetFilteredChildren() const
	{
		return GroupData->FilteredChildren;
	}

	/**
	 * @returns a weak reference to the group of this node, may be invalid.
	 */
	FBaseTreeNodeWeak GetGroupPtr() const
	{
		return GroupPtr;
	}

	/**
	 * @returns a weak reference to the group of this node, may be invalid.
	 */
	FBaseTreeNodePtr GetParentNode() const
	{
		return GroupPtr.Pin();
	}

	virtual bool IsFiltered() const
	{
		return false;
	}

	void SortChildren(const ITableCellValueSorter& Sorter, ESortMode SortMode);
	void SortFilteredChildren(const ITableCellValueSorter& Sorter, ESortMode SortMode);

	template <typename PredicateType>
	void SortChildren(PredicateType Predicate)
	{
		GroupData->Children.Sort(Predicate);
	}

	template <typename PredicateType>
	void SortFilteredChildren(PredicateType Predicate)
	{
		GroupData->FilteredChildren.Sort(Predicate);
	}

	/** Adds specified node to the children nodes. Also sets the current node as the group of the specified node. */
	void AddChildAndSetGroupPtr(const FBaseTreeNodePtr& ChildPtr)
	{
		ChildPtr->GroupPtr = AsWeak();
		GroupData->Children.Add(ChildPtr);
	}

	void SetGroupPtrForAllChildren()
	{
		FBaseTreeNodeWeak ThisNode = AsWeak();
		for (FBaseTreeNodePtr& NodePtr : GroupData->Children)
		{
			NodePtr->GroupPtr = ThisNode;
		}
	}

	void ResetGroupPtrForAllChildren()
	{
		for (FBaseTreeNodePtr& NodePtr : GroupData->Children)
		{
			NodePtr->GroupPtr = nullptr;
		}
	}

	/** Clears children. */
	void ClearChildren(int32 NewSize = 0)
	{
		ResetGroupPtrForAllChildren();
		GroupData->Children.Reset(NewSize);
	}

	void SwapChildren(TArray<FBaseTreeNodePtr>& NewChildren)
	{
		ResetGroupPtrForAllChildren();
		Swap(NewChildren, GroupData->Children);
		SetGroupPtrForAllChildren();
	}

	void SwapChildrenFast(TArray<FBaseTreeNodePtr>& NewChildren)
	{
		Swap(NewChildren, GroupData->Children);
	}

	/** Adds specified child to the filtered children nodes. */
	void AddFilteredChild(const FBaseTreeNodePtr& ChildPtr)
	{
		GroupData->FilteredChildren.Add(ChildPtr);
	}

	/** Clears the filtered children nodes. */
	void ClearFilteredChildren(int32 NewSize = 0)
	{
		GroupData->FilteredChildren.Reset(NewSize);
	}

	/**
	 * Resets the filtered children nodes.
	 * The filtered array of children nodes will be initialized with the unfiltered array of children.
	 */
	void ResetFilteredChildren()
	{
		const int32 Size = GroupData->Children.Num();
		GroupData->FilteredChildren.Reset();
		GroupData->FilteredChildren.Append(GroupData->Children);
	}

	/**
	 * Resets the filtered children for this node and also recursively for all children nodes.
	 * The filtered array of children nodes will be initialized with the unfiltered array of children.
	 */
	void ResetFilteredChildrenRec()
	{
		this->ResetFilteredChildren();
		for (FBaseTreeNodePtr Node : GroupData->Children)
		{
			Node->ResetFilteredChildrenRec();
		}
	}

	bool IsExpanded() const
	{
		return GroupData->bIsExpanded;
	}

	void SetExpansion(bool bOnOff)
	{
		if (IsGroup())
		{
			GroupData->bIsExpanded = bOnOff;
		}
	}

protected:
	/**
	 * @returns a reference to the children nodes of this group.
	 */
	TArray<FBaseTreeNodePtr>& GetChildrenMutable()
	{
		return GroupData->Children;
	}

	void SetGroupPtr(FBaseTreeNodePtr InGroupPtr)
	{
		GroupPtr = InGroupPtr;
	}

private:
	/** The default sort order. Index used to optimize sorting. */
	int32 DefaultSortOrder;

	/** The name of this node. */
	const FName Name;

	/** A weak pointer to the group/parent of this node. */
	FBaseTreeNodeWeak GroupPtr;

	/** The struct containing properties of a group node. It is allocated only for group nodes. */
	FGroupNodeData* GroupData;

	/** The only group data for "not a group" nodes. */
	static FGroupNodeData DefaultGroupData;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
