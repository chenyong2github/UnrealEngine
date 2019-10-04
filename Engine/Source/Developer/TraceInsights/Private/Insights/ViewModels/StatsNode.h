// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////////////////////////

class FStatsNode;

/** Type definition for shared pointers to instances of FStatsNode. */
typedef TSharedPtr<class FStatsNode> FStatsNodePtr;

/** Type definition for shared references to instances of FStatsNode. */
typedef TSharedRef<class FStatsNode> FStatsNodeRef;

/** Type definition for shared references to const instances of FStatsNode. */
typedef TSharedRef<const class FStatsNode> FStatsNodeRefConst;

/** Type definition for weak references to instances of FStatsNode. */
typedef TWeakPtr<class FStatsNode> FStatsNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class used to store information about a stats counter node (used in SStatsView).
 */
class FStatsNode : public Insights::FBaseTreeNode
{
public:
	static const FName TypeName;
	static const uint64 InvalidId = -1;

public:
	/** Initialization constructor for the stats node. */
	FStatsNode(uint64 InId, const FName InName, const FName InMetaGroupName, EStatsNodeType InType)
		: FBaseTreeNode(InId, InName, InType == EStatsNodeType::Group)
		, MetaGroupName(InMetaGroupName)
		, Type(InType)
		, bIsAddedToGraph(false)
	{
		const uint32 HashColor = GetId() * 0x2c2c57ed;
		Color.R = ((HashColor >> 16) & 0xFF) / 255.0f;
		Color.G = ((HashColor >> 8) & 0xFF) / 255.0f;
		Color.B = ((HashColor) & 0xFF) / 255.0f;
		Color.A = 1.0;

		ResetAggregatedStats();
	}

	/** Initialization constructor for the group node. */
	FStatsNode(const FName InGroupName)
		: FBaseTreeNode(0, InGroupName, true)
		, Type(EStatsNodeType::Group)
		, Color(0.0, 0.0, 0.0, 1.0)
		, bIsAddedToGraph(false)
	{
		ResetAggregatedStats();
	}

	virtual const FName& GetTypeName() const override { return TypeName; }

	/**
	 * @return a name of the meta group that this stats node belongs to, taken from the metadata.
	 */
	const FName& GetMetaGroupName() const
	{
		return MetaGroupName;
	}

	/**
	 * @return a type of this stats node or EStatsNodeType::Group for group nodes.
	 */
	const EStatsNodeType& GetType() const
	{
		return Type;
	}

	/**
	 * @return color of the node. Used when showing a graph series for a stats counter.
	 */
	FLinearColor GetColor() const
	{
		return Color;
	}

	/**
	 * @return the aggregated stats of this stats counter (if counter is a "float number" type).
	 */
	const FAggregatedStats& GetAggregatedStats() const
	{
		return AggregatedStats;
	}

	void ResetAggregatedStats();

	void SetAggregatedStats(FAggregatedStats& AggregatedStats);

	/**
	 * @return the aggregated stats of this stats counter (if counter is an "integer number" type).
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

	/** Sorts children using the specified class instance. */
	template<typename TSortingClass>
	void SortChildren(const TSortingClass& Instance)
	{
		auto Projection = [](Insights::FBaseTreeNodePtr Node) -> FStatsNodePtr
		{
			return StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(Node);
		};
		Algo::SortBy(GetChildrenMutable(), Projection, Instance);
	}

	bool IsAddedToGraph() const
	{
		return bIsAddedToGraph;
	}

	void SetAddedToGraphFlag(bool bOnOff)
	{
		bIsAddedToGraph = bOnOff;
	}

private:
	/** The name of the meta group that this stats counter belongs to, based on the stats' metadata; only valid for stats counter nodes. */
	const FName MetaGroupName;

	/** Holds the type of this stats counter. */
	const EStatsNodeType Type;

	/** Color of the node. */
	FLinearColor Color;

	bool bIsAddedToGraph;

	/** Aggregated stats (double). */
	FAggregatedStats AggregatedStats;

	/** Aggregated stats (int64). */
	FAggregatedIntegerStats AggregatedIntegerStats;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
