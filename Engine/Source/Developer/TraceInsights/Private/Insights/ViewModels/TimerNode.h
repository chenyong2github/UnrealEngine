// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Containers/Timelines.h"
#include "TraceServices/Model/TimingProfiler.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETimerNodeType
{
	/** The TimerNode is a CPU Scope timer. */
	CpuScope,

	/** The TimerNode is a GPU Scope timer. */
	GpuScope,

	/** The TimerNode is a Compute Scope timer. */
	ComputeScope,

	/** The TimerNode is a group node. */
	Group,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerNode;

/** Type definition for shared pointers to instances of FTimerNode. */
typedef TSharedPtr<class FTimerNode> FTimerNodePtr;

/** Type definition for shared references to instances of FTimerNode. */
typedef TSharedRef<class FTimerNode> FTimerNodeRef;

/** Type definition for shared references to const instances of FTimerNode. */
typedef TSharedRef<const class FTimerNode> FTimerNodeRefConst;

/** Type definition for weak references to instances of FTimerNode. */
typedef TWeakPtr<class FTimerNode> FTimerNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class used to store information about a timer node (used in the STimersView).
 */
class FTimerNode : public Insights::FBaseTreeNode
{
public:
	static const FName TypeName;
	//static constexpr uint64 InvalidTimerId = -1;

public:
	/** Initialization constructor for the timer node. */
	FTimerNode(uint64 InId, const FName InName, const FName InMetaGroupName, ETimerNodeType InType)
		: FBaseTreeNode(InId, InName, InType == ETimerNodeType::Group)
		, MetaGroupName(InMetaGroupName)
		, Type(InType)
		, bIsHotPath(false)
	{
		ResetAggregatedStats();
	}

	/** Initialization constructor for the group node. */
	FTimerNode(const FName InGroupName)
		: FBaseTreeNode(0, InGroupName, true)
		, Type(ETimerNodeType::Group)
		, bIsHotPath(false)
	{
		ResetAggregatedStats();
	}

	virtual const FName& GetTypeName() const override { return TypeName; }

	/**
	 * @return a name of the meta group that this timer node belongs to, taken from the metadata.
	 */
	const FName& GetMetaGroupName() const { return MetaGroupName; }

	/**
	 * @return a type of this timer node or ETimerNodeType::Group for group nodes.
	 */
	const ETimerNodeType& GetType() const { return Type; }

	/**
	 * @return the aggregated stats for this timer.
	 */
	const Trace::FTimingProfilerAggregatedStats& GetAggregatedStats() const { return AggregatedStats; }

	void ResetAggregatedStats();
	void SetAggregatedStats(const Trace::FTimingProfilerAggregatedStats& AggregatedStats);

	bool IsHotPath() const { return bIsHotPath; }
	void SetIsHotPath(bool bOnOff) { bIsHotPath = bOnOff; }

private:
	/** The name of the meta group that this timer belongs to, based on the timer's metadata; only valid for timer nodes. */
	const FName MetaGroupName;

	/** Holds the type of this timer. */
	const ETimerNodeType Type;

	/** Aggregated stats. */
	Trace::FTimingProfilerAggregatedStats AggregatedStats;

	/** True if this tree node is on the hot path. */
	bool bIsHotPath;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
