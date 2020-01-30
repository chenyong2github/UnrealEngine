// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetEventNode.h"

#define LOCTEXT_NAMESPACE "NetEventNode"

const FName FNetEventNode::TypeName(TEXT("FNetEventNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetEventNode::ResetAggregatedStats()
{
	AggregatedStats = Trace::FNetProfilerAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetEventNode::SetAggregatedStats(const Trace::FNetProfilerAggregatedStats& InAggregatedStats)
{
	AggregatedStats = InAggregatedStats;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
