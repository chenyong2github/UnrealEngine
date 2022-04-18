// Copyright Epic Games, Inc. All Rights Reserved.

#include "HistoryEdition/HistoryAnalysis.h"

#include "HistoryEdition/ActivityGraphIDs.h"
#include "HistoryEdition/DependencyGraphBuilder.h"

#include "Containers/Queue.h"

namespace UE::ConcertSyncCore
{
	FHistoryDeletionRequirements AnalyseActivityDeletion(const TSet<FActivityID>& ActivitiesToDelete, const FConcertSyncSessionDatabase& Database, bool bAddActivitiesToDelete)
	{
		const FActivityDependencyGraph Graph = BuildDependencyGraphFrom(Database);
		return AnalyseActivityDeletion(ActivitiesToDelete, Graph, bAddActivitiesToDelete);
	}
	
	FHistoryDeletionRequirements AnalyseActivityDeletion(const TSet<FActivityID>& ActivitiesToDelete, const FActivityDependencyGraph& DependencyGraph, bool bAddActivitiesToDelete)
	{
		FHistoryDeletionRequirements Result;

		TSet<FActivityNodeID> HardDoubleEnqueuingProtection;
		TQueue<FActivityNodeID> HardDependencyActivitiesToAnalyse;
		for (const FActivityID ActivityToDelete : ActivitiesToDelete)
		{
			const TOptional<FActivityNodeID> NodeID = DependencyGraph.FindNodeByActivity(ActivityToDelete);
			if (ensureMsgf(NodeID, TEXT("Graph does not correspond to ActivitiesToDelete")))
			{
				HardDoubleEnqueuingProtection.Add(*NodeID);
				HardDependencyActivitiesToAnalyse.Enqueue(*NodeID);
			}
		}

		TSet<FActivityNodeID> PossibleDoubleEnqueuingProtection;
		TQueue<FActivityNodeID> PossibleDependencyActivitiesToAnalyse;
		FActivityNodeID CurrentActivityID;

		/* We check the hard dependencies first.
		 * Why? Example:
		 *
		 *		R
		 *	   / \
		 *	  A   B
		 *	   \ /
		 *	    C
		 *
		 * The edges C -> A -> R are possible dependencies.
		 * The edges C -> B -> R are hard dependencies.
		 *
		 * Now: delete R.
		 * We want C to be marked has a hard dependency.
		 */
		while (HardDependencyActivitiesToAnalyse.Dequeue(CurrentActivityID))
		{
			const FActivityNode& ActivityNode = DependencyGraph.GetNodeById(CurrentActivityID);

			const FActivityID ActivityID = ActivityNode.GetActivityId();
			if (bAddActivitiesToDelete || !ActivitiesToDelete.Contains(ActivityID))
			{
				Result.HardDependencies.Add(ActivityID);
			}
			
			for (const FActivityNodeID& ChildID : ActivityNode.GetAffectedChildren())
			{
				const FActivityNode& ChildNode = DependencyGraph.GetNodeById(ChildID);

				// Performance: The below iterates the edge list twice but usually there will 1 or 2 entries
				if (ChildNode.DependsOnActivity(ActivityID, DependencyGraph, {}, EDependencyStrength::HardDependency))
				{
					if (!HardDoubleEnqueuingProtection.Contains(ChildID))
					{
						HardDoubleEnqueuingProtection.Add(ChildID);
						HardDependencyActivitiesToAnalyse.Enqueue(ChildID);
					}
				}
				else if (ChildNode.DependsOnActivity(ActivityID, DependencyGraph, {}, EDependencyStrength::PossibleDependency))
				{
					if (!HardDoubleEnqueuingProtection.Contains(ChildID) && !PossibleDoubleEnqueuingProtection.Contains(ChildID))
					{
						PossibleDoubleEnqueuingProtection.Add(ChildID);
						PossibleDependencyActivitiesToAnalyse.Enqueue(ChildID);
					}
				}
			}
		}

		// Any possible dependencies that are not also hard dependencies can be added now
		while (PossibleDependencyActivitiesToAnalyse.Dequeue(CurrentActivityID))
		{
			// This would imply a hard dependency - hard dependency takes precedence over possible dependency
			if (HardDoubleEnqueuingProtection.Contains(CurrentActivityID))
			{
				continue;
			}
			
			const FActivityNode& ActivityNode = DependencyGraph.GetNodeById(CurrentActivityID);
			const FActivityID ActivityID = ActivityNode.GetActivityId();
			Result.PossibleDependencies.Add(ActivityNode.GetActivityId());
			for (const FActivityNodeID& ChildID : ActivityNode.GetAffectedChildren())
			{
				const FActivityNode& ChildNode = DependencyGraph.GetNodeById(ChildID);
				if (ChildNode.DependsOnActivity(ActivityID, DependencyGraph, {}, EDependencyStrength::PossibleDependency)
					&& !HardDoubleEnqueuingProtection.Contains(ChildID)
					&& !PossibleDoubleEnqueuingProtection.Contains(ChildID))
				{
					PossibleDoubleEnqueuingProtection.Add(ChildID);
					PossibleDependencyActivitiesToAnalyse.Enqueue(ChildID);
				}
			}
		}

		return Result;
	}
}


