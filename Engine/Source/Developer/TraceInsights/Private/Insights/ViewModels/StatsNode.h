// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FStatsNode;

/** Type definition for shared pointers to instances of FStatsNode. */
typedef TSharedPtr<class FStatsNode> FStatsNodePtr;

/** Type definition for shared references to instances of FStatsNode. */
typedef TSharedRef<class FStatsNode> FStatsNodeRef;

/** Type definition for shared references to const instances of FStatsNode. */
typedef TSharedRef<const class FStatsNode> FStatsNodeRefConst;

/** Type definition for weak references to instances of FStatsNode. */
typedef TWeakPtr<class FStatsNode> FStatsNodeWeak;

enum class EStatsNodeType
{
	/** The StatsNode is a floating number stats. */
	Float,

	/** The StatsNode is an integer number stats. */
	Int64,

	/** The StatsNode is a group node. */
	Group,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

template<typename Type>
struct TAggregatedStats
{
	uint64 Count; /** Number of values. */

	Type Sum; /** Sum of all values. */
	Type Min; /** Min value. */
	Type Max; /** Max value. */
	Type Average; /** Average value. */
	Type Median; /** Median value. */
	Type LowerQuartile; /** Lower Quartile value. */
	Type UpperQuartile; /** Upper Quartile value. */

	TAggregatedStats()
		: Count(0)
		, Sum(0)
		, Min(0)
		, Max(0)
		, Average(0)
		, Median(0)
		, LowerQuartile(0)
		, UpperQuartile(0)
	{
	}

	void Reset()
	{
		Count = 0;
		Sum = 0;
		Min = 0;
		Max = 0;
		Average = 0;
		Median = 0;
		LowerQuartile = 0;
		UpperQuartile = 0;
	}
};

typedef TAggregatedStats<double> FAggregatedStats;
typedef TAggregatedStats<int64> FAggregatedIntegerStats;

/**
 * Class used to store information about a timer node (used in timers' tree view).
 */
class FStatsNode : public TSharedFromThis<FStatsNode>
{
public:
	static const uint64 InvalidId = -1;

public:
	/** Initialization constructor for the timer node. */
	FStatsNode(uint64 InId, const FName InName, const FName InMetaGroupName, EStatsNodeType InType)
		: Id(InId)
		, Name(InName)
		, MetaGroupName(InMetaGroupName)
		, Type(InType)
		, bForceExpandGroupNode(false)
	{
		ResetAggregatedStats();
	}

	/** Initialization constructor for the group node. */
	FStatsNode(const FName InGroupName)
		: Id(0)
		, Name(InGroupName)
		, Type(EStatsNodeType::Group)
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
	 * @return a name of this node, group or timer + addditional info, to display in Stats tree view.
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
	 * @return a type of this timer or EStatsNodeType::Group for group nodes.
	 */
	const EStatsNodeType& GetType() const
	{
		return Type;
	}

	/**
	 * @return true, if this node is a group node.
	 */
	bool IsGroup() const
	{
		return Type == EStatsNodeType::Group;
	}

	/**
	 * @return the aggregated stats of this counter (if counter is a "float number" type).
	 */
	const FAggregatedStats& GetAggregatedStats() const
	{
		return AggregatedStats;
	}

	void ResetAggregatedStats();

	void SetAggregatedStats(FAggregatedStats& AggregatedStats);

	/**
	 * @return the aggregated stats of this counter  (if counter is an "integer number" type).
	 */
	const FAggregatedIntegerStats& GetAggregatedIntegerStats() const
	{
		return AggregatedIntegerStats;
	}

	void ResetAggregatedIntegerStats();

	void SetAggregatedIntegerStats(FAggregatedIntegerStats& AggregatedIntegerStats);
	
	const FText FormatValue(double Value) const;
	const FText FormatValue(int64 Value) const;

	const FText FormatAggregatedStatsValue(double ValueDbl, int64 ValueInt) const;

	const FText GetTextForAggregatedStatsSum() const;
	const FText GetTextForAggregatedStatsMin() const;
	const FText GetTextForAggregatedStatsMax() const;
	const FText GetTextForAggregatedStatsAverage() const;
	const FText GetTextForAggregatedStatsMedian() const;
	const FText GetTextForAggregatedStatsLowerQuartile() const;
	const FText GetTextForAggregatedStatsUpperQuartile() const;

	/**
	 * @return a const reference to the child nodes of this group.
	 */
	FORCEINLINE_DEBUGGABLE const TArray<FStatsNodePtr>& GetChildren() const
	{
		return Children;
	}

	/**
	 * @return a const reference to the child nodes that should be visible to the UI based on filtering.
	 */
	FORCEINLINE_DEBUGGABLE const TArray<FStatsNodePtr>& GetFilteredChildren() const
	{
		return FilteredChildren;
	}

	/**
	 * @return a weak reference to the group of this timer node, may be invalid.
	 */
	FStatsNodeWeak GetGroupPtr() const
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
	FORCEINLINE_DEBUGGABLE void AddChildAndSetGroupPtr(const FStatsNodePtr& ChildPtr)
	{
		ChildPtr->GroupPtr = AsShared();
		Children.Add(ChildPtr);
	}

	/** Adds specified child to the filtered children. */
	FORCEINLINE_DEBUGGABLE void AddFilteredChild(const FStatsNodePtr& ChildPtr)
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
	const EStatsNodeType Type;

	/** Aggregated stats (double). */
	FAggregatedStats AggregatedStats;

	/** Aggregated stats (int64). */
	FAggregatedIntegerStats AggregatedIntegerStats;

	/** Children of this node. */
	TArray<FStatsNodePtr> Children;

	/** Filtered children of this node. */
	TArray<FStatsNodePtr> FilteredChildren;

	/** A weak pointer to the group/parent of this node. */
	FStatsNodeWeak GroupPtr;

public:
	/** Whether this group node should be expanded when the text filtering is enabled. */
	bool bForceExpandGroupNode;
};
