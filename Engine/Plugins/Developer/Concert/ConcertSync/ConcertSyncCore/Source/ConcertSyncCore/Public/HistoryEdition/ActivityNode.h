// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActivityDependencyEdge.h"
#include "ActivityGraphIDs.h"
#include "Misc/Optional.h"
#include "Templates/NonNullPointer.h"

namespace UE::ConcertSyncCore
{
	class FActivityDependencyGraph;

	/** A node corresponds to an activity and can depend on preceding activity*/
	class CONCERTSYNCCORE_API FActivityNode
	{
		friend FActivityDependencyGraph;
	public:
		
		FActivityNode(int64 ActivityId, FActivityNodeID NodeIndex)
			: ActivityId(ActivityId)
			, NodeIndex(NodeIndex)
		{}

		bool HasAnyDependency() const { return Dependencies.Num() > 0; }
		bool DependsOnNode(FActivityNodeID NodeId, TOptional<EActivityDependencyReason> WithReason = {}, TOptional<EDependencyStrength> WithStrength = {}) const; 
		bool DependsOnActivity(int64 InActivityId, const FActivityDependencyGraph& Graph, TOptional<EActivityDependencyReason> WithReason = {}, TOptional<EDependencyStrength> WithStrength = {}) const;
		
		bool AffectsAnyActivity() const { return GetAffectedChildren().Num() > 0; }
		bool AffectsNode(FActivityNodeID NodeID) const { return AffectedChildren.Contains(NodeID); }
		bool AffectsActivity(int64 InActivityId, const FActivityDependencyGraph& Graph) const;
		
		int64 GetActivityId() const { return ActivityId; }
		FActivityNodeID GetNodeIndex() const { return NodeIndex; }
		const TArray<FActivityDependencyEdge>& GetDependencies() const { return Dependencies; }
		const TArray<FActivityNodeID>& GetAffectedChildren() const { return AffectedChildren; }
	
	private:
	
		/** The activity this node corresponds to */
		const int64 ActivityId;

		/** Index in parent tree's node array (to avoid bad memory footprint) */
		const FActivityNodeID NodeIndex;

		/** This node's parents. Unset implies this is the root node. */
		TArray<FActivityDependencyEdge> Dependencies;

		/**
		 * This node's children. Children have dependencies to this node.
		 * Activities can only depend on activities that precede them.
		 * Since activity IDs are historic, every child's activity ID > ActivityId.
		 */
		TArray<FActivityNodeID> AffectedChildren;
	};
}