// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/AnalysisService.h"

class FTimerNode;

/** Type definition for shared pointers to instances of FTimerNode. */
typedef TSharedPtr<class FTimerNode> FTimerNodePtr;

/** Type definition for shared references to instances of FTimerNode. */
typedef TSharedRef<class FTimerNode> FTimerNodeRef;

/** Type definition for shared references to const instances of FTimerNode. */
typedef TSharedRef<const class FTimerNode> FTimerNodeRefConst;

/** Type definition for weak references to instances of FTimerNode. */
typedef TWeakPtr<class FTimerNode> FTimerNodeWeak;

enum class ETimerNodeType
{
	/** The TimerNode is a CPU Scope timer. */
	CpuScope,

	/** The TimerNode is a GPU Scope timer. */
	GpuScope,

	/** The TimerNode is a Compute Scope timer. */
	ComputeScope,

	/** The TimerNode is a group node. */
	Group,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

/**
 * Class used to store information about a timer node (used in timers' tree view).
 */
class FTimerNode : public TSharedFromThis<FTimerNode>
{
public:
	static const uint64 InvalidId = -1;

public:
	/** Initialization constructor for the timer node. */
	FTimerNode(uint64 InId, const FName InName, const FName InMetaGroupName, ETimerNodeType InType)
		: Id(InId)
		, Name(InName)
		, MetaGroupName(InMetaGroupName)
		, Type(InType)
		, bForceExpandGroupNode(false)
	{
		ResetAggregatedStats();
	}

	/** Initialization constructor for the group node. */
	FTimerNode(const FName InGroupName)
		: Id(0)
		, Name(InGroupName)
		, Type(ETimerNodeType::Group)
		, bForceExpandGroupNode(false)
	{
		ResetAggregatedStats();
	}

	/**
	 * @return an Id of this timer, valid only for timer nodes.
	 */
	const uint64 GetId() const
	{
		return Id;
	}

	/**
	 * @return a name of this node, group or timer.
	 */
	const FName& GetName() const
	{
		return Name;
	}

	/**
	 * @return a name of this node, group or timer + addditional info, to display in Timers tree view.
	*/
	const FText GetNameEx() const;

	/**
	 * @return a name of the group that this timer node belongs to, taken from the metadata.
	 */
	const FName& GetMetaGroupName() const
	{
		return MetaGroupName;
	}

	/**
	 * @return a type of this timer or ETimerNodeType::Group for group nodes.
	 */
	const ETimerNodeType& GetType() const
	{
		return Type;
	}

	/**
	 * @return true, if this node is a group node.
	 */
	bool IsGroup() const
	{
		return Type == ETimerNodeType::Group;
	}

	/**
	 * @return the aggregated stats for this timer.
	 */
	const Trace::FAggregatedTimingStats& GetAggregatedStats() const
	{
		return AggregatedStats;
	}

	void ResetAggregatedStats();

	void SetAggregatedStats(const Trace::FAggregatedTimingStats& AggregatedStats);

	/**
	 * @return a const reference to the child nodes of this group.
	 */
	FORCEINLINE_DEBUGGABLE const TArray<FTimerNodePtr>& GetChildren() const
	{
		return Children;
	}

	/**
	 * @return a const reference to the child nodes that should be visible to the UI based on filtering.
	 */
	FORCEINLINE_DEBUGGABLE const TArray<FTimerNodePtr>& GetFilteredChildren() const
	{
		return FilteredChildren;
	}

	/**
	 * @return a weak reference to the group of this timer node, may be invalid.
	 */
	FTimerNodeWeak GetGroupPtr() const
	{
		return GroupPtr;
	}

	/**
	 * @return a name of the fake group that this timer node belongs to.
	 */
	const FName& GetGroupName() const
	{
		return GroupPtr.Pin()->Name;
	}

	/**
	 * @return a name of the fake group that this timer node belongs to.
	 */
	//const FName& GetGroupNameSafe() const
	//{
	//	return GroupPtr.IsValid() ? GroupPtr.Pin()->Name : NAME_None;
	//}

	bool IsFiltered() const
	{
		return false; // TODO
	}

public:
	/** Sorts children using the specified class instance. */
	template<typename TSortingClass>
	void SortChildren(const TSortingClass& Instance)
	{
		Children.Sort(Instance);
	}

	/** Adds specified child to the children and sets group for it. */
	FORCEINLINE_DEBUGGABLE void AddChildAndSetGroupPtr(const FTimerNodePtr& ChildPtr)
	{
		ChildPtr->GroupPtr = AsShared();
		Children.Add(ChildPtr);
	}

	/** Adds specified child to the filtered children. */
	FORCEINLINE_DEBUGGABLE void AddFilteredChild(const FTimerNodePtr& ChildPtr)
	{
		FilteredChildren.Add(ChildPtr);
	}

	/** Clears filtered children. */
	void ClearFilteredChildren()
	{
		FilteredChildren.Reset();
	}

protected:
	/** The Id of this timer or group. */
	const uint64 Id;

	/** The name of this timer or group. */
	const FName Name;

	/** The name of the group that this timer belongs to, based on the timer's metadata; only valid for timer nodes. */
	const FName MetaGroupName;

	/** Holds the type of this timer; for the group, this is Group. */
	const ETimerNodeType Type;

	/** Aggregated stats. */
	Trace::FAggregatedTimingStats AggregatedStats;

	/** Children of this node. */
	TArray<FTimerNodePtr> Children;

	/** Filtered children of this node. */
	TArray<FTimerNodePtr> FilteredChildren;

	/** A weak pointer to the group/parent of this node. */
	FTimerNodeWeak GroupPtr;

public:
	/** Whether this group node should be expanded when the text filtering is enabled. */
	bool bForceExpandGroupNode;
};
