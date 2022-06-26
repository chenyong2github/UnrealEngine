// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "IConcertSyncServer.h"
#include "Dialog/SCustomDialog.h"
#include "HistoryEdition/HistoryAnalysis.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FArchivedSessionHistoryController;

/** Displayed when a user asks to delete an activity. */
class SDeleteActivityDependenciesDialog : public SCustomDialog
{
public:

	DECLARE_DELEGATE_OneParam(FConfirmDeletion, const UE::ConcertSyncCore::FHistoryEditionArgs& /*SelectedRequirements*/)

	SLATE_BEGIN_ARGS(SDeleteActivityDependenciesDialog)
	{}
		/** Called when the user confirms the deletion of the activities.*/
		SLATE_EVENT(FConfirmDeletion, OnConfirmDeletion)
	SLATE_END_ARGS()

	/**
	 * @param InDeletionRequirements Specifies which activities must be deleted and which are optional.
	 */
	void Construct(const FArguments& InArgs, const FGuid& SessionId, const TSharedRef<IConcertSyncServer>& SyncServer, UE::ConcertSyncCore::FHistoryEditionArgs InDeletionRequirements);

private:
	
	UE::ConcertSyncCore::FHistoryEditionArgs DeletionRequirements;
	FConfirmDeletion OnConfirmDeletionFunc;
	
	/** Maps each activity with a possible dependency to whether it will be deleted or not. */
	TMap<FActivityID, bool> ActivitiesToDelete;
	
	/** Filters out all activities that are not part of the deletion requirements. */
	TSharedPtr<FArchivedSessionHistoryController> FilteredSessionHistoryController;

	TSharedRef<SWidget> CreateBody(const FGuid& InSessionId, const TSharedRef<IConcertSyncServer>& InSyncServer, UE::ConcertSyncCore::FHistoryEditionArgs InDeletionRequirements);

	void OnConfirmPressed();
};

