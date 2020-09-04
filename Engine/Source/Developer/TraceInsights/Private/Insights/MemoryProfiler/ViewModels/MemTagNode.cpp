// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemTagNode.h"

// Insights
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"

#define LOCTEXT_NAMESPACE "MemTagNode"

const FName FMemTagNode::TypeName(TEXT("FMemTagNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemTagNode::GetTrackerText() const
{
	uint64 Trackers = GetTrackers();
	if (Trackers == 0)
	{
		return FText::GetEmpty();
	}

	FMemorySharedState* SharedState = FMemoryProfilerManager::Get()->GetSharedState();
	if (SharedState)
	{
		return FText::FromString(SharedState->TrackersToString(Trackers, TEXT(", ")));
	}

	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemTagNode::ResetAggregatedStats()
{
	//AggregatedStats = Trace::FMemoryProfilerAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/*
void FMemTagNode::SetAggregatedStats(const Trace::FMemoryProfilerAggregatedStats& InAggregatedStats)
{
	AggregatedStats = InAggregatedStats;
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
