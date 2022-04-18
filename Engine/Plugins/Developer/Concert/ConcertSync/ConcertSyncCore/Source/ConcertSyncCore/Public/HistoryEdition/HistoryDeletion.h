// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HistoryAnalysis.h"

class IConcertServer;

namespace UE::ConcertSyncCore
{
	struct FDeleteSessionErrorResult
	{
		TOptional<FText> ErrorMessage;

		explicit FDeleteSessionErrorResult(TOptional<FText> ErrorMessage = {})
			: ErrorMessage(ErrorMessage)
		{}

		static FDeleteSessionErrorResult MakeSuccess() { return FDeleteSessionErrorResult(); }
		static FDeleteSessionErrorResult MakeError(FText Error) { return FDeleteSessionErrorResult(MoveTemp(Error)); }

		bool WasSuccessful() const { return !ErrorMessage.IsSet(); }
		bool HadError() const { return !WasSuccessful(); }
	};

	/** Utility functions that converts FHistoryDeletionRequirements into a single TSet. */
	CONCERTSYNCCORE_API TSet<FActivityID> CombineRequirements(const FHistoryDeletionRequirements& ToDelete);

	/**
	 * Deletes the given sessions in ToDelete from ArchivedSessionDatabase.
	 *
	 * There is no direct functionality for removing activities in the database; the operation is as follows:
	 *	1. Restore the archived session with a session filter
	 *	2. Delete the archived session
	 *	3. Archive the live session created in step 1 with the old guid.
	 */
	CONCERTSYNCCORE_API FDeleteSessionErrorResult DeleteActivitiesInArchivedSession(const TSharedRef<IConcertServer>& Server, const FGuid& SessionToDeleteFrom, const TSet<FActivityID>& ToDelete);
}
