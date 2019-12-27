// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "SConcertSessionRecovery.h"
#include "ConcertActivityStream.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "DisasterRecoverySessionInfo.h"

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
	FDisasterRecoveryFSM(TSharedRef<IConcertSyncClient> InSyncClient, IDisasterRecoverySessionManager& RecoverySessionManager, bool bInLiveDataOnly);
	~FDisasterRecoveryFSM();

	bool IsDone() const
	{
		return CurrentState == &ExitState;
	}

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
	
	/** Tell the recovery server to drop old repositories. */
	void DropExpiredSessionRepositories();

	/** Try to find a session to recover. */
	void SelectRecoverySession();

	/** Tell the recovery server where to find this client sessions. */
	void MountSessionRepository();

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

	/**
	 * Returns true if the recovery widget should display activity details. For recovery, we expect less than 10k/20K activities. Fetching
	 * the transaction details should not be noticeable by user.
	 */
	constexpr bool ShouldDisplayActivityDetails() const { return true; }

private:
	TSharedRef<IConcertSyncClient> SyncClient;
	IDisasterRecoverySessionManager& RecoverySessionManager;
	IConcertClientRef Client;
	FDelegateHandle TickerHandle;

	// Shared state variables.
	FGuid RecoveryServerAdminEndpointId;
	TOptional<FDisasterRecoverySession> RecoverySession;
	FGuid RecoverySessionId;
	TSharedPtr<FConcertActivityStream> ActivityStream;
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
	FState DropSessionRepositoriesState;// Delete a set of old repository (and contained sessions) from this client.
	FState SelectRecoverySessionState; // Try to find a suitable session to recover.
	FState MountSessionRepositoryState;// Tell the server where to discover/load/process sessions used by this client.
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


FDisasterRecoveryFSM::FDisasterRecoveryFSM(TSharedRef<IConcertSyncClient> InSyncClient, IDisasterRecoverySessionManager& InRecoverySessionManager, bool bInLiveDataOnly)
	: SyncClient(InSyncClient)
	, RecoverySessionManager(InRecoverySessionManager)
	, Client(InSyncClient->GetConcertClient())
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
	EnterState.OnExit                    = [=]()    { Statup(); };
	LookupRecoveryServerState.OnEnter    = [this]() { Client->StartDiscovery(); };
	LookupRecoveryServerState.OnTick     = [this]() { LookupRecoveryServer(); };
	LookupRecoveryServerState.OnExit     = [this]() { Client->StopDiscovery(); };
	DropSessionRepositoriesState.OnEnter = [this]() { DropExpiredSessionRepositories(); };
	SelectRecoverySessionState.OnEnter   = [this]() { SelectRecoverySession(); };
	MountSessionRepositoryState.OnEnter  = [this]() { MountSessionRepository(); };
	LookupRecoverySessionState.OnEnter   = [this]() { LookupRecoverySession(); };
	FetchActivitiesState.OnEnter         = [this]() { ActivityStream = MakeShared<FConcertActivityStream>(Client, RecoveryServerAdminEndpointId, RecoverySessionId, ShouldDisplayActivityDetails()); };
	FetchActivitiesState.OnTick          = [this]() { FetchActivities(); };
	DisplayRecoveryUIState.OnEnter       = [this]() { DisplayRecoveryUI(); };
	CreateAndJoinSessionState.OnEnter    = [this]() { Client->OnSessionConnectionChanged().AddRaw(this, &FDisasterRecoveryFSM::OnSessionConnectionChanged); CreateAndJoinSession(); };
	CreateAndJoinSessionState.OnExit     = [this]() { Client->OnSessionConnectionChanged().RemoveAll(this); };
	RestoreAndJoinSessionState.OnEnter   = [this]() { Client->OnSessionConnectionChanged().AddRaw(this, &FDisasterRecoveryFSM::OnSessionConnectionChanged); RestoreAndJoinSession(); };
	RestoreAndJoinSessionState.OnExit    = [this]() { Client->OnSessionConnectionChanged().RemoveAll(this); };
	SynchronizeState.OnEnter             = [this]() { IDisasterRecoveryClientModule::Get().GetClient()->GetWorkspace()->OnWorkspaceSynchronized().AddRaw(this, &FDisasterRecoveryFSM::OnWorkspaceSynchronized); };
	SynchronizeState.OnExit              = [this]() { IDisasterRecoveryClientModule::Get().GetClient()->GetWorkspace()->OnWorkspaceSynchronized().RemoveAll(this); };
	PersistChangesState.OnEnter          = [this]() { WaitedFrameCount = 0; };
	PersistChangesState.OnTick           = [this]() { PersistRecoveredChanges(); };
	ErrorState.OnEnter                   = [this]() { NextStatePending = nullptr; DisplayError(); };
	ExitState.OnEnter                    = [=]()    { Terminate(); };

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
		ErrorMessage = LOCTEXT("ConnectionError", "Failed to connect to the recovery session. Recovery service will be disabled for this session.");
		TransitTo(ErrorState);
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
			TransitTo(DropSessionRepositoriesState);
			return;
		}
	}
}

void FDisasterRecoveryFSM::DropExpiredSessionRepositories()
{
	TArray<FGuid> ExpiredRepositoryIds = RecoverySessionManager.GetExpiredSessionRepositoryIds();
	if (!ExpiredRepositoryIds.Num())
	{
		TransitTo(SelectRecoverySessionState);
		return;
	}
	
	// Ask the server to drop the expired client repositories.
	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->DropSessionRepositories(RecoveryServerAdminEndpointId, ExpiredRepositoryIds).Next([this, ExecutionToken](const FConcertAdmin_DropSessionRepositoriesResponse& Response)
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		// Don't care if the function fails or not, this is non-essential cleaning that can be done next time. Notify the list of dropped repositories if any.
		if (Response.DroppedRepositoryIds.Num())
		{
			RecoverySessionManager.OnSessionRepositoryDropped(Response.DroppedRepositoryIds);
		}

		RequestTransitTo(SelectRecoverySessionState);
	});
}

void FDisasterRecoveryFSM::SelectRecoverySession()
{
	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->GetSessionRepositories(RecoveryServerAdminEndpointId).Next([this, ExecutionToken](const FConcertAdmin_GetSessionRepositoriesResponse& Response)
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response.ResponseCode == EConcertResponseCode::Success)
		{
			// Select which session should be restored (if any). Can only recover if the session repository is not mounted by another server instance.
			RecoverySession = RecoverySessionManager.FindRecoverySessionCandidate(Response.SessionRepositories);
			RequestTransitTo(MountSessionRepositoryState);
		}
		else
		{
			ErrorMessage = LOCTEXT("RepositoryQueryFailed", "Failed to retrieve repositories from the server. Recovery service will be disabled for this session.");
			RequestTransitTo(ErrorState);
		}
	});
}

void FDisasterRecoveryFSM::MountSessionRepository()
{
	// Check if an existing repository needs to be loaded or if a new one must be created.
	const bool bCreateIfNotExist = !RecoverySession.IsSet();
	const FGuid RepositoryId = RecoverySession.IsSet() ? RecoverySession->RepositoryId : RecoverySessionManager.GetSessionRepositoryId();
	const FString RepositoryRootDir = RecoverySession.IsSet() ? RecoverySession->RepositoryRootDir : RecoverySessionManager.GetSessionRepositoryRootDir(); // On restore, use the original root dir, if the user changed the root dir setting, he will likely not moves the existing sessions.

	// Try to mount the repository.
	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->MountSessionRepository(RecoveryServerAdminEndpointId, RepositoryRootDir, RepositoryId, bCreateIfNotExist, /*bAsDefault*/true).Next([this, ExecutionToken](const FConcertAdmin_MountSessionRepositoryResponse& Response)
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response.ResponseCode == EConcertResponseCode::Success)
		{
			if (RecoverySession.IsSet())
			{
				if (Response.MountStatus == EConcertSessionRepositoryMountResponseCode::Mounted)
				{
					RecoverySessionManager.TakeRecoverySessionOwnership(RecoverySession.GetValue()); // That client mounted the session repository first, take ownership of recovering the session.
					RequestTransitTo(LookupRecoverySessionState); // Find the session in the mounted repository and restore it.
				}
				else if (Response.MountStatus == EConcertSessionRepositoryMountResponseCode::AlreadyMounted)
				{
					// The session repository wasn't mounted when the session was selected as candidate, but now it is mounted. This means another instance is restoring the session from that repository.
					RequestTransitTo(SelectRecoverySessionState); // Try to select another session.
				}
				else
				{
					check(Response.MountStatus == EConcertSessionRepositoryMountResponseCode::NotFound);
					UE_LOG(LogConcert, Error, TEXT("Failed to recover previous session. The session files were likely moved or deleted. A new session will be created."));
					RecoverySessionManager.DiscardRecoverySession(RecoverySession.GetValue());
					RequestTransitTo(CreateAndJoinSessionState);
				}
			}
			else if (Response.MountStatus == EConcertSessionRepositoryMountResponseCode::Mounted)
			{
				RequestTransitTo(CreateAndJoinSessionState); // No candidate to recover. Create a new session in the new repository (set 'as default' when created).
			}
			else // NotFound/AlreadyMounted are not expected. A new repository should have been created.
			{
				ErrorMessage = LOCTEXT("RepositoryMountUnexpected", "Unexpected error while mounting session repository. Recovery service will be disabled for this session.");
				RequestTransitTo(ErrorState);
			}
		}
		else
		{
			ErrorMessage = LOCTEXT("RepositoryMountFailed", "Failed to mount session repository on the server. Recovery service will be disabled for this session.");
			RequestTransitTo(ErrorState);
		}
	});
}

void FDisasterRecoveryFSM::LookupRecoverySession()
{
	check(RecoveryServerAdminEndpointId.IsValid());
	check(RecoverySession.IsSet());

	TWeakPtr<uint8> ExecutionToken = FutureExecutionToken;
	Client->GetServerSessions(RecoveryServerAdminEndpointId).Next([this, ExecutionToken](const FConcertAdmin_GetAllSessionsResponse& Response)
	{
		if (!ExecutionToken.IsValid())
		{
			return; // The 'this' object captured has been destructed, don't run the continuation.
		}

		if (Response.ResponseCode == EConcertResponseCode::Success)
		{
			if (const FConcertSessionInfo* SessionToRestore = Response.ArchivedSessions.FindByPredicate([this](const FConcertSessionInfo& ArchivedSession){ return ArchivedSession.SessionName == RecoverySession->LastSessionName; }))
			{
				RecoverySessionId = SessionToRestore->SessionId;
				RequestTransitTo(FetchActivitiesState);
			}
			else
			{
				UE_LOG(LogConcert, Error, TEXT("Failed to recover previous session. The session was likely moved or deleted. A new session will be created."));
				RecoverySessionManager.DiscardRecoverySession(RecoverySession.GetValue());
				RequestTransitTo(CreateAndJoinSessionState);
			}
		}
		else
		{
			ErrorMessage = LOCTEXT("SessionQueryFailed", "Failed to retrieve available sessions. Recovery service will be disabled for this session.");
			RequestTransitTo(ErrorState);
		}
	});
}

void FDisasterRecoveryFSM::FetchActivities()
{
	check(ActivityStream.IsValid());

	int32 FetchedCount;
	FText ReadError;
	bool bEndOfStream = ActivityStream->Read(Activities, FetchedCount, ReadError);

	if (!ReadError.IsEmpty())
	{
		ErrorMessage = ReadError;
		RequestTransitTo(ErrorState);
	}
	else if (bEndOfStream) // Read everything?
	{
		if (Activities.Num() > 0)
		{
			RequestTransitTo(DisplayRecoveryUIState);
		}
		else // Nothing to recover
		{
			UE_LOG(LogConcert, Warning, TEXT("Disaster recovery service could not find any activities to recover."));
			RequestTransitTo(CreateAndJoinSessionState);
		}
	}
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

	auto FetchActivitiesFn = [this](TArray<TSharedPtr<FConcertClientSessionActivity>>& OutActivities, int32& OutFetchCount, FText& OutErrorMsg)
	{
		OutFetchCount = Activities.Num();
		OutErrorMsg = FText();
		OutActivities.Append(Activities);
		return true; // All activities fetched.
	};

	TSharedRef<SConcertSessionRecovery> RecoveryWidget =
		SNew(SConcertSessionRecovery)
		.IntroductionText(LOCTEXT("CrashRecoveryIntroductionText", "An abnormal Editor termination was detected for this project. You can recover up to the last operation recorded or to a previous state."))
		.ParentWindow(NewWindow)
		.OnFetchActivities(FetchActivitiesFn)
		.ClientAvatarColorColumnVisibility(EVisibility::Hidden) // Disaster recovery has only one user, the local one.
		.ClientNameColumnVisibility(EVisibility::Hidden)
		.OperationColumnVisibility(EVisibility::Visible)
		.PackageColumnVisibility(EVisibility::Visible)
		.DetailsAreaVisibility(ShouldDisplayActivityDetails() ? EVisibility::Visible : EVisibility::Collapsed)
		.IsConnectionActivityFilteringEnabled(false) // For disaster recovery, connection and lock events are meaningless, don't show the filtering options.
		.IsLockActivityFilteringEnabled(false);

	NewWindow->SetContent(RecoveryWidget);
	FSlateApplication::Get().AddModalWindow(NewWindow, nullptr);

	// Get which item was selected to recover through.
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
			ErrorMessage = FText::Format(LOCTEXT("FailedToCreate", "Failed to create recovery session '{0}'. Recovery service will be disabled for this session."), FText::AsCultureInvariant(Client->GetConfiguration()->DefaultSessionName));
			RequestTransitTo(ErrorState);
		}
		// else -> On success callback OnSessionConnectionChanged() will transit to synchronize state.
	});
}

void FDisasterRecoveryFSM::RestoreAndJoinSession()
{
	check(SelectedRecoveryActivity.IsValid());
	check(RecoverySession.IsSet());

	const UConcertClientConfig* ClientConfig = Client->GetConfiguration();

	FConcertRestoreSessionArgs RestoreArgs;
	RestoreArgs.ArchiveNameOverride = ClientConfig->DefaultSaveSessionAs;
	RestoreArgs.bAutoConnect = true; // Auto-connect will automatically join the session.
	RestoreArgs.SessionId = RecoverySessionId;
	RestoreArgs.SessionFilter.ActivityIdUpperBound = SelectedRecoveryActivity->Activity.ActivityId;
	RestoreArgs.SessionFilter.bOnlyLiveData = bLiveDataOnly; // TODO: I'm not sure if this should be true or false.
	RestoreArgs.SessionFilter.bIncludeIgnoredActivities = false; // Don't restore ignored activities. (Like MultiUser events recorded in a DisasterRecovery session for inspection purpose)
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
			UE_LOG(LogConcert, Error, TEXT("Disaster recovery service failed to restore session '%s (%s)'. A new session will be created."), *RecoverySession->LastSessionName, *RecoverySessionId.ToString());
			RecoverySessionManager.DiscardRecoverySession(RecoverySession.GetValue()); // If it failed once, it has no reason to succeed later.
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

void DisasterRecoveryUtil::StartRecovery(TSharedRef<IConcertSyncClient> SyncClient, IDisasterRecoverySessionManager& InRecoveryManager, bool bLiveDataOnly)
{
	if (!RecoveryFSM)
	{
		RecoveryFSM = MakeShared<FDisasterRecoveryFSM>(SyncClient, InRecoveryManager, bLiveDataOnly);
	}
}

bool DisasterRecoveryUtil::EndRecovery()
{
	bool bDone = true;
	if (RecoveryFSM)
	{
		bDone = RecoveryFSM->IsDone();
		RecoveryFSM.Reset();
	}

	return bDone;
}

FString DisasterRecoveryUtil::GetDisasterRecoveryServiceExeName()
{
	if (FGenericCrashContext::IsOutOfProcessCrashReporter())
	{
		return TEXT("CrashReporterClientEditor");
	}
	else
	{
		return TEXT("UnrealDisasterRecoveryService");
	}
}

#undef LOCTEXT_NAMESPACE
