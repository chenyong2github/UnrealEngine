// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimerGroupingAndSorting.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains helper functions and classes for ETimerNodeType enum. */
struct TimerNodeTypeHelper
{
	/**
	 * @param Type - The value to get the text for.
	 *
	 * @return text representation of the specified ETimerNodeType value.
	 */
	static FText ToName(const ETimerNodeType Type);

	/**
	 * @param Type - The value to get the text for.
	 *
	 * @return text representation with more detailed explanation of the specified ETimerNodeType value.
	 */
	static FText ToDescription(const ETimerNodeType Type);

	/**
	 * @param Type - The value to get the brush name for.
	 *
	 * @return brush name of the specified ETimerNodeType value.
	 */
	static FName ToBrushName(const ETimerNodeType Type);

	static const FSlateBrush* GetIconForGroup();
	static const FSlateBrush* GetIconForTimerNodeType(const ETimerNodeType Type);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains grouping static functions and classes. */
struct TimerNodeGroupingHelper
{
	/**
	 * @param TimerGroupingMode - The value to get the text for.
	 *
	 * @return text representation of the specified ETimerGroupingMode value.
	 */
	static FText ToName(const ETimerGroupingMode TimerGroupingMode);

	/**
	 * @param TimerGroupingMode - The value to get the text for.
	 *
	 * @return text representation with more detailed explanation of the specified ETimerGroupingMode value.
	 */
	static FText ToDescription(const ETimerGroupingMode TimerGroupingMode);

	/**
	 * @param TimerGroupingMode - The value to get the brush name for.
	 *
	 * @return brush name of the specified ETimerGroupingMode value.
	 */
	static FName ToBrushName(const ETimerGroupingMode TimerGroupingMode);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains sorting static functions and classes. */
struct TimerNodeSortingHelper
{
	//////////////////////////////////////////////////
	// Sorting by timer's name.

	struct ByNameAscending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const
		{
			return A->GetName().LexicalLess(B->GetName());
		}
	};

	struct ByNameDescending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const
		{
			return B->GetName().LexicalLess(A->GetName());
		}
	};

	//////////////////////////////////////////////////
	// Sorting by timer's meta group name.
	// If meta group names are the same then sort by name.

	struct ByMetaGroupNameAscending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const
		{
			if (A->GetMetaGroupName() == B->GetMetaGroupName())
			{
				// Sort by timer name (ascending).
				return A->GetName().LexicalLess(B->GetName());
			}
			else
			{
				return A->GetMetaGroupName().LexicalLess(B->GetMetaGroupName());
			}
		}
	};

	struct ByMetaGroupNameDescending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const
		{
			if (A->GetMetaGroupName() == B->GetMetaGroupName())
			{
				// Sort by timer name (ascending).
				return A->GetName().LexicalLess(B->GetName());
			}
			else
			{
				return B->GetMetaGroupName().LexicalLess(A->GetMetaGroupName());
			}
		}
	};

	//////////////////////////////////////////////////
	// Sorting by timer's type.
	// If types are the same then sort by name.

	struct ByTypeAscending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const
		{
			const ETimerNodeType& TypeA = A->GetType();
			const ETimerNodeType& TypeB = B->GetType();

			if (TypeA == TypeB)
			{
				// Sort by timer name (ascending).
				return A->GetName().LexicalLess(B->GetName());
			}
			else
			{
				return TypeA < TypeB;
			}
		}
	};

	struct ByTypeDescending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const
		{
			const ETimerNodeType& TypeA = A->GetType();
			const ETimerNodeType& TypeB = B->GetType();

			if (TypeA == TypeB)
			{
				// Sort by timer name (ascending).
				return A->GetName().LexicalLess(B->GetName());
			}
			else
			{
				return TypeA > TypeB;
			}
		}
	};

	//////////////////////////////////////////////////
	// Sorting by an aggregated stats value.
	// If aggregated stats values are the same then sort by name.

	#define SORT_BY_STATS_ASCENDING(Type, SortName, AggregatedStatsMember) \
		struct SortName \
		{ \
			FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const \
			{ \
				const Type ValueA = A->GetAggregatedStats().AggregatedStatsMember; \
				const Type ValueB = B->GetAggregatedStats().AggregatedStatsMember; \
				\
				if (ValueA == ValueB) \
				{ \
					/* Sort by name (ascending). */ \
					return A->GetName().LexicalLess(B->GetName()); \
				} \
				else \
				{ \
					return ValueA < ValueB; \
				} \
			} \
		};

	#define SORT_BY_STATS_DESCENDING(Type, SortName, AggregatedStatsMember) \
		struct SortName \
		{ \
			FORCEINLINE_DEBUGGABLE bool operator()(const FTimerNodePtr& A, const FTimerNodePtr& B) const \
			{ \
				const Type ValueA = A->GetAggregatedStats().AggregatedStatsMember; \
				const Type ValueB = B->GetAggregatedStats().AggregatedStatsMember; \
				\
				if (ValueA == ValueB) \
				{ \
					/* Sort by name (ascending). */ \
					return A->GetName().LexicalLess(B->GetName()); \
				} \
				else \
				{ \
					return ValueA > ValueB; \
				} \
			} \
		};

	#define SORT_BY_STATS(Type, SortName, AggregatedStatsMember) \
		SORT_BY_STATS_ASCENDING(Type, SortName##Ascending, AggregatedStatsMember) \
		SORT_BY_STATS_DESCENDING(Type, SortName##Descending, AggregatedStatsMember)

	SORT_BY_STATS(uint64, ByInstanceCount, InstanceCount);

	SORT_BY_STATS(double, ByTotalInclusiveTime, TotalInclusiveTime);
	SORT_BY_STATS(double, ByMinInclusiveTime, MinInclusiveTime);
	SORT_BY_STATS(double, ByMaxInclusiveTime, MaxInclusiveTime);
	SORT_BY_STATS(double, ByAverageInclusiveTime, AverageInclusiveTime);
	SORT_BY_STATS(double, ByMedianInclusiveTime, MedianInclusiveTime);

	SORT_BY_STATS(double, ByTotalExclusiveTime, TotalExclusiveTime);
	SORT_BY_STATS(double, ByMinExclusiveTime, MinExclusiveTime);
	SORT_BY_STATS(double, ByMaxExclusiveTime, MaxExclusiveTime);
	SORT_BY_STATS(double, ByAverageExclusiveTime, AverageExclusiveTime);
	SORT_BY_STATS(double, ByMedianExclusiveTime, MedianExclusiveTime);

	#undef SORT_BY_STATS_ASCENDING
	#undef SORT_BY_STATS_DESCENDING
	#undef SORT_BY_STATS

	//////////////////////////////////////////////////
};

////////////////////////////////////////////////////////////////////////////////////////////////////
