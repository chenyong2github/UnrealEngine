// Copyright Epic Games, Inc. All Rights Reserved.

#include "HistoryEdition/HistoryDeletion.h"

#include "IConcertServer.h"

#include "Misc/ScopeExit.h"

namespace UE::ConcertSyncCore
{
	static FConcertSessionFilter BuildFilterFrom(const TSet<FActivityID>& ToDelete);

	TSet<FActivityID> CombineRequirements(const FHistoryDeletionRequirements& ToDelete)
	{
		TSet<FActivityID> Result;
		for (const FActivityID ActivityID : ToDelete.HardDependencies)
		{
			Result.Add(ActivityID);
		}
		for (const FActivityID ActivityID : ToDelete.PossibleDependencies)
		{
			Result.Add(ActivityID);
		}
		return Result;
	}
	
	FDeleteSessionErrorResult DeleteActivitiesInArchivedSession(const TSharedRef<IConcertServer>& Server, const FGuid& SessionToDeleteFrom, const TSet<FActivityID>& ToDelete)
	{
		const TOptional<FConcertSessionInfo> DeletedSessionInfo = Server->GetArchivedSessionInfo(SessionToDeleteFrom);
		if (!DeletedSessionInfo)
		{
			const FString ErrorMessage = FString::Printf(TEXT("Session ID %s does not resolve to any archived session!"), *SessionToDeleteFrom.ToString());
			return FDeleteSessionErrorResult::MakeError(FText::FromString(ErrorMessage));
		}

		// Restore the session while skipping all to be deleted activities
		FText FailureReason;
		const FConcertSessionFilter Filter = BuildFilterFrom(ToDelete);
		const TSharedPtr<IConcertServerSession> LiveSession = Server->RestoreSession(SessionToDeleteFrom, *DeletedSessionInfo, Filter, FailureReason);
		if (!LiveSession)
		{
			return FDeleteSessionErrorResult::MakeError(FailureReason);
		}
		ON_SCOPE_EXIT
		{
			Server->DestroySession(LiveSession->GetId(), FailureReason);
		};

		// The archived session must be removed before it can be overwritten
		if (!Server->DestroySession(SessionToDeleteFrom, FailureReason))
		{
			return FDeleteSessionErrorResult::MakeError(FailureReason); 
		}

		if (!Server->ArchiveSession(LiveSession->GetId(), DeletedSessionInfo->SessionName, Filter, FailureReason, SessionToDeleteFrom).IsValid())
		{
			return FDeleteSessionErrorResult::MakeError(FailureReason); 
		}
		return FDeleteSessionErrorResult::MakeSuccess(); 
	}

	static FConcertSessionFilter BuildFilterFrom(const TSet<FActivityID>& ToDelete)
	{
		FConcertSessionFilter Result;
		Result.ActivityIdsToExclude.Reserve(ToDelete.Num());
		for (const FActivityID ActivityID : ToDelete)
		{
			Result.ActivityIdsToExclude.Add(ActivityID);
		}
		return Result;
	}
}

