// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActivityDependencyGraph.h"
#include "ConcertMessageData.h"

class FConcertSyncSessionDatabase;

namespace UE::ConcertSyncCore
{
	/** Describes the activities that must be considered when deleting an activity */
	struct CONCERTSYNCCORE_API FHistoryEditionArgs
	{
		/** The activities that must be removed. */
		TSet<FActivityID> HardDependencies;
		/**
		 * The activities may want to be removed. It's not certain that they are affected (but it should be safe to keep them in).
		 * This will not contain any elements in HardDependencies.
		 */
		TSet<FActivityID> PossibleDependencies;

		FHistoryEditionArgs(TSet<FActivityID> HardDependencies = {}, TSet<FActivityID> PossibleDependencies = {})
			: HardDependencies(MoveTemp(HardDependencies)),
			  PossibleDependencies(MoveTemp(PossibleDependencies))
		{}
	};

	/** Utility function for one-off operations: just computes the dependency graph before calling AnalyseActivityDeletion. */
	CONCERTSYNCCORE_API FHistoryEditionArgs AnalyseActivityDependencies(const TSet<FActivityID>& ActivitiesToDelete, const FConcertSyncSessionDatabase& Database, bool bAddActivitiesToDelete = false);

	/**
	 * Given a set of activities to be edited (e.g. deleted or muted), returns which activities 1. must be and 2. may want to be considered in addition.
	 * @param ActivitiesToEdit The activities that should be removed
	 * @param DependencyGraph The graph encoding the activity dependencies
	 * @param bAddEditedAsHardDependencies Whether to add ActivitiesToEdit to the result's HardDependencies
	 * @return The activities to consider as well if ActivitiesToEdit are edited; ActivitiesToEdit is included in HardDependencies if bAddActivitiesToDelete == true.
	 */
	CONCERTSYNCCORE_API FHistoryEditionArgs AnalyseActivityDependencies(const TSet<FActivityID>& ActivitiesToEdit, const FActivityDependencyGraph& DependencyGraph, bool bAddEditedAsHardDependencies = false);
}

