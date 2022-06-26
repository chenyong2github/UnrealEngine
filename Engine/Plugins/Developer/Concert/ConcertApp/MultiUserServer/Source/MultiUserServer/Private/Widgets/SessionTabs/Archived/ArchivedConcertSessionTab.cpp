// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SessionTabs/Archived/ArchivedConcertSessionTab.h"

#include "ArchivedSessionHistoryController.h"
#include "ConcertLogGlobal.h"
#include "HistoryEdition/ActivityNode.h"
#include "HistoryEdition/DebugDependencyGraph.h"
#include "HistoryEdition/DependencyGraphBuilder.h"
#include "HistoryEdition/HistoryAnalysis.h"
#include "HistoryEdition/HistoryEdition.h"
#include "MultiUserServerConsoleVariables.h"
#include "MultiUserServerModule.h"
#include "Session/History/SEditableSessionHistory.h"
#include "Session/History/SSessionHistory.h"
#include "Window/ModalWindowManager.h"
#include "Widgets/HistoryDeletion/SDeleteActivityDependenciesDialog.h"
#include "Widgets/SessionTabs/Archived/SConcertArchivedSessionTabView.h"

#include "Algo/AllOf.h"
#include "Algo/Transform.h"
#include "Dialog/SMessageDialog.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

FArchivedConcertSessionTab::FArchivedConcertSessionTab(FGuid InspectedSessionID, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow)
	: FConcertSessionTabBase(InspectedSessionID, SyncServer)
	, InspectedSessionID(MoveTemp(InspectedSessionID))
	, SyncServer(MoveTemp(SyncServer))
	, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
{}

void FArchivedConcertSessionTab::CreateDockContent(const TSharedRef<SDockTab>& InDockTab)
{
	SEditableSessionHistory::FMakeSessionHistory MakeSessionHistory = SEditableSessionHistory::FMakeSessionHistory::CreateLambda([this](SSessionHistory::FArguments Arguments)
	{
		checkf(!HistoryController.IsValid(), TEXT("Called more than once"));
		HistoryController = UE::MultiUserServer::CreateForInspector(InspectedSessionID, SyncServer, MoveTemp(Arguments));
		return HistoryController->GetSessionHistory();
	});
	
	InDockTab->SetContent(
		SNew(SConcertArchivedSessionTabView, *GetTabId())
			.ConstructUnderMajorTab(InDockTab)
			.ConstructUnderWindow(ConstructUnderWindow.Get())
			.MakeSessionHistory(MoveTemp(MakeSessionHistory))
			.DeleteActivity_Raw(this, &FArchivedConcertSessionTab::OnRequestDeleteActivity)
			.CanDeleteActivity_Raw(this, &FArchivedConcertSessionTab::CanDeleteActivity)
		);
}

void FArchivedConcertSessionTab::OnRequestDeleteActivity(const TSet<TSharedRef<FConcertSessionActivity>>& ActivitiesToDelete) const
{
	using namespace UE::ConcertSyncCore;
	
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> SessionDatabase = SyncServer->GetArchivedSessionDatabase(InspectedSessionID))
	{
		const FActivityDependencyGraph DependencyGraph = BuildDependencyGraphFrom(*SessionDatabase);
		if (UE::MultiUserServer::ConsoleVariables::CVarLogActivityDependencyGraphOnDelete.GetValueOnGameThread())
		{
			UE_LOG(LogConcert, Log, TEXT("%s"), *UE::ConcertSyncCore::Graphviz::ExportToGraphviz(DependencyGraph, *SessionDatabase));
		}
		
		TSet<FActivityID> RequestedForDelete;
		Algo::Transform(ActivitiesToDelete, RequestedForDelete, [](const TSharedRef<FConcertSessionActivity>& Activity)
		{
			return Activity->Activity.ActivityId;
		});
		FHistoryEditionArgs DeletionRequirements = AnalyseActivityDependencies(RequestedForDelete, DependencyGraph, true);

		TWeakPtr<const FArchivedConcertSessionTab> WeakTabThis = SharedThis(this);
		TSharedRef<SDeleteActivityDependenciesDialog> Dialog = SNew(SDeleteActivityDependenciesDialog, InspectedSessionID, SyncServer, MoveTemp(DeletionRequirements))
			.OnConfirmDeletion_Lambda([WeakTabThis](const FHistoryEditionArgs& SelectedRequirements)
			{
				// Because the dialog is non-modal, the user may have closed the program in the mean time
				if (const TSharedPtr<const FArchivedConcertSessionTab> PinnedThis = WeakTabThis.Pin())
				{
					const FOperationErrorResult ErrorResult = DeleteActivitiesInArchivedSession(PinnedThis->SyncServer->GetConcertServer(), PinnedThis->InspectedSessionID, CombineRequirements(SelectedRequirements));
					if (ErrorResult.HadError())
					{
						UE_LOG(LogConcert, Error, TEXT("Failed to delete activities from session %s: %s"), *PinnedThis->InspectedSessionID.ToString(), *ErrorResult.ErrorMessage->ToString());
						
						const TSharedRef<SMessageDialog> ErrorDialog = SNew(SMessageDialog)
							.Title(LOCTEXT("ErrorDeletingSessions", "Error deleting sessions"))
							.Message(*ErrorResult.ErrorMessage)
							.Buttons({
								SMessageDialog::FButton(LOCTEXT("Ok", "Ok"))
								.SetPrimary(true)
							});
						ErrorDialog->Show();
					}
					else
					{
						// The list needs to be refreshed after the delete operation
						PinnedThis->HistoryController->ReloadActivities();
					}
				}
			});
		
		UE::MultiUserServer::FConcertServerUIModule::Get()
			.GetModalWindowManager()
			->ShowFakeModalWindow(Dialog);
	}
}

FCanDeleteActivitiesResult FArchivedConcertSessionTab::CanDeleteActivity(const TSet<TSharedRef<FConcertSessionActivity>>& ActivitiesToDelete) const
{
	const bool bOnlyPackagesAndTransactions =  Algo::AllOf(ActivitiesToDelete, [](const TSharedRef<FConcertSessionActivity>& Activity)
	{
		return Activity->Activity.EventType == EConcertSyncActivityEventType::Package || Activity->Activity.EventType == EConcertSyncActivityEventType::Transaction;
	});
	if (!bOnlyPackagesAndTransactions)
	{
		return FCanDeleteActivitiesResult::No(LOCTEXT("CanDeleteActivity.OnlyPackagesAndTransactionsReason", "Only package and transaction activities can be deleted (the current selection includes other activity types)."));
	}

	return FCanDeleteActivitiesResult::Yes();
}

#undef LOCTEXT_NAMESPACE