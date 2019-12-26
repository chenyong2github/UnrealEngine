// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/ViewModels/StatsNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Enumerates types of grouping or sorting for the stats nodes. */
enum class EStatsGroupingMode
{
	/** Creates a single group for all timers. */
	Flat,

	/** Creates one group for one letter. */
	ByName,

	/** Creates groups based on stats metadata group names. */
	ByMetaGroupName,

	/** Creates one group for each stats type. */
	ByType,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

/** Type definition for shared pointers to instances of EStatsGroupingMode. */
typedef TSharedPtr<EStatsGroupingMode> EStatsGroupingModePtr;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains helper functions and classes for EStatsNodeType enum. */
struct StatsNodeTypeHelper
{
	/**
	 * @param Type - The value to get the text for.
	 *
	 * @return text representation of the specified EStatsNodeType value.
	 */
	static FText ToName(const EStatsNodeType Type);

	/**
	 * @param Type - The value to get the text for.
	 *
	 * @return text representation with more detailed explanation of the specified EStatsNodeType value.
	 */
	static FText ToDescription(const EStatsNodeType Type);

	/**
	 * @param Type - The value to get the brush name for.
	 *
	 * @return brush name of the specified EStatsNodeType value.
	 */
	static FName ToBrushName(const EStatsNodeType Type);

	static const FSlateBrush* GetIconForGroup();
	static const FSlateBrush* GetIconForStatsNodeType(const EStatsNodeType Type);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains grouping static functions and classes. */
struct StatsNodeGroupingHelper
{
	/**
	 * @param StatsGroupingMode - The value to get the text for.
	 *
	 * @return text representation of the specified EStatsGroupingMode value.
	 */
	static FText ToName(const EStatsGroupingMode StatsGroupingMode);

	/**
	 * @param StatsGroupingMode - The value to get the text for.
	 *
	 * @return text representation with more detailed explanation of the specified EStatsGroupingMode value.
	 */
	static FText ToDescription(const EStatsGroupingMode StatsGroupingMode);

	/**
	 * @param StatsGroupingMode - The value to get the brush name for.
	 *
	 * @return brush name of the specified EStatsGroupingMode value.
	 */
	static FName ToBrushName(const EStatsGroupingMode StatsGroupingMode);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains sorting static functions and classes. */
struct StatsNodeSortingHelper
{
	//////////////////////////////////////////////////
	// Sorting by stats name.

	struct ByNameAscending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const
		{
			return A->GetName().LexicalLess(B->GetName());
		}
	};

	struct ByNameDescending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const
		{
			return B->GetName().LexicalLess(A->GetName());
		}
	};

	//////////////////////////////////////////////////
	// Sorting by stats meta group name.
	// If meta group names are the same then sort by name.

	struct ByMetaGroupNameAscending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const
		{
			if (A->GetMetaGroupName() == B->GetMetaGroupName())
			{
				// Sort by stats name (ascending).
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
		FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const
		{
			if (A->GetMetaGroupName() == B->GetMetaGroupName())
			{
				// Sort by stats name (ascending).
				return A->GetName().LexicalLess(B->GetName());
			}
			else
			{
				return B->GetMetaGroupName().LexicalLess(A->GetMetaGroupName());
			}
		}
	};

	//////////////////////////////////////////////////
	// Sorting by stats type.
	// If types are the same then sort by name.

	struct ByTypeAscending
	{
		FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const
		{
			const EStatsNodeType& TypeA = A->GetType();
			const EStatsNodeType& TypeB = B->GetType();

			if (TypeA == TypeB)
			{
				// Sort by stats name (ascending).
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
		FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const
		{
			const EStatsNodeType& TypeA = A->GetType();
			const EStatsNodeType& TypeB = B->GetType();

			if (TypeA == TypeB)
			{
				// Sort by stats name (ascending).
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
			FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const \
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
			FORCEINLINE_DEBUGGABLE bool operator()(const FStatsNodePtr& A, const FStatsNodePtr& B) const \
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

	SORT_BY_STATS(uint64, ByCount, Count);
	SORT_BY_STATS(double, BySum, Sum);
	SORT_BY_STATS(double, ByMin, Min);
	SORT_BY_STATS(double, ByMax, Max);
	SORT_BY_STATS(double, ByAverage, Average);
	SORT_BY_STATS(double, ByMedian, Median);
	SORT_BY_STATS(double, ByLowerQuartile, LowerQuartile);
	SORT_BY_STATS(double, ByUpperQuartile, UpperQuartile);


	#undef SORT_BY_STATS_ASCENDING
	#undef SORT_BY_STATS_DESCENDING
	#undef SORT_BY_STATS

	//////////////////////////////////////////////////
};

////////////////////////////////////////////////////////////////////////////////////////////////////
