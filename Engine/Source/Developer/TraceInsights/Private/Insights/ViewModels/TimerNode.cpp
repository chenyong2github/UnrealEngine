// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimerNode.h"

#define LOCTEXT_NAMESPACE "TimerNode"

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNode::ResetAggregatedStats()
{
	AggregatedStats = Trace::FAggregatedTimingStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNode::SetAggregatedStats(const Trace::FAggregatedTimingStats& InAggregatedStats)
{
	AggregatedStats = InAggregatedStats;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FTimerNode::GetNameEx() const
{
	FText Text = FText::GetEmpty();

	if (IsGroup())
	{
		const int32 NumChildren = Children.Num();
		const int32 NumFilteredChildren = FilteredChildren.Num();

		if (NumFilteredChildren == NumChildren)
		{
			Text = FText::Format(LOCTEXT("TimerNodeGroupTextFmt1", "{0} ({1})"), FText::FromName(Name), FText::AsNumber(NumChildren));
		}
		else
		{
			Text = FText::Format(LOCTEXT("TimerNodeGroupTextFmt2", "{0} ({1} / {2})"), FText::FromName(Name), FText::AsNumber(NumFilteredChildren), FText::AsNumber(NumChildren));
		}
	}
	else
	{
		//const bool bIsStatTracked = FProfilerManager::Get()->IsStatTracked(GroupOrStatNode->GetStatID());
		//if (bIsStatTracked)
		//{
		//	Text = FText::Format(LOCTEXT("TimerNodeTrackedTextFmt", "{0}*"), FText::FromName(Name));
		//}
		//else
		{
			Text = FText::FromName(Name);
		}
	}

	return Text;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
