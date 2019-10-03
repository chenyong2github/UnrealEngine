// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/NetProfiler.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ENetEventNodeType
{
	/** The NetEventNode is a Net Event. */
	NetEvent,

	/** The NetEventNode is a Net Object. */
	//NetObject,

	/** The NetEventNode is a group node. */
	Group,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FNetEventNode;

/** Type definition for shared pointers to instances of FNetEventNode. */
typedef TSharedPtr<class FNetEventNode> FNetEventNodePtr;

/** Type definition for shared references to instances of FNetEventNode. */
typedef TSharedRef<class FNetEventNode> FNetEventNodeRef;

/** Type definition for shared references to const instances of FNetEventNode. */
typedef TSharedRef<const class FNetEventNode> FNetEventNodeRefConst;

/** Type definition for weak references to instances of FNetEventNode. */
typedef TWeakPtr<class FNetEventNode> FNetEventNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class used to store information about a timer node (used in the STimersView).
 */
class FNetEventNode : public Insights::FBaseTreeNode
{
public:
	static const FName TypeName;

public:
	/** Initialization constructor for the NetEvent node. */
	FNetEventNode(uint64 InId, const FName InName, ENetEventNodeType InType, uint32 InLevel)
		: FBaseTreeNode(InId, InName, InType == ENetEventNodeType::Group)
		, Type(InType)
		, Level(InLevel)
	{
		ResetAggregatedStats();
	}

	/** Initialization constructor for the group node. */
	FNetEventNode(const FName InGroupName)
		: FBaseTreeNode(0, InGroupName, true)
		, Type(ENetEventNodeType::Group)
		, Level(0)
	{
		ResetAggregatedStats();
	}

	virtual const FName& GetTypeName() const override { return TypeName; }

	/**
	 * @return a type of this NetEvent node or ENetEventNodeType::Group for group nodes.
	 */
	const ENetEventNodeType& GetType() const { return Type; }

	void SetLevel(uint32 InLevel) const { return ; }
	uint32 GetLevel() const { return Level; }

	/**
	 * @return the aggregated stats for this NetEvent node.
	 */
	const Trace::FNetProfilerAggregatedStats& GetAggregatedStats() const { return AggregatedStats; }

	void ResetAggregatedStats();
	void SetAggregatedStats(const Trace::FNetProfilerAggregatedStats& AggregatedStats);

private:
	/** Holds the type of this NetEvent node. */
	const ENetEventNodeType Type;

	uint32 Level;

	/** Aggregated stats. */
	Trace::FNetProfilerAggregatedStats AggregatedStats;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
