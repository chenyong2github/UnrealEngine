// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimerNode.h"

#define LOCTEXT_NAMESPACE "TimerNode"

const FName FTimerNode::TypeName(TEXT("FTimerNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNode::ResetAggregatedStats()
{
	AggregatedStats = Trace::FTimingProfilerAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNode::SetAggregatedStats(const Trace::FTimingProfilerAggregatedStats& InAggregatedStats)
{
	AggregatedStats = InAggregatedStats;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
