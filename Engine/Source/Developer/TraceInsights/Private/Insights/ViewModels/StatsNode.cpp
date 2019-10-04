// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StatsNode.h"

// Insights
#include "Insights/Common/TimeUtils.h"

#define LOCTEXT_NAMESPACE "StatsNode"

const FName FStatsNode::TypeName(TEXT("FStatsNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::ResetAggregatedStats()
{
	AggregatedStats.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::SetAggregatedStats(FAggregatedStats& InAggregatedStats)
{
	AggregatedStats = InAggregatedStats;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::ResetAggregatedIntegerStats()
{
	AggregatedIntegerStats.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::SetAggregatedIntegerStats(FAggregatedIntegerStats& InAggregatedIntegerStats)
{
	AggregatedIntegerStats = InAggregatedIntegerStats;

	// Sorting and display of Count value uses the "float" stats.
	AggregatedStats.Count = AggregatedIntegerStats.Count;

	AggregatedStats.Sum           = static_cast<double>(AggregatedIntegerStats.Sum);
	AggregatedStats.Min           = static_cast<double>(AggregatedIntegerStats.Min);
	AggregatedStats.Max           = static_cast<double>(AggregatedIntegerStats.Max);
	AggregatedStats.Average       = static_cast<double>(AggregatedIntegerStats.Average);
	AggregatedStats.Median        = static_cast<double>(AggregatedIntegerStats.Median);
	AggregatedStats.LowerQuartile = static_cast<double>(AggregatedIntegerStats.LowerQuartile);
	AggregatedStats.UpperQuartile = static_cast<double>(AggregatedIntegerStats.UpperQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::FormatAggregatedStatsValue(double ValueDbl, int64 ValueInt) const
{
	if (GetType() == EStatsNodeType::Float)
	{
		if (AggregatedStats.Count == 0)
		{
			return LOCTEXT("AggregatedStatsNA", "N/A");
		}
		else
		{
			//TODO: if (GetDisplayHint() == FName(TEXT("Seconds")))
			if (GetMetaGroupName() == FName(TEXT("Time")))
			{
				return FText::FromString(TimeUtils::FormatTimeAuto(ValueDbl));
			}
			else
			{
				return FText::AsNumber(ValueDbl);
			}
		}
	}
	else
	{
		if (AggregatedIntegerStats.Count == 0)
		{
			return LOCTEXT("AggregatedStatsNA", "N/A");
		}
		else
		{
			//TODO: if (GetDisplayHint() == FName(TEXT("Bytes")))
			if (GetMetaGroupName() == FName(TEXT("Memory")))
			{
				if (ValueInt > 0)
				{
					return FText::AsMemory(ValueInt);
				}
				else if (ValueInt == 0)
				{
					return FText::FromString(TEXT("0"));
				}
				else
				{
					return FText::FromString(FString::Printf(TEXT("-%s"), *FText::AsMemory(-ValueInt).ToString()));
				}
			}
			else
			{
				return FText::AsNumber(ValueInt);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsSum() const
{
	return FormatAggregatedStatsValue(AggregatedStats.Sum, AggregatedIntegerStats.Sum);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsMin() const
{
	return FormatAggregatedStatsValue(AggregatedStats.Min, AggregatedIntegerStats.Min);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsMax() const
{
	return FormatAggregatedStatsValue(AggregatedStats.Max, AggregatedIntegerStats.Max);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsAverage() const
{
	return FormatAggregatedStatsValue(AggregatedStats.Average, AggregatedIntegerStats.Average);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsMedian() const
{
	return FormatAggregatedStatsValue(AggregatedStats.Median, AggregatedIntegerStats.Median);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsLowerQuartile() const
{
	return FormatAggregatedStatsValue(AggregatedStats.LowerQuartile, AggregatedIntegerStats.LowerQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsUpperQuartile() const
{
	return FormatAggregatedStatsValue(AggregatedStats.UpperQuartile, AggregatedIntegerStats.UpperQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
