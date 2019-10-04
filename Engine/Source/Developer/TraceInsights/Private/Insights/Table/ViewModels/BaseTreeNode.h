// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Insights
{

class ITableCellValueSorter;

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
public:
	static constexpr int32 InvalidId = -1;

public:
	/** Initialization constructor for the node. */
	FBaseTreeNode(uint64 InId, const FName InName, bool bInIsGroup)
		: Id(InId)
		, Name(InName)
		, bIsGroup(bInIsGroup)
		, bIsExpanded(false)
	{
	}

	virtual ~FBaseTreeNode()
	{
	}

	virtual const FName& GetTypeName() const = 0;

	/**
	 * @return an id of this node.
	 */
	const int32 GetId() const
	{
		return Id;
	}

	/**
	 * @return a name of this node.
	 */
	const FName& GetName() const
	{
		return Name;
	}

	/**
	 * @return a name of this node to display in tree view. Ex.: group nodes may include additional info, like the number of visible/total children.
	 */
	virtual const FText GetDisplayName() const;

	/**
	 * @return true, if this node is a group node.
	 */
	bool IsGroup() const
	{
		return bIsGroup;
	}

	/**
	 * @return a const reference to the child nodes of this group.
	 */
	FORCEINLINE_DEBUGGABLE const TArray<FBaseTreeNodePtr>& GetChildren() const
	{
		return Children;
	}

	/**
	 * @return a const reference to the child nodes that should be visible to the UI based on filtering.
	 */
	FORCEINLINE_DEBUGGABLE const TArray<FBaseTreeNodePtr>& GetFilteredChildren() const
	{
		return FilteredChildren;
	}

	/**
	 * @return a weak reference to the group of this node, may be invalid.
	 */
	FBaseTreeNodeWeak GetGroupPtr() const
	{
		return GroupPtr;
	}

	/**
	 * @return a name of the group node that this node belongs to.
	 */
	const FName& GetGroupName() const
	{
		return GroupPtr.Pin()->GetName();
	}

	virtual bool IsFiltered() const
	{
		return false;
	}

	void SortChildrenAscending(const ITableCellValueSorter& Sorter);
	void SortChildrenDescending(const ITableCellValueSorter& Sorter);

	/** Adds specified child to the children and sets group for it. */
	FORCEINLINE_DEBUGGABLE void AddChildAndSetGroupPtr(const FBaseTreeNodePtr& ChildPtr)
	{
		ChildPtr->GroupPtr = AsShared();
		Children.Add(ChildPtr);
	}

	/** Clears children. */
	void ClearChildren()
	{
		for (FBaseTreeNodePtr& NodePtr : Children)
		{
			NodePtr->GroupPtr = nullptr;
		}
		Children.Reset();
	}

	/** Adds specified child to the filtered children. */
	FORCEINLINE_DEBUGGABLE void AddFilteredChild(const FBaseTreeNodePtr& ChildPtr)
	{
		FilteredChildren.Add(ChildPtr);
	}

	/** Clears filtered children. */
	void ClearFilteredChildren()
	{
		FilteredChildren.Reset();
	}

	bool IsExpanded() const { return bIsExpanded; }
	void SetExpansion(bool bOnOff) { bIsExpanded = bOnOff; }

protected:
	/**
	 * @return a reference to the child nodes of this group.
	 */
	FORCEINLINE_DEBUGGABLE TArray<FBaseTreeNodePtr>& GetChildrenMutable()
	{
		return Children;
	}

private:
	/** The id of this node. */
	const int32 Id;

	/** The name of this node. */
	const FName Name;

	/** Children of this node. */
	TArray<FBaseTreeNodePtr> Children;

	/** Filtered children of this node. */
	TArray<FBaseTreeNodePtr> FilteredChildren;

	/** A weak pointer to the group/parent of this node. */
	FBaseTreeNodeWeak GroupPtr;

	/** If this node is a group node or not. */
	bool bIsGroup;

	/** Whether this group node should be expanded or not. */
	bool bIsExpanded;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
