// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimerNode.h"

#include "Insights/ViewModels/TimingEvent.h"

#define LOCTEXT_NAMESPACE "TimerNode"

const FName FTimerNode::TypeName(TEXT("FTimerNode"));

const FName FTimerNode::GpuGroup(TEXT("GPU"));
const FName FTimerNode::CpuGroup(TEXT("CPU"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNode::FTimerNode(uint32 InTimerId, const TCHAR* InName, ETimerNodeType InType)
	: FBaseTreeNode(FName(InName), InType == ETimerNodeType::Group)
	, TimerId(InTimerId)
	, MetaGroupName(InType == ETimerNodeType::CpuScope ? CpuGroup : InType == ETimerNodeType::GpuScope ? GpuGroup : NAME_None)
	, Type(InType)
	, bIsAddedToGraph(false)
	, bIsHotPath(false)
{
	uint32 Color32 = FTimingEvent::ComputeEventColor(InName);
	Color.R = ((Color32 >> 16) & 0xFF) / 255.0f;
	Color.G = ((Color32 >>  8) & 0xFF) / 255.0f;
	Color.B = ((Color32      ) & 0xFF) / 255.0f;
	Color.A = 1.0;

	ResetAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Initialization constructor for the group node. */
FTimerNode::FTimerNode(const FName InGroupName)
	: FBaseTreeNode(InGroupName, true)
	, TimerId(InvalidTimerId)
	, Type(ETimerNodeType::Group)
	, Color(0.0, 0.0, 0.0, 1.0)
	, bIsAddedToGraph(false)
	, bIsHotPath(false)
{
	ResetAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNode::~FTimerNode()
{
}

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
