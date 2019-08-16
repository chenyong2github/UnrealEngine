// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisasterRecoveryFSM.h"

#include "ConcertLogGlobal.h"
#include "ConcertMessages.h"
#include "IConcertClient.h"
#include "IConcertClientWorkspace.h"
#include "IConcertSyncClient.h"
#include "IConcertClientWorkspace.h"
#include "IDisasterRecoveryClientModule.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AsyncTaskNotification.h"
#include "Widgets/SWindow.h"
#include "SDisasterRecovery.h"

#define LOCTEXT_NAMESPACE "DisasterRecoveryFSM"

/**
 * Runs the steps required to find, load, display let the user select a recovery point and restore its assets to a given
 * point in time.
 */
class FDisasterRecoveryFSM
{
public:
	/** The maximum number of activities to fetch per request. */
	static constexpr int64 MaxActivityPerRequest = 1024;

public:
	FDisasterRecoveryFSM(TSharedRef<IConcertSyncClient> InSyncClient, const FString& InSessionNameToRecover, bool bInLiveDataOnly);
	~FDisasterRecoveryFSM();

private:
	struct FState
	{
		FState(): OnEnter([](){}), OnTick([](){}), OnExit([](){}) {}
		TFunction<void()> OnEnter;
		TFunction<void()> OnTick;
		TFunction<void()> OnExit;
	};

	/** Invoked when the client connects/disconnects from the recovery session. */
	void OnSessionConnectionChanged(IConcertClientSession& Session, EConcertConnectionStatus Status);

	/** Invoked when the client workspace is synchronized, i.e. when the recovery has completed. */
	void OnWorkspaceSynchronized();

	/** Tick the finite state machine. */
	bool Tick(float);

	/** Poll the list of servers and lookup the recovery service and transit to the next state (finding the session to recover) once the recovery service is found. */
	void LookupRecoveryServer();

	/** Poll the list of sessions from the recovery server, look up for the recovery session and transit to the next state (fetching activities) once the recovery session is found. */
	void LookupRecoverySession();

	/** Requests and stores the recovery session activities. Each call request a 'page'. Transit to the next state (displaying once all activities are retrieved or create the session if no activities were found). */
	void FetchActivities();

	/** Displays the session activities and gather the user selection. Transit to the next state (restoring the session) once the recover windows is closed by the user. */
	void DisplayRecoveryUI();

	/** Creates and join a new recovery session is there is nothing to restore (or user selected to not restore), then go to the synchronization session state state. */
	void CreateAndJoinSession();

	/** Restores and join the session on the server at the selected point in time, then go to the synchronization state. */
	void RestoreAndJoinSession();

	/** Persists the recovered transactions locally, applying all changes to the game content folder. */
	void PersistRecoveredChanges();

	/** Displays the error that halted the recovery FSM and go to the exit state. */
	void DisplayError();

	/** Requests a transition to the next state, applied to the next Tick(). Used when triggering a transition in a TFuture continuation. */
	void RequestTransitTo(FState& NextState)
	{
		NextStatePending = &NextState;
	}

	/** Transite to the specified state. */
	void TransitTo(FState& NextState)
	{
		CurrentState->OnExit();
		CurrentState = &NextState;
		CurrentState->OnEnter();
	}

private:
	TSharedRef<IConcertSyncClient> SyncClient;
	IConcertClientRef Client;
	FDelegateHandle TickerHandle;

	// Shared state variables.
	FGuid RecoveryServerAdminEndpointId;
	FString RecoverySessionName;
	FGuid RecoverySessionId;
	TArray<TSharedPtr<FConcertClientSessionActivity>> Activities;
	TSharedPtr<FConcertClientSessionActivity> SelectedRecoveryActivity;
	bool bLiveDataOnly;
	TSharedPtr<uint8> FutureExecutionToken;
	FText ErrorMessage;
	FString ExitMessage;
	int32 WaitedFrameCount = 0;

	// States.
	FState EnterState;                 // State to start from.
	FState LookupRecoveryServerState;  // Poll to find the recovery servers.
	FState LookupRecoverySessionState; // Poll to find the recovery session.
	FState FetchActivitiesState;       // Fetch all recovery session activities.
	FState DisplayRecoveryUIState;     // Let the user view and select the recovery point.
	FState RestoreAndJoinSessionState; // Restore the session on the server.
	FState CreateAndJoinSessionState;  // Create a new session on the server.
	FState SynchronizeState;           // Recover the assets to the selected point.
	FState PersistChangesState;        // Applies the recovered transactions to the game content directory packages (making them dirty).
	FState ErrorState;                 // Displays the error and halt the FSM.
	FState ExitState;                  // State to exit the FSM.
	FState* CurrentState = nullptr;    // The current state.
	FState* NextStatePending = nullptr;// State to transit to at the next Tick().
};

static TSharedPtr<FDisasterRecoveryFSM> RecoveryFSM;


FDisasterRecoveryFSM::FDisasterRecoveryFSM(TSharedRef<IConcertSyncClient> InSyncClient, const FString& SessionNameToRecover, bool bInLiveDataOnly)
	: SyncClient(InSyncClient)
	, Client(InSyncClient->GetConcertClient())
	, RecoverySessionName(SessionNameToRecover)
	, bLiveDataOnly(bInLiveDataOnly)
	, ExitMessage(TEXT("Disaster recovery process completed successfully."))
	, CurrentState(&EnterState)
{
	FutureExecutionToken = MakeShared<uint8>(0); // Token use to prevent execution of async continuation if this object instance is destructed before then continuation is executed.

	auto Statup = [this]()
	{
		UE_LOG(LogConcert, Display, TEXT("Disaster recovery process started."));
		TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FDisasterRecoveryFSM::Tick), 0.0f);
	};

	auto Terminate = [this]()
	{
		NextStatePending = nullptr;
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
		UE_LOG(LogConcert, Display, TEXT("%s"), *ExitMessage);
	};

	// Implement the states, pretty much in execution order.
	EnterState.OnExit                  = [=]()    { Statup(); };
	LookupRecoveryServerState.OnEnter  = [this]() { Client->StartDiscovery(); };
	LookupRecoveryServerState.OnTick   = [this]() { LookupRecoveryServer(); };
	LookupRecoveryServerState.OnExit   = [this]() { Client->StopDiscovery(); };
	LookupRecoverySessionState.OnEnter = [this]() { LookupRecoverySession(); };
	FetchActivitiesState.OnEnter       = [this]() { FetchActivities(); };
	DisplayRecoveryUIState.OnEnter     = [this]() { DisplayRecoveryUI(); };
	CreateAndJoinSessionState.OnEnter  = [this]() { Client->OnSessionConnectionChanged().AddRaw(this, &FDisasterRecoveryFSM::OnSessionConnectionChanged); CreateAndJoinSession(); };
	CreateAndJoinSessionState.OnExit   = [this]() { Client->OnSessionConnectionChanged().RemoveAll(this); };
	RestoreAndJoinSessionState.OnEnter = [this]() { Client->OnSessionConnectionChanged().AddRaw(this, &FDisasterRecoveryFSM::OnSessionConnectionChanged); RestoreAndJoinSession(); };
	RestoreAndJoinSessionState.OnExit  = [this]() { Client->OnSessionConnectionChanged().RemoveAll(this); };
	SynchronizeState.OnEnter           = [this]() { IDisasterRecoveryClientModule::Get().GetClient()->GetWorkspace()->OnWorkspaceSynchronized().AddRaw(this, &FDisasterRecoveryFSM::OnWorkspaceSynchronized); };
	SynchronizeState.OnExit            = [this]() { IDisasterRecoveryClientModule::Get().GetClient()->GetWorkspace()->OnWorkspaceSynchronized().RemoveAll(this); };
	PersistChangesState.OnEnter        = [this]() { WaitedFrameCount = 0; };
	PersistChangesState.OnTick         = [this]() { PersistRecoveredChanges(); };
	ErrorState.OnEnter                 = [this]() { NextStatePending = nullptr; DisplayError(); };
	ExitState.OnEnter                  = [=]()    { Terminate(); };

	// Starts the finite state machine, transiting from 'EnterState' to 'LookupRecoveryServerState'.
	TransitTo(LookupRecoveryServerState);
}

FDisasterRecoveryFSM::~FDisasterRecoveryFSM()
{
	if (CurrentState != &ExitState)
	{
		ExitMessage = TEXT("Disaster recovery process was aborted.");
		TransitTo(ExitState); // Ensure to run the OnExit() of the current state if the FSM is aborted.
	}
}

void FDisasterRecoveryFSM::OnSessionConnectionChanged(IConcertClientSession& Session, EConcertConnectionStatus Status)
{
	check (CurrentState == &CreateAndJoinSessionState || CurrentState == &RestoreAndJoinSessionState);

	if (Status == EConcertConnectionStatus::Connected)
	{
		TransitTo(SynchronizeState); // Concert will perform initial synchronization, i.e. restoring the user assets to the selected point in time.
	}
	else if (Status == EConcertConnectionStatus::Disconnected)
	{
		ErrorMessage = LOCTEXT("ConnectionError", "Disaster recover client failed to connect to the recovery session.");
		TransitTo(ErrorState); // Something went wrong.
	}
}

void FDisasterRecoveryFSM::OnWorkspaceSynchronized()
{
	check(CurrentState == &SynchronizeState);
	TransitTo(PersistChangesState);
}

void FDisasterRecoveryFSM::LookupRecoveryServer()
{
	TArray<FConcertServerInfo> Servers = Client->GetKnownServers();
	for (const FConcertServerInfo& ServerInfo : Servers)
	{
		if (ServerInfo.ServerName == Client->GetConfiguration()->DefaultServerURL)
		{
			RecoveryServerAdminEndpointId = ServerInfo.AdminEndpointId;
			TransitTo(LookupRecoverySessionState);
		}
	}
}

void FDisasterRecoveryFSM::LookupRecoverySession()
{
	check(RecoveryServerAdminEndpointId.IsValid());

	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->GetServerSessions(RecoveryServerAdminEndpointId).Next([this, ExecutionToken](const FConcertAdmin_GetAllSessionsResponse& Response)
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response.ResponseCode == EConcertResponseCode::Success)
		{
			// The disaster recovery service archives the sessions on exit (or on reboot if it crashed/was killed).
			if (const FConcertSessionInfo* ArchMatch = Response.ArchivedSessions.FindByPredicate([this](const FConcertSessionInfo& MatchCandidate) { return MatchCandidate.SessionName == RecoverySessionName; }))
			{
				RecoverySessionId = ArchMatch->SessionId;
				RequestTransitTo(FetchActivitiesState);
			}
			else
			{
				UE_LOG(LogConcert, Error, TEXT("Disaster recovery service could not find the recovery session '%s'. A new session will be created."), *RecoverySessionName);
				RequestTransitTo(CreateAndJoinSessionState); // We still need to have a recovery session at the end.
			}
		}
		else
		{
			ErrorMessage = LOCTEXT("SessionQueryFailed", "Failed to retrieve the session list from the recovery service.");
			RequestTransitTo(ErrorState);
		}
	});
}

void FDisasterRecoveryFSM::FetchActivities()
{
	// Note: Activity IDs are 1-based.
	const int64 FromActivityId = Activities.Num() + 1;

	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->GetSessionActivities(RecoveryServerAdminEndpointId, RecoverySessionId, FromActivityId, MaxActivityPerRequest).Next([this, ExecutionToken](const FConcertAdmin_GetSessionActivitiesResponse& Response)
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response.ResponseCode == EConcertResponseCode::Success)
		{
			for (const FConcertSessionSerializedPayload& SerializedActivity : Response.Activities)
			{
				FConcertSyncActivity Activity;
				SerializedActivity.GetTypedPayload<FConcertSyncActivity>(Activity);

				// Get the Activity summary.
				FStructOnScope EventSummaryPayload;
				Activity.EventSummary.GetPayload(EventSummaryPayload);

				Activities.Add(MakeShared<FConcertClientSessionActivity>(Activity, MoveTemp(EventSummaryPayload)));
			}

			if (Response.Activities.Num() == MaxActivityPerRequest) // Maybe there is more?
			{
				RequestTransitTo(FetchActivitiesState); // Exit/Enter the same state again to fetch the next page.
			}
			else if (Activities.Num() > 0) // Returned less than MaxActivityPerRequest -> it reached the last activity and there is something to recover.
			{
				RequestTransitTo(DisplayRecoveryUIState);
			}
			else // Nothing to recover.
			{
				UE_LOG(LogConcert, Warning, TEXT("Disaster recovery service could not find any activity to recover"));
				RequestTransitTo(CreateAndJoinSessionState);
			}
		}
		else
		{
			ErrorMessage = FText::Format(LOCTEXT("ActivityQueryFailed", "Failed to retrieve {0} activities from the session '{1} ({2})'"), Activities.Num() + 1, FText::AsCultureInvariant(RecoverySessionName), FText::AsCultureInvariant(RecoverySessionId.ToString()));
			RequestTransitTo(ErrorState);
		}
	});
}

void FDisasterRecoveryFSM::DisplayRecoveryUI()
{
	check(Activities.Num() > 0); // Should not display UI if the user has nothing to decide.

	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(LOCTEXT("RecoveryTitle", "Disaster Recovery"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(1200, 800))
		.SupportsMaximize(true)
		.SupportsMinimize(false);

	TSharedRef<SDisasterRecovery> RecoveryWidget =
		SNew(SDisasterRecovery, Activities)
		.ParentWindow(NewWindow);

	NewWindow->SetContent(RecoveryWidget);
	FSlateApplication::Get().AddModalWindow(NewWindow, nullptr);

	// Remember which item was selected to recover through.
	SelectedRecoveryActivity = RecoveryWidget->GetRecoverThroughItem();
	if (SelectedRecoveryActivity)
	{
		TransitTo(RestoreAndJoinSessionState); // User selected to restore up to a given activity.
	}
	else
	{
		TransitTo(CreateAndJoinSessionState); // User selected to not restore anything.
	}
}

void FDisasterRecoveryFSM::CreateAndJoinSession()
{
	FConcertCreateSessionArgs CreateArgs;
	CreateArgs.SessionName = Client->GetConfiguration()->DefaultSessionName;
	CreateArgs.ArchiveNameOverride = Client->GetConfiguration()->DefaultSaveSessionAs;

	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->CreateSession(RecoveryServerAdminEndpointId, CreateArgs).Next([this, ExecutionToken](EConcertResponseCode Response) // This also joins the session on successful creation.
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response != EConcertResponseCode::Success)
		{
			ErrorMessage = FText::Format(LOCTEXT("FailedToCreate", "Failed to create recovery session '{0}'"), FText::AsCultureInvariant(Client->GetConfiguration()->DefaultSessionName));
			RequestTransitTo(ErrorState);
		}
		// else -> On success callback OnSessionConnectionChanged() will transit to synchronize state.
	});
}

void FDisasterRecoveryFSM::RestoreAndJoinSession()
{
	check(SelectedRecoveryActivity.IsValid());

	const UConcertClientConfig* ClientConfig = Client->GetConfiguration();

	FConcertRestoreSessionArgs RestoreArgs;
	RestoreArgs.ArchiveNameOverride = ClientConfig->DefaultSaveSessionAs;
	RestoreArgs.bAutoConnect = true; // Auto-connect will automatically join the session.
	RestoreArgs.SessionId = RecoverySessionId;
	RestoreArgs.SessionFilter.ActivityIdUpperBound = SelectedRecoveryActivity->Activity.ActivityId;
	RestoreArgs.SessionFilter.bOnlyLiveData = bLiveDataOnly; // TODO: I'm not sure if this should be true or false.
	RestoreArgs.SessionName = ClientConfig->DefaultSessionName;

	// Restore the session on the server.
	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->RestoreSession(RecoveryServerAdminEndpointId, RestoreArgs).Next([this, ExecutionToken](EConcertResponseCode Response) // This also joins the session on successful restore.
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response != EConcertResponseCode::Success)
		{
			UE_LOG(LogConcert, Error, TEXT("Disaster recovery service failed to restore session '%s (%s)'. A new session will be created."), *RecoverySessionName, *RecoverySessionId.ToString());
			RequestTransitTo(CreateAndJoinSessionState); // Still need to have a session at the end.
		}
		// else -> On success callback OnSessionConnectionChanged() will transit to synchronize state.
	});
}

void FDisasterRecoveryFSM::PersistRecoveredChanges()
{
	// Don't execute in the same frame as OnWorkspaceSynchronized(). FConcertClientWorkspace::OnEndFrame() needs to run first to apply the transaction before persisting changes.
	if (WaitedFrameCount++ >= 1)
	{
		// Save live transactions to package, gather files changed in the Concert sandbox and apply the changes to the content directory.
		SyncClient->PersistAllSessionChanges();
		TransitTo(ExitState); // Disaster recovery process completed successfully.
	}
}

void FDisasterRecoveryFSM::DisplayError()
{
	FAsyncTaskNotificationConfig NotificationConfig;
	NotificationConfig.bIsHeadless = false;
	NotificationConfig.bKeepOpenOnFailure = true;
	NotificationConfig.LogCategory = &LogConcert;

	FAsyncTaskNotification Notification(NotificationConfig);
	Notification.SetComplete(LOCTEXT("RecoveryError", "Recovery Process Failure"), ErrorMessage, /*Success*/ false);

	ExitMessage = FString::Format(TEXT("Disaster recovery failed: {0}"), {ErrorMessage.ToString()});
	TransitTo(ExitState);
}

bool FDisasterRecoveryFSM::Tick(float)
{
	if (NextStatePending != nullptr)
	{
		TransitTo(*NextStatePending);
		NextStatePending = nullptr;
	}

	if (CurrentState == &ExitState)
	{
		DisasterRecoveryUtil::EndRecovery(); // Self-Destruct.
		return false;
	}
	else
	{
		CurrentState->OnTick();
	}

	return true;
}

void DisasterRecoveryUtil::StartRecovery(TSharedRef<IConcertSyncClient> SyncClient, const FString& SessionNameToRecover, bool bLiveDataOnly)
{
	if (!RecoveryFSM)
	{
		RecoveryFSM = MakeShared<FDisasterRecoveryFSM>(SyncClient, SessionNameToRecover, bLiveDataOnly);
	}
}

void DisasterRecoveryUtil::EndRecovery()
{
	RecoveryFSM.Reset();
}

#undef LOCTEXT_NAMESPACE
