// Copyright Epic Games, Inc. All Rights Reserved.

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

void FStatsNode::SetAggregatedStatsDouble(uint64 InCount, const TAggregatedStats<double>& InAggregatedStats)
{
	AggregatedStats.Count = InCount;
	AggregatedStats.DoubleStats = InAggregatedStats;
	UpdateAggregatedStatsInt64FromDouble();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::SetAggregatedStatsInt64(uint64 InCount, const TAggregatedStats<int64>& InAggregatedStats)
{
	AggregatedStats.Count = InCount;
	AggregatedStats.Int64Stats = InAggregatedStats;
	UpdateAggregatedStatsDoubleFromInt64();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::UpdateAggregatedStatsInt64FromDouble()
{
	TAggregatedStats<int64>& Int64Stats = AggregatedStats.Int64Stats;
	TAggregatedStats<double>& DoubleStats = AggregatedStats.DoubleStats;

	Int64Stats.Sum           = static_cast<int64>(DoubleStats.Sum);
	Int64Stats.Min           = static_cast<int64>(DoubleStats.Min);
	Int64Stats.Max           = static_cast<int64>(DoubleStats.Max);
	Int64Stats.Average       = static_cast<int64>(DoubleStats.Average);
	Int64Stats.Median        = static_cast<int64>(DoubleStats.Median);
	Int64Stats.LowerQuartile = static_cast<int64>(DoubleStats.LowerQuartile);
	Int64Stats.UpperQuartile = static_cast<int64>(DoubleStats.UpperQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNode::UpdateAggregatedStatsDoubleFromInt64()
{
	TAggregatedStats<double>& DoubleStats = AggregatedStats.DoubleStats;
	TAggregatedStats<int64>& Int64Stats = AggregatedStats.Int64Stats;

	DoubleStats.Sum           = static_cast<double>(Int64Stats.Sum);
	DoubleStats.Min           = static_cast<double>(Int64Stats.Min);
	DoubleStats.Max           = static_cast<double>(Int64Stats.Max);
	DoubleStats.Average       = static_cast<double>(Int64Stats.Average);
	DoubleStats.Median        = static_cast<double>(Int64Stats.Median);
	DoubleStats.LowerQuartile = static_cast<double>(Int64Stats.LowerQuartile);
	DoubleStats.UpperQuartile = static_cast<double>(Int64Stats.UpperQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FStatsNodeDisplayHint
{
	static const FName Seconds;
	static const FName Bytes;
};

const FName FStatsNodeDisplayHint::Seconds(TEXT("Seconds"));
const FName FStatsNodeDisplayHint::Bytes(TEXT("Bytes"));

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FStatsNodeMetaGroupName
{
	static const FName Time;
	static const FName Memory;
};

const FName FStatsNodeMetaGroupName::Time(TEXT("Time"));
const FName FStatsNodeMetaGroupName::Memory(TEXT("Memory"));

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::FormatAggregatedStatsValue(double ValueDbl, int64 ValueInt) const
{
	if (AggregatedStats.Count > 0)
	{
		if (GetDataType() == EStatsNodeDataType::Double)
		{
			//TODO: if (GetDisplayHint() == FStatsNodeDisplayHint::Seconds)
			if (GetMetaGroupName() == FStatsNodeMetaGroupName::Time)
			{
				return FText::FromString(TimeUtils::FormatTimeAuto(ValueDbl));
			}
			else
			{
				return FText::AsNumber(ValueDbl);
			}
		}
		else
		if (GetDataType() == EStatsNodeDataType::Int64)
		{
			//TODO: if (GetDisplayHint() == FStatsNodeDisplayHint::Bytes)
			if (GetMetaGroupName() == FStatsNodeMetaGroupName::Memory)
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

	return LOCTEXT("AggregatedStatsNA", "N/A");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsSum() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.Sum, AggregatedStats.Int64Stats.Sum);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsMin() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.Min, AggregatedStats.Int64Stats.Min);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsMax() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.Max, AggregatedStats.Int64Stats.Max);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsAverage() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.Average, AggregatedStats.Int64Stats.Average);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsMedian() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.Median, AggregatedStats.Int64Stats.Median);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsLowerQuartile() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.LowerQuartile, AggregatedStats.Int64Stats.LowerQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FStatsNode::GetTextForAggregatedStatsUpperQuartile() const
{
	return FormatAggregatedStatsValue(AggregatedStats.DoubleStats.UpperQuartile, AggregatedStats.Int64Stats.UpperQuartile);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
