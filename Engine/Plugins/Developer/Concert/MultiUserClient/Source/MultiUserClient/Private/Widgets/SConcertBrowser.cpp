// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertBrowser.h"

#include "IMultiUserClientModule.h"
#include "IConcertClient.h"
#include "IConcertClientWorkspace.h"
#include "IConcertSyncClient.h"
#include "ConcertActivityStream.h"
#include "ConcertFrontendStyle.h"
#include "ConcertFrontendUtils.h"
#include "SActiveSession.h"
#include "SConcertSessionRecovery.h"
#include "ConcertSessionBrowserSettings.h"
#include "ConcertSettings.h"
#include "ConcertLogGlobal.h"
#include "MultiUserClientUtils.h"

#include "Algo/Transform.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Regex.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/Char.h"
#include "Misc/MessageDialog.h"
#include "Misc/TextFilter.h"
#include "SlateOptMacros.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SHeader.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
#endif

#define LOCTEXT_NAMESPACE "SConcertBrowser"

namespace ConcertBrowserUtils
{

// Defines the sessions list view column tag names.
const FName IconColName(TEXT("Icon"));
const FName SessionColName(TEXT("Session"));
const FName ServerColName(TEXT("Server"));

// Name of the filter box in the View option.
const FName ActiveSessionsCheckBoxMenuName(TEXT("ActiveSessions"));
const FName ArchivedSessionsCheckBoxMenuName(TEXT("ArchivedSessions"));
const FName DefaultServerCheckBoxMenuName(TEXT("DefaultServer"));

// The awesome font used to pick the icon displayed in the session list view 'Icon' column.
const FName IconColumnFontName(TEXT("FontAwesome.9"));

/** Utility function used to create buttons displaying only an icon (using FontAwesome). */
TSharedRef<SButton> MakeIconButton(const FName& ButtonStyle, const TAttribute<FText>& GlyphIcon, const TAttribute<FText>& Tooltip, const TAttribute<bool>& EnabledAttribute, const FOnClicked& OnClicked,
	const FSlateColor& ForegroundColor, const TAttribute<EVisibility>& Visibility = EVisibility::Visible, const TAttribute<FMargin>& ContentPadding = FMargin(3.0, 2.0), const FName FontStyle = IconColumnFontName)
{
	return SNew(SButton)
		.ForegroundColor(ForegroundColor)
		.ButtonStyle(FEditorStyle::Get(), ButtonStyle)
		.OnClicked(OnClicked)
		.ToolTipText(Tooltip)
		.ContentPadding(ContentPadding)
		.Visibility(Visibility)
		.IsEnabled(EnabledAttribute)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle(FontStyle))
			.Text(GlyphIcon)
		];
}

/** Utility function used to create buttons displaying only an icon (using a brush) */
TSharedRef<SButton> MakeIconButton(const FName& ButtonStyle, const TAttribute<const FSlateBrush*>& Icon, const TAttribute<FText>& Tooltip, const TAttribute<bool>& EnabledAttribute, const FOnClicked& OnClicked, const TAttribute<EVisibility>& Visibility = EVisibility::Visible)
{
	return SNew(SButton)
		.ForegroundColor(FSlateColor::UseForeground())
		.ButtonStyle(FEditorStyle::Get(), ButtonStyle)
		.OnClicked(OnClicked)
		.ToolTipText(Tooltip)
		.ContentPadding(FMargin(0, 0))
		.Visibility(Visibility)
		.IsEnabled(EnabledAttribute)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SImage).Image(Icon)
		];
}

/** Returns the tooltip shown when hovering the triangle with an exclamation icon when a server doesn't validate the version requirements. */
FText GetServerVersionIgnoredTooltip()
{
	return LOCTEXT("ServerIgnoreSessionRequirementsTooltip", "Careful this server won't verify that you have the right requirements before you join a session");
}

/** Create a widget displaying the triangle with an exclamation icon in case the server flags include IgnoreSessionRequirement. */
TSharedRef<SWidget> MakeServerVersionIgnoredWidget(EConcertServerFlags InServerFlags)
{
	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.ColorAndOpacity(FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton.Warning").Normal.TintColor.GetSpecifiedColor())
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
			.Text(FEditorFontGlyphs::Exclamation_Triangle)
			.ToolTipText(GetServerVersionIgnoredTooltip())
			.Visibility((InServerFlags & EConcertServerFlags::IgnoreSessionRequirement) != EConcertServerFlags::None ? EVisibility::Visible : EVisibility::Collapsed)
		];
}

} // namespace ConcertSessionBrowserUtils

/** Signal emitted when a session name text field should enter in edit mode. */
DECLARE_MULTICAST_DELEGATE(FOnBeginEditConcertSessionNameRequest)

/**
 * Item displayed in the session list view.
 */
class FConcertSessionItem
{
public:
	enum class EType
	{
		None,
		NewSession,      // Editable item to enter a session name and a pick a server.
		RestoreSession,  // Editable item to name the restored session.
		SaveSession,     // Editable item to name the archive.
		ActiveSession,   // Read-only item representing an active session.
		ArchivedSession, // Read-only item representing an archived session.
	};

	FConcertSessionItem(EType Type, const FString& InSessionName, const FGuid& InSessionId, const FString& InServerName, const FGuid& InServerEndpoint, EConcertServerFlags InServerFlags)
		: Type(Type)
		, ServerAdminEndpointId(InServerEndpoint)
		, SessionId(InSessionId)
		, SessionName(InSessionName)
		, ServerName(InServerName)
		, ServerFlags(InServerFlags)
	{
	}

	bool operator==(const FConcertSessionItem& Other) const
	{
		return Type == Other.Type && ServerAdminEndpointId == Other.ServerAdminEndpointId && SessionId == Other.SessionId;
	}

	EType Type = EType::None;
	FGuid ServerAdminEndpointId;
	FGuid SessionId;
	FString SessionName;
	FString ServerName;
	FOnBeginEditConcertSessionNameRequest OnBeginEditSessionNameRequest; // Emitted when user press 'F2' or select 'Rename' from context menu.
	EConcertServerFlags ServerFlags;
};


/**
 * Runs and cache network queries for the UI. In the model-view-controller pattern, this class acts like the controller. Its purpose
 * is to keep the UI code as decoupled as possible from the API used to query it. It encapsulate the asynchronous code and provide a
 * simpler API to the UI.
 */
class FConcertBrowserController
{
public:
	/** Keeps the state of an active async request and provides a tool to cancel its future continuation execution. */
	struct FAsyncRequest
	{
		/** Returns true if there is a registered async request future and if it hasn't executed yet. */
		bool IsOngoing() const { return Future.IsValid() && !Future.IsReady(); }

		/** Reset the execution token, canceling previous execution (if any) and setting up the token for a new request. */
		TWeakPtr<uint8, ESPMode::ThreadSafe> ResetExecutionToken() { FutureExecutionToken = MakeShared<uint8, ESPMode::ThreadSafe>(); return FutureExecutionToken; }

		/** Cancel the execution of request async continuation. */
		void Cancel() { FutureExecutionToken.Reset(); }

		/** The future provided by an asynchronous request. */
		TFuture<void> Future;

		/** Determines whether or not the async request continuation code should execute. Reset to disarm execution of an async future continuation. */
		TSharedPtr<uint8, ESPMode::ThreadSafe> FutureExecutionToken;
	};

	struct FActiveSessionInfo
	{
		FConcertServerInfo ServerInfo;
		FConcertSessionInfo SessionInfo;
		FAsyncRequest ListClientRequest;
		TArray<FConcertSessionClientInfo> Clients;
		bool bSessionNameDirty = false; // Raised when the UI and the cache values may be out of sync if a rename failed (UI assumed it succeeded)
	};

	struct FArchivedSessionInfo
	{
		FConcertServerInfo ServerInfo;
		FConcertSessionInfo SessionInfo;
		bool bSessionNameDirty = false; // Raised when the UI and the cache values may be out of sync if a rename failed (UI assumed it succeeded)
	};

public:
	FConcertBrowserController(IConcertClientPtr InConcertClient);
	~FConcertBrowserController();

	IConcertClientPtr GetConcertClient() { return ConcertClient; }

	/**
	 * Fires new requests to retrieve all known server and for each server, their active and archived sessions. The responses are
	 * received asynchronously and may not be available right now. When a response is received, if the corresponding list cached
	 * is updated, the list version is incremented.
	 *
	 * @return A (serverListVersion, sessionsListVersion) pair, corresponding the the versions currently cached by this object.
	 */
	TPair<uint32, uint32> TickServersAndSessionsDiscovery();

	/**
	 * Fires a new request to retrieve the clients for the selected session. The result is cached and can be retrived by GetClients().
	 * The class caches clients for a single active session determined by this function. If the specified session changes, the cache
	 * of the previous session is cleared.
	 *
	 * @return The version of the client list currently cached by the object.
	 */
	uint32 TickClientsDiscovery(const FGuid& ServerAdminEndpointId, const FGuid& SessionId);

	/** Returns true if the controller received async responses and updated its cache since the last time the function was called, then clear the flag. */
	bool GetAndClearDiscoveryUpdateFlag();

	/** Returns the latest list of server known to this controller. Ensure to call TickServersAndSessionsDiscovery() periodically. */
	const TArray<FConcertServerInfo>& GetServers() const { return Servers; }

	/** Returns the latest list of active sessions known to this controller. Ensure to call TickServersAndSessionsDiscovery() periodically.*/
	const TArray<TSharedPtr<FActiveSessionInfo>>& GetActiveSessions() const { return ActiveSessions; }

	/** Returns the latest list of archived sessions known to this controller. Ensure to call TickServersAndSessionsDiscovery() periodically.*/
	const TArray<TSharedPtr<FArchivedSessionInfo>>& GetArchivedSessions() const { return ArchivedSessions; }

	/** Returns the latest list of clients corresponding to the session known to this controller. Ensure to call TickClientsDiscovery() periodically. */
	const TArray<FConcertSessionClientInfo>& GetClients(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const;

	/** Returns the active sessions info corresponding to the specified parameters. Used to display the sessions details. */
	const FConcertSessionInfo* GetActiveSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const;

	/** Returns the archived sessions info corresponding to the specified parameters. Used to display the sessions details. */
	const FConcertSessionInfo* GetArchivedSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const;

	void CreateSession(const FGuid& ServerAdminEndpointId, const FString& SessionName);
	void ArchiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& ArchiveName, const FConcertSessionFilter& SessionFilter);
	void RestoreSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& RestoredName, const FConcertSessionFilter& SessionFilter);
	void JoinSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId);
	void RenameActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName);
	void RenameArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName);
	bool CanRenameActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const;
	bool CanRenameArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const;
	void DeleteActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId);
	void DeleteArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId);
	bool CanDeleteActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const;
	bool CanDeleteArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const;

	// The 2 functions below are used to prevent fast UI transition with the 'no session' panel flashing when the session list is empty.
	bool HasReceivedInitialSessionList() const { return bInitialActiveSessionQueryResponded && bInitialArchivedSessionQueryResponded; }
	bool IsCreatingSession() const { return CreateSessionRequests.Num() > 0 || ExpectedSessionsToDiscover.Num() > 0; }

private:
	/** Hold information about a session created by this client, not yet 'discovered' by a 'list session' query, but expected to be soon. */
	struct FPendingSessionDiscovery
	{
		FDateTime CreateTimestamp;
		FGuid ServerEndpoint;
		FString SessionName;
	};

	void UpdateSessionsAsync();
	void UpdateActiveSessionsAsync(const FConcertServerInfo& ServerInfo);
	void UpdateArchivedSessionsAsync(const FConcertServerInfo& ServerInfo);
	void UpdateClientsAsync(const FGuid& ServerAdminEndpointId, const FGuid& SessionId);

	void OnActiveSessionDiscovered(const FActiveSessionInfo&) { ++SessionListVersion; bCacheUpdated = true; }
	void OnActiveSessionDiscarded(const FActiveSessionInfo&) { ++SessionListVersion; bCacheUpdated = true; }
	void OnArchivedSessionDiscovered(const FArchivedSessionInfo&) { ++SessionListVersion; bCacheUpdated = true; }
	void OnArchivedSessionDiscarded(const FArchivedSessionInfo&) { ++SessionListVersion; bCacheUpdated = true; }
	void OnActiveSessionClientsUpdated(const FActiveSessionInfo&) { ++ClientListVersion; bCacheUpdated = true; }
	void OnActiveSessionRenamed(const FActiveSessionInfo&, const FString& NewName) { ++SessionListVersion; bCacheUpdated = true; }
	void OnArchivedSessionRenamed(const FArchivedSessionInfo&, const FString& NewName) { ++SessionListVersion; bCacheUpdated = true; }
	void OnActiveSessionListDirty() { ++SessionListVersion; bCacheUpdated = true; } // This will force the UI to refresh its list.
	void OnArchivedSessionListDirty() { ++SessionListVersion; bCacheUpdated = true; } // This will force the UI to refresh its list.

private:
	// Holds a concert client instance.
	IConcertClientPtr ConcertClient;

	// The list of active/archived async requests (requesting the list of session) per server. There is only one per server as we prevent stacking more than one at the time.
	TMap<FGuid, FAsyncRequest> ActiveSessionRequests;
	TMap<FGuid, FAsyncRequest> ArchivedSessionRequests;

	// The cached lists.
	TArray<FConcertServerInfo> Servers;
	TArray<TSharedPtr<FActiveSessionInfo>> ActiveSessions;
	TArray<TSharedPtr<FArchivedSessionInfo>> ArchivedSessions;

	// The session for which the clients are monitored. UI only monitor client of 1 session at the time.
	TSharedPtr<FActiveSessionInfo> ClientMonitoredSession;

	// Holds the version of data cached by the controller. The version is updated when an async response is received and implies a change in the cached values.
	uint32 ServerListVersion = 0;
	uint32 SessionListVersion = 0;
	uint32 ClientListVersion = 0;
	bool bCacheUpdated = false;
	bool bInitialActiveSessionQueryResponded = false;
	bool bInitialArchivedSessionQueryResponded = false;

	TArray<FAsyncRequest> CreateSessionRequests;
	TArray<FPendingSessionDiscovery> ExpectedSessionsToDiscover;
	TSet<FString> IgnoredServers; // List of ignored servers (Useful for testing/debugging)
};


FConcertBrowserController::FConcertBrowserController(IConcertClientPtr InConcertClient)
{
	check(InConcertClient.IsValid()); // Don't expect this class to be instantiated if the concert client is not available.
	check(InConcertClient->IsConfigured()); // Expected to be done by higher level code.
	ConcertClient = InConcertClient;

	// When others servers are running, add them to the list if you want to test the UI displayed if no servers/no sessions exists.
	//IgnoredServers.Add(TEXT("wksyul10355")); // TODO: COMMENT BEFORE SUBMIT

	// Start server discovery to find the available Concert servers.
	ConcertClient->StartDiscovery();

	// Populate the session cache.
	TickServersAndSessionsDiscovery();
}

FConcertBrowserController::~FConcertBrowserController()
{
	if (ConcertClient.IsValid())
	{
		ConcertClient->StopDiscovery();
	}
}

const TArray<FConcertSessionClientInfo>& FConcertBrowserController::GetClients(const FGuid& AdminEndpoint, const FGuid& SessionId) const
{
	// If a session clients are monitored and the list is cached
	if (ClientMonitoredSession.IsValid() && ClientMonitoredSession->ServerInfo.AdminEndpointId == AdminEndpoint && ClientMonitoredSession->SessionInfo.SessionId == SessionId)
	{
		return ClientMonitoredSession->Clients; // Returns the list retrieved by the last TickClientsDiscovery().
	}

	// Returns an empty list for now. We expect the caller to call TickClientsDiscovery() periodically to maintain the session client list.
	static TArray<FConcertSessionClientInfo> EmptyClientList;
	return EmptyClientList;
}

const FConcertSessionInfo* FConcertBrowserController::GetActiveSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const
{
	const TSharedPtr<FActiveSessionInfo>* SessionInfo = ActiveSessions.FindByPredicate([&AdminEndpoint, &SessionId](const TSharedPtr<FActiveSessionInfo>& MatchCandidate)
	{
		return MatchCandidate->ServerInfo.AdminEndpointId == AdminEndpoint && MatchCandidate->SessionInfo.SessionId == SessionId;
	});

	return SessionInfo != nullptr ? &(*SessionInfo)->SessionInfo : nullptr;
}

const FConcertSessionInfo* FConcertBrowserController::GetArchivedSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const
{
	const TSharedPtr<FArchivedSessionInfo>* SessionInfo = ArchivedSessions.FindByPredicate([&AdminEndpoint, &SessionId](const TSharedPtr<FArchivedSessionInfo>& MatchCandidate)
	{
		return MatchCandidate->ServerInfo.AdminEndpointId == AdminEndpoint && MatchCandidate->SessionInfo.SessionId == SessionId;
	});

	return SessionInfo != nullptr ? &(*SessionInfo)->SessionInfo : nullptr;
}

void FConcertBrowserController::CreateSession(const FGuid& ServerAdminEndpointId, const FString& SessionName)
{
	FAsyncRequest& CreateRequest = CreateSessionRequests.AddDefaulted_GetRef();
	TWeakPtr<uint8, ESPMode::ThreadSafe> CreateRequestExecutionToken = CreateRequest.ResetExecutionToken();

	// On success, the client automatically joins the new session and SConcertBrowser::HandleSessionConnectionChanged() will transit the UI to the SActiveSession.
	// On failure: An async notification banner will be displayer to the user.
	FConcertCreateSessionArgs CreateSessionArgs;
	CreateSessionArgs.SessionName = SessionName;
	ConcertClient->CreateSession(ServerAdminEndpointId, CreateSessionArgs).Next([this, CreateRequestExecutionToken, ServerAdminEndpointId, SessionName](EConcertResponseCode ResponseCode)
	{
		if (TSharedPtr<uint8, ESPMode::ThreadSafe> ExecutionToken = CreateRequestExecutionToken.Pin())
		{
			if (ResponseCode == EConcertResponseCode::Success)
			{
				// Expect to find those session at some point in the future.
				ExpectedSessionsToDiscover.Add(FPendingSessionDiscovery{ FDateTime::UtcNow(), ServerAdminEndpointId, SessionName });
			}

			// Stop tracking the request.
			CreateSessionRequests.RemoveAll([&ExecutionToken](const FAsyncRequest& DiscardCandidate) { return DiscardCandidate.FutureExecutionToken == ExecutionToken; });
		}
	});
}

void FConcertBrowserController::ArchiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& ArchiveName, const FConcertSessionFilter& SessionFilter)
{
	// On success, an archived is created and TickServersAndSessionsDiscovery() will eventually discover it.
	// On failure: An async notification banner will be displayer to the user.
	FConcertArchiveSessionArgs ArchiveSessionArgs;
	ArchiveSessionArgs.SessionId = SessionId;
	ArchiveSessionArgs.ArchiveNameOverride = ArchiveName;
	ArchiveSessionArgs.SessionFilter = SessionFilter;
	ConcertClient->ArchiveSession(ServerAdminEndpointId, ArchiveSessionArgs);
}

void FConcertBrowserController::RestoreSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& RestoredName, const FConcertSessionFilter& SessionFilter)
{
	FString ArchivedSessionName;
	if (const FConcertSessionInfo* SessionInfo = GetArchivedSessionInfo(ServerAdminEndpointId, SessionId))
	{
		ArchivedSessionName = SessionInfo->SessionName;
	}

	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("RestoreSessionDialogTitle", "Restoring {0}"), FText::AsCultureInvariant(ArchivedSessionName)))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(1200, 800))
		.IsTopmostWindow(false) // Consider making it always on top?
		.SupportsMaximize(true)
		.SupportsMinimize(false);

	// Ask the stream to pull the activity details (for transaction/package) for inspection.
	constexpr bool bRequestActivityDetails = true;

	// Create a stream of activities (streaming from the most recent to the oldest).
	TSharedPtr<FConcertActivityStream> ActivityStream = MakeShared<FConcertActivityStream>(ConcertClient, ServerAdminEndpointId, SessionId, bRequestActivityDetails);

	// The UI uses this function to read and consume the activity stream.
	auto ReadActivitiesFn = [ActivityStream](TArray<TSharedPtr<FConcertClientSessionActivity>>& InOutActivities, int32& OutFetchCount, FText& OutErrorMsg)
	{
		return ActivityStream->Read(InOutActivities, OutFetchCount, OutErrorMsg);
	};

	// The UI uses this function to map an activity ID from the stream to a client info.
	auto GetActivityClientInfoFn = [ActivityStream](FGuid EndpointID)
	{
		return ActivityStream->GetActivityClientInfo(EndpointID);
	};

	// Invoked if the client selects a point in time to recover.
	TWeakPtr<IConcertClient, ESPMode::ThreadSafe> WeakClient = ConcertClient;
	auto OnAcceptRestoreFn = [WeakClient, ServerAdminEndpointId, SessionId, RestoredName, SessionFilter](TSharedPtr<FConcertClientSessionActivity> SelectedRecoveryActivity)
	{
		FConcertRestoreSessionArgs RestoreSessionArgs;
		RestoreSessionArgs.bAutoConnect = true;
		RestoreSessionArgs.SessionId = SessionId;
		RestoreSessionArgs.SessionName = RestoredName;
		RestoreSessionArgs.SessionFilter = SessionFilter;
		RestoreSessionArgs.SessionFilter.bOnlyLiveData = false;

		// Set which item was selected to recover through.
		if (SelectedRecoveryActivity)
		{
			RestoreSessionArgs.SessionFilter.ActivityIdUpperBound = SelectedRecoveryActivity->Activity.ActivityId;
		}
		// else -> Restore the entire session as it.

		bool bDismissRecoveryWindow = true; // Dismiss the window showing the session activities

		if (TSharedPtr<IConcertClient, ESPMode::ThreadSafe> ConcertClientPin = WeakClient.Pin())
		{
			// Prompt the user to persist and leave the session.
			bool bDisconnected = IMultiUserClientModule::Get().DisconnectSession(/*bAlwaysAskConfirmation*/true);
			if (bDisconnected)
			{
				// On success, a new session is created from the archive, the client automatically disconnects from the current session (if any), joins the restored one and SConcertBrowser::HandleSessionConnectionChanged() will transit the UI to the SActiveSession.
				// On failure: An async notification banner will be displayer to the user.
				ConcertClientPin->RestoreSession(ServerAdminEndpointId, RestoreSessionArgs);
			}
			else // The user declined disconnection.
			{
				bDismissRecoveryWindow = false; // Keep the window open, let the client handle it (close/cancel or just restore later).
			}
		}
		else
		{
			FAsyncTaskNotificationConfig NotificationConfig;
			NotificationConfig.bIsHeadless = false;
			NotificationConfig.bKeepOpenOnFailure = true;
			NotificationConfig.LogCategory = &LogConcert;

			FAsyncTaskNotification Notification(NotificationConfig);
			Notification.SetComplete(LOCTEXT("RecoveryError", "Failed to recover the session"), LOCTEXT("ClientUnavailable", "Concert client unavailable"), /*Success*/ false);
		}

		return bDismissRecoveryWindow;
	};

	TSharedRef<SConcertSessionRecovery> RestoreWidget = SNew(SConcertSessionRecovery)
		.ParentWindow(NewWindow)
		.IntroductionText(LOCTEXT("RecoverSessionIntroductionText", "Select the point in time at which the session should be restored"))
		.OnFetchActivities(ReadActivitiesFn)
		.OnMapActivityToClient(GetActivityClientInfoFn)
		.OnRestore(OnAcceptRestoreFn)
		.ClientNameColumnVisibility(EVisibility::Visible)
		.ClientAvatarColorColumnVisibility(EVisibility::Visible)
		.OperationColumnVisibility(EVisibility::Visible)
		.PackageColumnVisibility(EVisibility::Hidden) // Even tough the column is not present, the tooltips and summary contains the affected package.
		.DetailsAreaVisibility(bRequestActivityDetails ? EVisibility::Visible : EVisibility::Collapsed) // The activity stream was configured to pull the activity details.
		.IsConnectionActivityFilteringEnabled(true)
		.IsLockActivityFilteringEnabled(true);

	NewWindow->SetContent(RestoreWidget);
	FSlateApplication::Get().AddWindow(NewWindow, true);
}

void FConcertBrowserController::JoinSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId)
{
	// On success: The client joins the session and SConcertBrowser::HandleSessionConnectionChanged() will transit the UI to the SActiveSession.
	// On failure: An async notification banner will be displayer to the user.
	ConcertClient->JoinSession(ServerAdminEndpointId, SessionId);
}

void FConcertBrowserController::RenameActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName)
{
	// Find the currently cached session info.
	const TSharedPtr<FActiveSessionInfo>* SessionInfo = ActiveSessions.FindByPredicate([&ServerAdminEndpointId, &SessionId](const TSharedPtr<FActiveSessionInfo>& MatchCandidate)
	{
		return MatchCandidate->ServerInfo.AdminEndpointId == ServerAdminEndpointId && MatchCandidate->SessionInfo.SessionId == SessionId;
	});

	check(SessionInfo); // If the UI is displaying it, the UI backend should have it.
	ConcertClient->RenameSession(ServerAdminEndpointId, SessionId, NewName).Next([ActiveSessionInfo = *SessionInfo](EConcertResponseCode Response)
	{
		if (Response != EConcertResponseCode::Success)
		{
			ActiveSessionInfo->bSessionNameDirty = true; // Renamed failed, the UI may be displaying the wrong name. Force the UI to update against the latest cached values.
		}
		// else -> Succeeded -> TickServersAndSessionsDiscovery() will pick up the new name.
	});
}

void FConcertBrowserController::RenameArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName)
{
	// Find the currently cached session info.
	const TSharedPtr<FArchivedSessionInfo>* SessionInfo = ArchivedSessions.FindByPredicate([&ServerAdminEndpointId, &SessionId](const TSharedPtr<FArchivedSessionInfo>& MatchCandidate)
	{
		return MatchCandidate->ServerInfo.AdminEndpointId == ServerAdminEndpointId && MatchCandidate->SessionInfo.SessionId == SessionId;
	});

	check(SessionInfo); // If the UI is displaying it, the UI backend should have it.
	ConcertClient->RenameSession(ServerAdminEndpointId, SessionId, NewName).Next([SessionInfo = *SessionInfo](EConcertResponseCode Response)
	{
		if (Response != EConcertResponseCode::Success)
		{
			SessionInfo->bSessionNameDirty = true; // Renamed failed, the UI may be displaying the wrong name. Force the UI to update against the latest cached values.
		}
		// else -> Succeeded -> TickServersAndSessionsDiscovery() will pick up the new name.
	});
}

bool FConcertBrowserController::CanRenameActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const
{
	return CanDeleteActiveSession(ServerAdminEndpointId, SessionId); // Rename requires the same permission than delete.
}

bool FConcertBrowserController::CanRenameArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const
{
	return CanDeleteArchivedSession(ServerAdminEndpointId, SessionId); // Rename requires the same permission than delete.
}

void FConcertBrowserController::DeleteActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId)
{
	// On success, an session is deleted and TickServersAndSessionsDiscovery() will eventually notice it.
	// On failure: An async notification banner will be displayer to the user.
	ConcertClient->DeleteSession(ServerAdminEndpointId, SessionId);
}

void FConcertBrowserController::DeleteArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId)
{
	// On success, an archive is deleted and TickServersAndSessionsDiscovery() will eventually notice it.
	// On failure: An async notification banner will be displayer to the user.
	ConcertClient->DeleteSession(ServerAdminEndpointId, SessionId);
}

bool FConcertBrowserController::CanDeleteActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const
{
	const TSharedPtr<FActiveSessionInfo>* SessionInfo = ActiveSessions.FindByPredicate([&ServerAdminEndpointId, &SessionId](const TSharedPtr<FActiveSessionInfo>& MatchCandidate)
	{
		return MatchCandidate->ServerInfo.AdminEndpointId == ServerAdminEndpointId && MatchCandidate->SessionInfo.SessionId == SessionId;
	});

	// Can delete the session only if the concert client is the owner.
	return SessionInfo == nullptr ? false : ConcertClient->IsOwnerOf((*SessionInfo)->SessionInfo);
}

bool FConcertBrowserController::CanDeleteArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const
{
	const TSharedPtr<FArchivedSessionInfo>* SessionInfo = ArchivedSessions.FindByPredicate([&ServerAdminEndpointId, &SessionId](const TSharedPtr<FArchivedSessionInfo>& MatchCandidate)
	{
		return MatchCandidate->ServerInfo.AdminEndpointId == ServerAdminEndpointId && MatchCandidate->SessionInfo.SessionId == SessionId;
	});

	// Can delete the session only if the concert client is the owner.
	return SessionInfo == nullptr ? false : ConcertClient->IsOwnerOf((*SessionInfo)->SessionInfo);
}

TPair<uint32, uint32> FConcertBrowserController::TickServersAndSessionsDiscovery()
{
	// Fire new async queries to poll the sessions on all known servers.
	UpdateSessionsAsync();

	// Returns the versions corresponding to the last async responses received. The version are incremented when response to the asynchronous queries are received.
	return MakeTuple(ServerListVersion, SessionListVersion);
}

uint32 FConcertBrowserController::TickClientsDiscovery(const FGuid& ServerAdminEndpointId, const FGuid& SessionId)
{
	// Fire new async queries to poll clients for the selected server/session pair.
	UpdateClientsAsync(ServerAdminEndpointId, SessionId);

	// Returns the versions corresponding to the last async responses received. The version is incremented when response to the asynchronous query is received.
	return ClientListVersion;
}

bool FConcertBrowserController::GetAndClearDiscoveryUpdateFlag()
{
	bool bOldCacheChanged = bCacheUpdated; // This flag is raised every time an async response updates the cached data.
	bCacheUpdated = false;
	return bOldCacheChanged;
}

void FConcertBrowserController::UpdateSessionsAsync()
{
	// Get the list of known servers.
	TArray<FConcertServerInfo> OnlineServers = ConcertClient->GetKnownServers();
	if (IgnoredServers.Num())
	{
		OnlineServers.RemoveAll([this](const FConcertServerInfo& ServerInfo) { return IgnoredServers.Contains(ServerInfo.ServerName); });
	}

	bool bServerListVersionUpdated = false;

	// Detects which server(s) went offline since the last update.
	for (const FConcertServerInfo& ServerInfo : Servers)
	{
		// If a server, previously tracked, is not online anymore, remove all its sessions.
		if (!OnlineServers.ContainsByPredicate([this, &ServerInfo](const FConcertServerInfo& Visited) { return ServerInfo.InstanceInfo.InstanceId == Visited.InstanceInfo.InstanceId; }))
		{
			// Remove all active sessions corresponding to this server.
			ActiveSessions.RemoveAll([this, &ServerInfo](const TSharedPtr<FActiveSessionInfo>& ActiveSessionInfo)
			{
				if (ServerInfo.InstanceInfo.InstanceId == ActiveSessionInfo->ServerInfo.InstanceInfo.InstanceId)
				{
					OnActiveSessionDiscarded(*ActiveSessionInfo);
					return true;
				}
				return false;
			});

			// Remove all archived sessions corresponding to this server.
			ArchivedSessions.RemoveAll([this, &ServerInfo](const TSharedPtr<FArchivedSessionInfo>& ArchivedSessionInfo)
			{
				if (ServerInfo.InstanceInfo.InstanceId == ArchivedSessionInfo->ServerInfo.InstanceInfo.InstanceId)
				{
					OnArchivedSessionDiscarded(*ArchivedSessionInfo);
					return true;
				}
				return false;
			});

			// Disarm any active async request future for this server. Removing it from the map effectively disarm the future as the shared pointer used to as token gets released.
			ActiveSessionRequests.Remove(ServerInfo.InstanceInfo.InstanceId);
			ArchivedSessionRequests.Remove(ServerInfo.InstanceInfo.InstanceId);

			// The server went offline, update the server list version.
			ServerListVersion++;
			bServerListVersionUpdated = true;
		}
	}

	// For all online servers.
	for (const FConcertServerInfo& ServerInfo : OnlineServers)
	{
		// Check if this is a new server.
		if (!bServerListVersionUpdated && !Servers.ContainsByPredicate([&ServerInfo](const FConcertServerInfo& Visited) { return ServerInfo.InstanceInfo.InstanceId == Visited.InstanceInfo.InstanceId; }))
		{
			ServerListVersion++;
			bServerListVersionUpdated = true; // No need to look up further, only one new server is required to update the version.
		}

		// Poll the sessions for this online server.
		UpdateActiveSessionsAsync(ServerInfo);
		UpdateArchivedSessionsAsync(ServerInfo);
	}

	// Keep the list of online servers.
	Servers = MoveTemp(OnlineServers);
}

void FConcertBrowserController::UpdateActiveSessionsAsync(const FConcertServerInfo& ServerInfo)
{
	// Check if a request is already pulling active session from this server.
	FAsyncRequest& ListActiveSessionAsyncRequest = ActiveSessionRequests.FindOrAdd(ServerInfo.InstanceInfo.InstanceId);
	if (ListActiveSessionAsyncRequest.IsOngoing())
	{
		// Don't stack another async request on top of the ongoing one, TickServersAndSessionsDiscovery() will eventually catch up any possibly missed info here.
		return;
	}

	// Arm the future, enabling its continuation to execute (or not).
	TWeakPtr<uint8, ESPMode::ThreadSafe> ListActiveSessionExecutionToken = ListActiveSessionAsyncRequest.ResetExecutionToken();

	// Keep track of the request time.
	FDateTime ListRequestTimestamp = FDateTime::UtcNow();

	// Retrieve all live sessions currently known by this server.
	ListActiveSessionAsyncRequest.Future = ConcertClient->GetLiveSessions(ServerInfo.AdminEndpointId)
		.Next([this, ServerInfo, ListActiveSessionExecutionToken, ListRequestTimestamp](const FConcertAdmin_GetSessionsResponse& Response)
		{
			// If the future is disarmed.
			if (!ListActiveSessionExecutionToken.IsValid())
			{
				// Don't go further, the future execution was canceled, maybe because this object was deleted, the server removed from the list or it wasn't safe to get this future executing.
				return;
			}

			// If the server responded.
			if (Response.ResponseCode == EConcertResponseCode::Success)
			{
				bInitialActiveSessionQueryResponded = true;

				// Remove from the cache any session that were deleted from 'Server' since the last update. Find which one were renamed.
				ActiveSessions.RemoveAll([this, &ServerInfo, &Response](const TSharedPtr<FActiveSessionInfo>& DiscardCandidate)
				{
					// If the candidate is owned by another server.
					if (DiscardCandidate->ServerInfo.InstanceInfo.InstanceId != ServerInfo.InstanceInfo.InstanceId)
					{
						return false; // Keep that session, it's owned by another server.
					}
					// If the candidate is still active.
					else if (const FConcertSessionInfo* SessionFromServer = Response.Sessions.FindByPredicate([DiscardCandidate](const FConcertSessionInfo& MatchCandidate) { return DiscardCandidate->SessionInfo.SessionId == MatchCandidate.SessionId; }))
					{
						// Don't discard the session, it is still active, but check if it was renamed.
						if (SessionFromServer->SessionName != DiscardCandidate->SessionInfo.SessionName)
						{
							DiscardCandidate->SessionInfo.SessionName = SessionFromServer->SessionName; // Update the session name.
							OnActiveSessionRenamed(*DiscardCandidate, SessionFromServer->SessionName);
						}
						else if (DiscardCandidate->bSessionNameDirty) // Renaming this particular session failed?
						{
							OnActiveSessionListDirty(); // UI optimistically updated a session name from a rename, but it failed on the server. The controller caches the latest official server list unmodified. Force the client to resync its list against the cache.
							DiscardCandidate->bSessionNameDirty = false;
						}
						return false; // Don't remove the session, it's still active.
					}

					// The session is not active anymore on the server, it was discarded.
					OnActiveSessionDiscarded(*DiscardCandidate);
					return true;
				});

				// Add to the cache any session that was added to 'Server' since the last update.
				for (const FConcertSessionInfo& SessionInfo : Response.Sessions)
				{
					// Try to find the session in the list of active session.
					if (!ActiveSessions.ContainsByPredicate([&SessionInfo](const TSharedPtr<FActiveSessionInfo>& MatchCandidate)
						{ return SessionInfo.ServerInstanceId == MatchCandidate->ServerInfo.InstanceInfo.InstanceId && SessionInfo.SessionId == MatchCandidate->SessionInfo.SessionId; }))
					{
						// This is a newly discovered session, add it to the list.
						ActiveSessions.Add(MakeShared<FActiveSessionInfo>(FActiveSessionInfo{ServerInfo, SessionInfo}));
						OnActiveSessionDiscovered(*ActiveSessions.Last());
					}

					// Remove sessions that were created and discovered or those that should have shown up by now, but have not.
					ExpectedSessionsToDiscover.RemoveAll([&SessionInfo, &ListRequestTimestamp](const FPendingSessionDiscovery& DiscardCandidate)
					{
						bool bDiscovered = DiscardCandidate.ServerEndpoint == SessionInfo.ServerEndpointId && DiscardCandidate.SessionName == SessionInfo.SessionName; // Session was discovered as expected.
						bool bDeleted = ListRequestTimestamp > DiscardCandidate.CreateTimestamp; // The session was created successfully, a list request posted, but the session did not turn up. The session was deleted before it could be listed.
						return bDiscovered || bDeleted;
					});
				}
			}
			// else -> The concert request failed, possibly because the server went offline. Wait until next TickServersAndSessionsDiscovery() to sync the server again and discover if it went offline.
		});
}

void FConcertBrowserController::UpdateArchivedSessionsAsync(const FConcertServerInfo& ServerInfo)
{
	// Check if a request is already pulling archived sessions from this server.
	FAsyncRequest& ListArchivedSessionAsyncRequest = ArchivedSessionRequests.FindOrAdd(ServerInfo.InstanceInfo.InstanceId);
	if (ListArchivedSessionAsyncRequest.IsOngoing())
	{
		// Don't stack another async request on top of the ongoing one, TickServersAndSessionsDiscovery() will eventually catch up any possibly missed info here.
		return;
	}

	// Arm the future, enabling its continuation to execute.
	TWeakPtr<uint8, ESPMode::ThreadSafe> ListArchivedSessionExecutionToken = ListArchivedSessionAsyncRequest.ResetExecutionToken();

	// Retrieve the archived sessions.
	ListArchivedSessionAsyncRequest.Future = ConcertClient->GetArchivedSessions(ServerInfo.AdminEndpointId)
		.Next([this, ServerInfo, ListArchivedSessionExecutionToken](const FConcertAdmin_GetSessionsResponse& Response)
		{
			// If the future is disarmed.
			if (!ListArchivedSessionExecutionToken.IsValid())
			{
				// Don't go further, the future execution was canceled, maybe because this object was deleted, the server removed from the list or it wasn't safe to get this future executing.
				return;
			}

			// If the server responded.
			if (Response.ResponseCode == EConcertResponseCode::Success)
			{
				bInitialArchivedSessionQueryResponded = true;

				// Remove from the cache archives that were deleted from 'Server' since the last update. Find which one were renamed.
				ArchivedSessions.RemoveAll([this, &ServerInfo, &Response](const TSharedPtr<FArchivedSessionInfo>& DiscardCandidate)
				{
					// If the discard candidate is stored on another server.
					if (DiscardCandidate->ServerInfo.InstanceInfo.InstanceId != ServerInfo.InstanceInfo.InstanceId)
					{
						return false; // Keep that archive, it's stored on another server.
					}
					// If the archive is still stored on the server.
					else if (const FConcertSessionInfo* SessionFromServer = Response.Sessions.FindByPredicate([DiscardCandidate](const FConcertSessionInfo& MatchCandidate) { return DiscardCandidate->SessionInfo.SessionId == MatchCandidate.SessionId; }))
					{
						// Don't discard the session, it is still there. Check if it was renamed.
						if (SessionFromServer->SessionName != DiscardCandidate->SessionInfo.SessionName)
						{
							DiscardCandidate->SessionInfo.SessionName = SessionFromServer->SessionName; // Update the session name.
							OnArchivedSessionRenamed(*DiscardCandidate, SessionFromServer->SessionName);
						}
						else if (DiscardCandidate->bSessionNameDirty) // Renaming this particular session failed?
						{
							OnArchivedSessionListDirty(); // UI optimistically updated a session name from a rename, but it failed on the server. The controller caches the latest official server list unmodified. Force the client to resync its list against the cache.
							DiscardCandidate->bSessionNameDirty = false;
						}

						return false; // Don't remove the archive, it's still stored on the server.
					}

					OnArchivedSessionDiscarded(*DiscardCandidate);
					return true; // The session is not archived anymore on 'Server' anymore, remove it from the list.
				});

				// Add to the cache archives that was stored on 'Server' since the last update.
				for (const FConcertSessionInfo& SessionInfo : Response.Sessions)
				{
					// Try to find the archived in the list.
					if (!ArchivedSessions.ContainsByPredicate([&SessionInfo, &ServerInfo](const TSharedPtr<FArchivedSessionInfo>& MatchCandidate)
						{ return ServerInfo.InstanceInfo.InstanceId == MatchCandidate->ServerInfo.InstanceInfo.InstanceId && SessionInfo.SessionId == MatchCandidate->SessionInfo.SessionId; }))
					{
						// This is a newly discovered archive, add it to the list.
						ArchivedSessions.Add(MakeShared<FArchivedSessionInfo>(FArchivedSessionInfo{ServerInfo, SessionInfo}));
						OnArchivedSessionDiscovered(*ArchivedSessions.Last());
					}
				}
			}
			// else -> Request failed, will discovered what happened on next TickServersAndSessionsDiscovery().
		});
}

void FConcertBrowserController::UpdateClientsAsync(const FGuid& ServerAdminEndpointId, const FGuid& SessionId)
{
	// The clients request is for a different session than the one cached by the last client update (The UI only shows 1 at the time).
	if (ClientMonitoredSession.IsValid() && (ClientMonitoredSession->ServerInfo.AdminEndpointId != ServerAdminEndpointId || ClientMonitoredSession->SessionInfo.SessionId != SessionId))
	{
		// Reset the cache.
		ClientMonitoredSession->Clients.Reset();
		ClientMonitoredSession->ListClientRequest.Cancel();
		ClientMonitoredSession.Reset();
		ClientListVersion = 0;
	}

	// Find the session requested by the user.
	if (!ClientMonitoredSession.IsValid())
	{
		TSharedPtr<FActiveSessionInfo>* MatchEntry = ActiveSessions.FindByPredicate([&ServerAdminEndpointId, &SessionId](const TSharedPtr<FActiveSessionInfo>& MatchCandidate)
		{
			return MatchCandidate->ServerInfo.AdminEndpointId == ServerAdminEndpointId && MatchCandidate->SessionInfo.SessionId == SessionId;
		});

		// Update the active session.
		ClientMonitoredSession = MatchEntry != nullptr ? *MatchEntry : nullptr;
	}

	if (!ClientMonitoredSession.IsValid() || ClientMonitoredSession->ListClientRequest.IsOngoing())
	{
		// Don't stack another async request on top of the on-going one, TickClientsDiscovery() will eventually catch up any possibly missed info here.
		return;
	}

	// Arm the future, enabling its continuation to execute when called.
	TWeakPtr<uint8, ESPMode::ThreadSafe> ListClientExecutionToken = ClientMonitoredSession->ListClientRequest.ResetExecutionToken();

	// Retrieve (asynchronously) the clients corresponding to the selected server/session.
	ClientMonitoredSession->ListClientRequest.Future = ConcertClient->GetSessionClients(ServerAdminEndpointId, SessionId)
		.Next([this, ListClientExecutionToken](const FConcertAdmin_GetSessionClientsResponse& Response)
		{
			// If the future execution was canceled.
			if (!ListClientExecutionToken.IsValid())
			{
				// Don't go further, the future execution was canceled.
				return;
			}

			auto SortClientPredicate = [](const FConcertSessionClientInfo& Lhs, const FConcertSessionClientInfo& Rhs) { return Lhs.ClientEndpointId < Rhs.ClientEndpointId; };

			// If the request succeeded.
			if (Response.ResponseCode == EConcertResponseCode::Success)
			{
				if (ClientMonitoredSession->Clients.Num() != Response.SessionClients.Num())
				{
					ClientMonitoredSession->Clients = Response.SessionClients;
					ClientMonitoredSession->Clients.Sort(SortClientPredicate);
					OnActiveSessionClientsUpdated(*ClientMonitoredSession);
					return;
				}
				else if (Response.SessionClients.Num() == 0) // All existing clients disconnected.
				{
					ClientMonitoredSession->Clients.Reset();
					OnActiveSessionClientsUpdated(*ClientMonitoredSession);
				}
				else // Compare the old and the new list, both sorted by client endpoint id.
				{
					TArray<FConcertSessionClientInfo> SortedClients = Response.SessionClients;
					SortedClients.Sort(SortClientPredicate);

					int Index = 0;
					for (const FConcertSessionClientInfo& Client : SortedClients)
					{
						if (ClientMonitoredSession->Clients[Index].ClientEndpointId != Client.ClientEndpointId || // Not the same client?
						    ClientMonitoredSession->Clients[Index].ClientInfo != Client.ClientInfo)               // Client info was updated?
						{
							// The two lists are not identical, don't bother finding the 'delta', refresh all clients.
							ClientMonitoredSession->Clients = SortedClients;
							OnActiveSessionClientsUpdated(*ClientMonitoredSession);
							break;
						}
						++Index;
					}
				}
			}
		});
}


/**
 * The type of row used in the session list view to display active or archived session.
 */
class SSessionRow : public SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>
{
public:
	typedef TFunction<void(TSharedPtr<FConcertSessionItem>)> FDoubleClickFunc;
	typedef TFunction<void(TSharedPtr<FConcertSessionItem>, const FString&)> FRenameFunc;

	SLATE_BEGIN_ARGS(SSessionRow)
		: _OnDoubleClickFunc()
		, _OnRenameFunc()
		, _HighlightText()
		, _IsSelected(false)
	{
	}

	SLATE_ARGUMENT(FDoubleClickFunc, OnDoubleClickFunc)
	SLATE_ARGUMENT(FRenameFunc, OnRenameFunc)
	SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_ATTRIBUTE(bool, IsSelected)

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView, IConcertClientPtr InConcertClient);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	void OnSessionNameCommitted(const FText& NewSessionName, ETextCommit::Type CommitType);

private:
	void OnBeginEditingSessionName() { SessionNameText->EnterEditingMode(); }
	bool OnValidatingSessionName(const FText& NewSessionName, FText& OutError);

private:
	TWeakPtr<FConcertSessionItem> Item;
	FDoubleClickFunc DoubleClickFunc; // Invoked when the user double click on the row.
	FRenameFunc RenameFunc; // Invoked when the user commit the session rename. (This will send the request to server)
	TAttribute<FText> HighlightText;
	TAttribute<bool> IsSelected;
	TSharedPtr<SInlineEditableTextBlock> SessionNameText;
	IConcertClientPtr ConcertClient;
};


void SSessionRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView, IConcertClientPtr InConcertClient)
{
	Item = MoveTemp(InItem);
	DoubleClickFunc = InArgs._OnDoubleClickFunc; // This function should join a session or add a row to restore an archive.
	RenameFunc = InArgs._OnRenameFunc; // Function invoked to send a rename request to the server.
	HighlightText = InArgs._HighlightText;
	IsSelected = InArgs._IsSelected;
	ConcertClient = InConcertClient;

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

	// Listen and handle rename request.
	InItem->OnBeginEditSessionNameRequest.AddSP(this, &SSessionRow::OnBeginEditingSessionName);
}

TSharedRef<SWidget> SSessionRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();

	if (ColumnName == ConcertBrowserUtils::IconColName)
	{
		return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.Padding(2)
		.ToolTipText(ItemPin->Type == FConcertSessionItem::EType::ActiveSession ? LOCTEXT("ActiveIconTooltip", "Active session") : LOCTEXT("ArchivedIconTooltip", "Archived Session"))
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle(ConcertBrowserUtils::IconColumnFontName))
			.Text(ItemPin->Type == FConcertSessionItem::EType::ActiveSession ? FEditorFontGlyphs::Circle : FEditorFontGlyphs::Archive)
			.ColorAndOpacity(ItemPin->Type == FConcertSessionItem::EType::ActiveSession ? FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton.Success").Normal.TintColor : FSlateColor::UseSubduedForeground())
		];
	}

	bool bIsDefaultConfig = (ItemPin->Type == FConcertSessionItem::EType::ActiveSession && ItemPin->SessionName == ConcertClient->GetConfiguration()->DefaultSessionName && ItemPin->ServerName == ConcertClient->GetConfiguration()->DefaultServerURL);
	FSlateFontInfo FontInfo;
	FSlateColor FontColor;
	if (ItemPin->Type == FConcertSessionItem::EType::ActiveSession)
	{
		FontColor = bIsDefaultConfig ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor::White * 0.8f);
		FontInfo = FEditorStyle::Get().GetFontStyle("NormalFont");
	}
	else
	{
		FontColor = FSlateColor::UseSubduedForeground();
		FontInfo = FCoreStyle::GetDefaultFontStyle("Italic", 9);
	}

	if (ColumnName == ConcertBrowserUtils::SessionColName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SAssignNew(SessionNameText, SInlineEditableTextBlock)
				.Text_Lambda([this]() { return FText::AsCultureInvariant(Item.Pin()->SessionName); })
				.HighlightText(HighlightText)
				.OnTextCommitted(this, &SSessionRow::OnSessionNameCommitted)
				.IsReadOnly(false)
				.IsSelected(FIsSelected::CreateLambda([this]() { return IsSelected.Get(); }))
				.OnVerifyTextChanged(this, &SSessionRow::OnValidatingSessionName)
				.Font(FontInfo)
				.ColorAndOpacity(FontColor)
			];
	}

	check(ColumnName == ConcertBrowserUtils::ServerColName);

	if (bIsDefaultConfig)
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::Format(INVTEXT("{0} * "), FText::AsCultureInvariant(ItemPin->ServerName)))
				.HighlightText(HighlightText)
				.Font(FontInfo)
				.ColorAndOpacity(FontColor)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultServerSession", "(Default Session/Server)"))
				.HighlightText(HighlightText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FontColor)
			]
			+SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				ConcertBrowserUtils::MakeServerVersionIgnoredWidget(ItemPin->ServerFlags)
			];
	}
	else
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::AsCultureInvariant(ItemPin->ServerName))
				.HighlightText(HighlightText)
				.Font(FontInfo)
				.ColorAndOpacity(FontColor)
			]
			+SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				ConcertBrowserUtils::MakeServerVersionIgnoredWidget(ItemPin->ServerFlags)
			];
	}
}

FReply SSessionRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin())
	{
		DoubleClickFunc(ItemPin);
	}
	return FReply::Handled();
}

bool SSessionRow::OnValidatingSessionName(const FText& NewSessionName, FText& OutError)
{
	OutError = ConcertSettingsUtils::ValidateSessionName(NewSessionName.ToString());
	return OutError.IsEmpty();
}

void SSessionRow::OnSessionNameCommitted(const FText& NewSessionName, ETextCommit::Type CommitType)
{
	if (TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin())
	{
		FString NewName = NewSessionName.ToString();
		if (NewName != ItemPin->SessionName) // Was renamed?
		{
			if (ConcertSettingsUtils::ValidateSessionName(NewName).IsEmpty()) // Name is valid?
			{
				RenameFunc(ItemPin, NewName); // Send the rename request to the server. (Server may still refuse at this point)
			}
			else
			{
				// NOTE: Error are interactively detected and raised by OnValidatingSessionName()
				FSlateApplication::Get().SetKeyboardFocus(SessionNameText);
			}
		}
	}
}


/**
 * The type of row used in the session list view to edit a new session (the session name + server).
 */
class SNewSessionRow : public SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>
{
public:
	typedef TFunction<TPair<uint32, const TArray<FConcertServerInfo>&>()> FGetServersFunc;
	typedef TFunction<void(const TSharedPtr<FConcertSessionItem>&)> FAcceptFunc; // Should remove the editable 'new' row and creates the sessions.
	typedef TFunction<void(const TSharedPtr<FConcertSessionItem>&)> FDeclineFunc; // Should just remove the editable 'new' row.

	SLATE_BEGIN_ARGS(SNewSessionRow) // Needed to use Args because Construct() is limited to 5 arguments and 6 were required.
		: _GetServerFunc()
		, _OnAcceptFunc()
		, _OnDeclineFunc()
		, _HighlightText()
	{
	}

	SLATE_ARGUMENT(FGetServersFunc, GetServerFunc)
	SLATE_ARGUMENT(FAcceptFunc, OnAcceptFunc)
	SLATE_ARGUMENT(FDeclineFunc, OnDeclineFunc)
	SLATE_ATTRIBUTE(FText, HighlightText)

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView, IConcertClientPtr InConcertClient);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> OnGenerateServersComboOptionWidget(TSharedPtr<FConcertServerInfo> Item);
	TSharedRef<SWidget> MakeSelectedServerWidget();
	FText GetSelectedServerText() const;
	FText GetSelectedServerIgnoreVersionText() const;
	FText GetSelectedServerIgnoreVersionTooltip() const;
	FText GetServerDisplayName(const FString& ServerName) const;

	void OnSessionNameChanged(const FText& NewName);
	void OnSessionNameCommitted(const FText& NewText, ETextCommit::Type CommitType);
	FReply OnAccept();
	FReply OnDecline();
	FReply OnKeyDownHandler(const FGeometry&, const FKeyEvent&); // Registered as handler to the editable text (vs. OnKeyDown() virtual method).

	void UpdateServerList();

private:
	IConcertClientPtr ConcertClient;
	TWeakPtr<FConcertSessionItem> Item; // Holds the new item to fill with session name and server.
	TArray<TSharedPtr<FConcertServerInfo>> Servers; // Servers displayed in the server combo box.
	TSharedPtr<SComboBox<TSharedPtr<FConcertServerInfo>>> ServersComboBox;
	TSharedPtr<SEditableTextBox> EditableSessionName;
	FGetServersFunc GetServersFunc;
	FAcceptFunc AcceptFunc;
	FDeclineFunc DeclineFunc;
	TAttribute<FText> HighlightText;
	uint32 ServerListVersion = 0;
	bool bInitialFocusTaken = false;
};

void SNewSessionRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView, IConcertClientPtr InConcertClient)
{
	check(InConcertClient.IsValid());

	Item = MoveTemp(InItem);
	ConcertClient = InConcertClient;
	GetServersFunc = InArgs._GetServerFunc;
	AcceptFunc = InArgs._OnAcceptFunc;
	DeclineFunc = InArgs._OnDeclineFunc;
	HighlightText = InArgs._HighlightText;

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

	// Fill the server combo.
	UpdateServerList();
}

void SNewSessionRow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Check if the server list displayed by the combo box should be updated.
	if (GetServersFunc().Key != ServerListVersion)
	{
		UpdateServerList();
	}

	// Should give the focus to an editable text.
	if (!bInitialFocusTaken)
	{
		bInitialFocusTaken = FSlateApplication::Get().SetKeyboardFocus(EditableSessionName.ToSharedRef());
	}
}

TSharedRef<SWidget> SNewSessionRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();

	if (ColumnName == ConcertBrowserUtils::IconColName)
	{
		// 'New' icon
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle(ConcertBrowserUtils::IconColumnFontName))
					.Text(FEditorFontGlyphs::Plus_Circle)
			];
	}
	else if (ColumnName == ConcertBrowserUtils::SessionColName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0, 0, 2, 0))
			[
				SAssignNew(EditableSessionName, SEditableTextBox)
				.HintText(LOCTEXT("EnterSessionNameHint", "Enter a session name"))
				.OnTextCommitted(this, &SNewSessionRow::OnSessionNameCommitted)
				.OnKeyDownHandler(this, &SNewSessionRow::OnKeyDownHandler)
				.OnTextChanged(this, &SNewSessionRow::OnSessionNameChanged)
			];
	}
	else
	{
		return SNew(SHorizontalBox)

			// 'Server' combo
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 1)
			[
				SAssignNew(ServersComboBox, SComboBox<TSharedPtr<FConcertServerInfo>>)
				.OptionsSource(&Servers)
				.OnGenerateWidget(this, &SNewSessionRow::OnGenerateServersComboOptionWidget)
				.ToolTipText(LOCTEXT("SelectServerTooltip", "Select the server on which the session should be created"))
				[
					MakeSelectedServerWidget()
				]
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			.HAlign(HAlign_Left)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(1.0f, 0.0f))

				// 'Accept' button
				+SUniformGridPanel::Slot(0, 0)
				[
					ConcertBrowserUtils::MakeIconButton(
						TEXT("FlatButton.Success"),
						FEditorFontGlyphs::Check,
						LOCTEXT("CreateCheckIconTooltip", "Create the session"),
						TAttribute<bool>::Create([this]() { return !EditableSessionName->GetText().IsEmpty(); }),
						FOnClicked::CreateRaw(this, &SNewSessionRow::OnAccept),
						FSlateColor(FLinearColor::White))
				]

				// 'Decline' button
				+SUniformGridPanel::Slot(1, 0)
				[
					ConcertBrowserUtils::MakeIconButton(
						TEXT("FlatButton.Danger"),
						FEditorFontGlyphs::Times,
						LOCTEXT("CancelIconTooltip", "Cancel"),
						true, // Always enabled.
						FOnClicked::CreateRaw(this, &SNewSessionRow::OnDecline),
						FSlateColor(FLinearColor::White))
				]
			];
	}
}

TSharedRef<SWidget> SNewSessionRow::OnGenerateServersComboOptionWidget(TSharedPtr<FConcertServerInfo> ServerItem)
{
	bool bIsDefaultServer = ServerItem->ServerName == ConcertClient->GetConfiguration()->DefaultServerURL;

	FText Tooltip;
	if (bIsDefaultServer)
	{
		Tooltip = LOCTEXT("DefaultServerTooltip", "Default Configured Server");
	}
	else if (ServerItem->ServerName == FPlatformProcess::ComputerName())
	{
		Tooltip = LOCTEXT("LocalServerTooltip", "Local Server Running on This Computer");
	}
	else
	{
		Tooltip = LOCTEXT("OnlineServerTooltip", "Online Server");
	}

	return SNew(SHorizontalBox)
		.ToolTipText(Tooltip)

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Font(bIsDefaultServer ? FEditorStyle::GetFontStyle("BoldFont") : FEditorStyle::GetFontStyle("NormalFont"))
			.Text(GetServerDisplayName(ServerItem->ServerName))
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			ConcertBrowserUtils::MakeServerVersionIgnoredWidget(ServerItem->ServerFlags)
		];
}

void SNewSessionRow::UpdateServerList()
{
	// Remember the currently selected item (if any).
	TSharedPtr<FConcertServerInfo> SelectedItem = ServersComboBox->GetSelectedItem(); // Instance in current list.

	// Clear the current list. The list is rebuilt from scratch.
	Servers.Reset();

	TSharedPtr<FConcertServerInfo> LocalServerInfo;
	TSharedPtr<FConcertServerInfo> DefaultServerInfo;
	TSharedPtr<FConcertServerInfo> SelectedServerInfo; // Instance in the new list.

	const UConcertClientConfig* ConcertClientConfig = ConcertClient->GetConfiguration();

	// Update the list version.
	ServerListVersion = GetServersFunc().Key;

	// Convert to shared ptr (slate needs that) and find if the latest list contains a default/local server.
	for (const FConcertServerInfo& ServerInfo : GetServersFunc().Value)
	{
		TSharedPtr<FConcertServerInfo> ComboItem = MakeShared<FConcertServerInfo>(ServerInfo);

		if (ComboItem->ServerName == ConcertClientConfig->DefaultServerURL) // Default server is deemed more important than local server to display the icon aside the server.
		{
			DefaultServerInfo = ComboItem;
		}
		else if (ComboItem->ServerName == FPlatformProcess::ComputerName())
		{
			LocalServerInfo = ComboItem;
		}

		if (SelectedItem.IsValid() && SelectedItem->ServerName == ComboItem->ServerName)
		{
			SelectedServerInfo = ComboItem; // Preserve user selection using the new instance.
		}

		Servers.Emplace(MoveTemp(ComboItem));
	}

	// Sort the server list alphabetically.
	Servers.Sort([](const TSharedPtr<FConcertServerInfo>& Lhs, const TSharedPtr<FConcertServerInfo>& Rhs) { return Lhs->ServerName < Rhs->ServerName; });

	// If a server is running on this machine, put it first in the list.
	if (LocalServerInfo.IsValid() && Servers[0] != LocalServerInfo)
	{
		Servers.Remove(LocalServerInfo); // Keep sort order.
		Servers.Insert(LocalServerInfo, 0);
	}

	// If a 'default server' is configured and available, put it first in the list. (Possibly overruling the local one)
	if (DefaultServerInfo.IsValid() && Servers[0] != DefaultServerInfo)
	{
		Servers.Remove(DefaultServerInfo); // Keep sort order.
		Servers.Insert(DefaultServerInfo, 0);
	}

	// If a server was selected and is still in the updated list.
	if (SelectedServerInfo.IsValid())
	{
		// Preserve user selection.
		ServersComboBox->SetSelectedItem(SelectedServerInfo);
	}
	else if (Servers.Num())
	{
		// Select the very first item in the list which is most likely be the default or the local server as they were put first above.
		ServersComboBox->SetSelectedItem(Servers[0]);
	}
	else // Server list is empty.
	{
		ServersComboBox->ClearSelection();
		Servers.Reset();
	}

	ServersComboBox->RefreshOptions();
}

TSharedRef<SWidget> SNewSessionRow::MakeSelectedServerWidget()
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(this, &SNewSessionRow::GetSelectedServerText)
			.HighlightText(HighlightText)
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
			.Text(this, &SNewSessionRow::GetSelectedServerIgnoreVersionText)
			.ToolTipText(this, &SNewSessionRow::GetSelectedServerIgnoreVersionTooltip)
		];
}

FText SNewSessionRow::GetSelectedServerText() const
{
	TSharedPtr<FConcertServerInfo> SelectedServer = ServersComboBox->GetSelectedItem();
	if (SelectedServer.IsValid())
	{
		return GetServerDisplayName(SelectedServer->ServerName);
	}
	return LOCTEXT("SelectAServer", "Select a Server");
}

FText SNewSessionRow::GetServerDisplayName(const FString& ServerName) const
{
	bool bIsDefaultServer = ServerName == ConcertClient->GetConfiguration()->DefaultServerURL;
	if (bIsDefaultServer)
	{
		return FText::Format(LOCTEXT("DefaultServer", "{0} (Default)"), FText::FromString(FPlatformProcess::ComputerName()));
	}
	else if (ServerName == FPlatformProcess::ComputerName())
	{
		return FText::Format(LOCTEXT("MyComputer", "{0} (My Computer)"), FText::FromString(FPlatformProcess::ComputerName()));
	}
	return FText::FromString(ServerName);
}

FText SNewSessionRow::GetSelectedServerIgnoreVersionText() const
{
	if (ServersComboBox->GetSelectedItem() && (ServersComboBox->GetSelectedItem()->ServerFlags & EConcertServerFlags::IgnoreSessionRequirement) != EConcertServerFlags::None)
	{
		return FEditorFontGlyphs::Exclamation_Triangle;
	}
	return FText();
}

FText SNewSessionRow::GetSelectedServerIgnoreVersionTooltip() const
{
	if (ServersComboBox->GetSelectedItem() && (ServersComboBox->GetSelectedItem()->ServerFlags & EConcertServerFlags::IgnoreSessionRequirement) != EConcertServerFlags::None)
	{
		return ConcertBrowserUtils::GetServerVersionIgnoredTooltip();
	}
	return FText();
}

FReply SNewSessionRow::OnAccept()
{
	// Read the session name given by the user.
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();
	FString NewSessionName = EditableSessionName->GetText().ToString();

	FText InvalidNameErrorMsg = ConcertSettingsUtils::ValidateSessionName(NewSessionName);
	if (InvalidNameErrorMsg.IsEmpty()) // Name is valid?
	{
		ItemPin->SessionName = EditableSessionName->GetText().ToString();
		ItemPin->ServerName = ServersComboBox->GetSelectedItem()->ServerName;
		ItemPin->ServerAdminEndpointId = ServersComboBox->GetSelectedItem()->AdminEndpointId;
		AcceptFunc(ItemPin); // Delegate to create the session.
	}
	else
	{
		EditableSessionName->SetError(InvalidNameErrorMsg);
		FSlateApplication::Get().SetKeyboardFocus(EditableSessionName);
	}

	return FReply::Handled();
}

FReply SNewSessionRow::OnDecline()
{
	DeclineFunc(Item.Pin()); // Decline the creation and remove the row from the table.
	return FReply::Handled();
}

void SNewSessionRow::OnSessionNameChanged(const FText& NewName)
{
	EditableSessionName->SetError(ConcertSettingsUtils::ValidateSessionName(NewName.ToString()));
}

void SNewSessionRow::OnSessionNameCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		OnAccept(); // Create the session.
	}
}

FReply SNewSessionRow::OnKeyDownHandler(const FGeometry&, const FKeyEvent& KeyEvent)
{
	// NOTE: This is invoked when the editable text field has the focus.
	return KeyEvent.GetKey() == EKeys::Escape ? OnDecline() : FReply::Unhandled();
}


/**
 * The type of row used in the session list view to create and archive or restore a session (edit the information required for the operation).
 */
class SSaveRestoreSessionRow : public SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>
{
public:
	typedef TFunction<void(TSharedPtr<FConcertSessionItem>, const FString&)> FAcceptFunc; // Should remove the editable row and save or restore the session.
	typedef TFunction<void(TSharedPtr<FConcertSessionItem>)> FDeclineFunc; // Should only remove the editable row from the table.

	SLATE_BEGIN_ARGS(SSaveRestoreSessionRow)
		: _OnAcceptFunc()
		, _OnDeclineFunc()
		, _HighlightText()
	{
	}

	SLATE_ARGUMENT(FAcceptFunc, OnAcceptFunc)
	SLATE_ARGUMENT(FDeclineFunc, OnDeclineFunc)
	SLATE_ATTRIBUTE(FText, HighlightText)

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> Node, const TSharedRef<STableViewBase>& InOwnerTableView);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void OnSessionNameChanged(const FText& NewName);
	void OnSessionNameCommitted(const FText& NewText, ETextCommit::Type CommitType);
	FReply OnAccept();
	FReply OnDecline();
	FReply OnKeyDownHandler(const FGeometry&, const FKeyEvent&); // Registered as handler to the editable text (vs. OnKeyDown() virtual method for this widget).

	// Override SMultiColumnTableRow functions to ensure a wire is drawn between the item to restore and the editable row to link them together.
	virtual TBitArray<> GetWiresNeededByDepth() const override;
	virtual bool IsLastChild() const override { return true; }
	virtual int32 DoesItemHaveChildren() const override { return 0; }
	virtual bool IsItemExpanded() const override { return false; }

	FText GetDefaultName(const FConcertSessionItem& Item) const; // Generates a default name for an archive or a restored session.

private:
	TWeakPtr<FConcertSessionItem> Item;
	TSharedPtr<SEditableTextBox> EditableSessionName;
	FAcceptFunc AcceptFunc;
	FDeclineFunc DeclineFunc;
	TAttribute<FText> HighlightText;
	bool bInitialFocusTaken = false;
};


void SSaveRestoreSessionRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InNode, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = MoveTemp(InNode);
	AcceptFunc = InArgs._OnAcceptFunc;
	DeclineFunc = InArgs._OnDeclineFunc;
	HighlightText = InArgs._HighlightText;

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TBitArray<> SSaveRestoreSessionRow::GetWiresNeededByDepth() const
{
	TBitArray<> Bits;
	Bits.Add(false);
	return Bits;
}

FText SSaveRestoreSessionRow::GetDefaultName(const FConcertSessionItem& InItem) const
{
	if (InItem.Type == FConcertSessionItem::EType::SaveSession)
	{
		return FText::Format(LOCTEXT("DefaultName", "{0}.{1}"), FText::FromString(InItem.SessionName), FText::FromString(FDateTime::UtcNow().ToString()));
	}

	// Supposing the name of the archive has the dates as suffix, like SessionXYZ.2019.03.13-19.39.12, then extracts SessionXYZ
	static FRegexPattern Pattern(TEXT(R"((.*)\.\d+\.\d+\.\d+\-\d+\.\d+\.\d+$)"));
	FRegexMatcher Matcher(Pattern, InItem.SessionName);
	if (Matcher.FindNext())
	{
		return FText::FromString(Matcher.GetCaptureGroup(1));
	}

	return FText::FromString(InItem.SessionName);
}

TSharedRef<SWidget> SSaveRestoreSessionRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();

	if (ColumnName == ConcertBrowserUtils::IconColName)
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SExpanderArrow, SharedThis(this))
				.StyleSet(&FEditorStyle::Get())
				.ShouldDrawWires(true)
			];
	}
	else if (ColumnName == ConcertBrowserUtils::SessionColName)
	{
		return SNew(SHorizontalBox)

			// 'Restore as/Save as' text
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0, 0.0)
			[
				SNew(STextBlock).Text(ItemPin->Type == FConcertSessionItem::EType::RestoreSession ? LOCTEXT("RestoreAs", "Restore as:") : LOCTEXT("ArchiveAs", "Archive as:"))
			]

			// Editable text.
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0, 0, 2, 0))
			[
				SAssignNew(EditableSessionName, SEditableTextBox)
				.HintText(ItemPin->Type == FConcertSessionItem::EType::RestoreSession ? LOCTEXT("RestoreSessionHint", "Enter a session name") : LOCTEXT("ArchivSessionHint", "Enter an archive name"))
				.OnTextCommitted(this, &SSaveRestoreSessionRow::OnSessionNameCommitted)
				.OnKeyDownHandler(this, &SSaveRestoreSessionRow::OnKeyDownHandler)
				.OnTextChanged(this, &SSaveRestoreSessionRow::OnSessionNameChanged)
				.Text(GetDefaultName(*ItemPin))
				.SelectAllTextWhenFocused(true)
			];
	}
	else
	{
		check(ColumnName == ConcertBrowserUtils::ServerColName);

		// 'Server' text block.
		return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			[
				// Server.
				SNew(STextBlock)
				.Text(FText::FromString(ItemPin->ServerName))
				.HighlightText(HighlightText)
			]
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		.HAlign(HAlign_Left)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(1.0f, 0.0f))

			// 'Accept' button
			+SUniformGridPanel::Slot(0, 0)
			[
				ConcertBrowserUtils::MakeIconButton(
					TEXT("FlatButton.Success"),
					FEditorFontGlyphs::Check,
					ItemPin->Type == FConcertSessionItem::EType::RestoreSession ? LOCTEXT("RestoreCheckIconTooltip", "Restore the session") : LOCTEXT("ArchiveCheckIconTooltip", "Archive the session"),
					TAttribute<bool>::Create([this]() { return !EditableSessionName->GetText().IsEmpty(); }), // Enabled?
					FOnClicked::CreateRaw(this, &SSaveRestoreSessionRow::OnAccept),
					FSlateColor(FLinearColor::White))
			]

			// 'Cancel' button
			+SUniformGridPanel::Slot(1, 0)
			[
				ConcertBrowserUtils::MakeIconButton(
					TEXT("FlatButton.Danger"),
					FEditorFontGlyphs::Times,
					LOCTEXT("CancelTooltip", "Cancel"),
					true, // Enabled?
					FOnClicked::CreateRaw(this, &SSaveRestoreSessionRow::OnDecline),
					FSlateColor(FLinearColor::White))
			]
		];
	}
}

void SSaveRestoreSessionRow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Should give the focus to an editable text.
	if (!bInitialFocusTaken)
	{
		bInitialFocusTaken = FSlateApplication::Get().SetKeyboardFocus(EditableSessionName.ToSharedRef());
	}
}

void SSaveRestoreSessionRow::OnSessionNameChanged(const FText& NewName)
{
	EditableSessionName->SetError(ConcertSettingsUtils::ValidateSessionName(NewName.ToString()));
}

void SSaveRestoreSessionRow::OnSessionNameCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();
	if (CommitType == ETextCommit::Type::OnEnter)
	{
		OnAccept();
	}
}

FReply SSaveRestoreSessionRow::OnAccept()
{
	// Read the session name given by the user.
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();
	FString Name = EditableSessionName->GetText().ToString(); // Archive name or restored session name.

	// Ensure the user provided a name.
	FText InvalidNameErrorMsg = ConcertSettingsUtils::ValidateSessionName(Name);
	if (InvalidNameErrorMsg.IsEmpty()) // Name is valid?
	{
		AcceptFunc(ItemPin, Name); // Delegate archiving/restoring operation.
	}
	else
	{
		EditableSessionName->SetError(InvalidNameErrorMsg);
		FSlateApplication::Get().SetKeyboardFocus(EditableSessionName);
	}

	return FReply::Handled();
}

FReply SSaveRestoreSessionRow::OnDecline()
{
	DeclineFunc(Item.Pin()); // Remove the save/restore editable row.
	return FReply::Handled();
}

FReply SSaveRestoreSessionRow::OnKeyDownHandler(const FGeometry&, const FKeyEvent& KeyEvent)
{
	// NOTE: This handler is to capture the 'Escape' while the text field has focus.
	return KeyEvent.GetKey() == EKeys::Escape ? OnDecline() : FReply::Unhandled();
}


/**
 * Widget displayed when discovering multi-user server(s) or session(s).
 */
class SConcertDiscovery : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConcertDiscovery)
		: _Text()
		, _ThrobberVisibility(EVisibility::Visible)
		, _ButtonVisibility(EVisibility::Visible)
		, _IsButtonEnabled(true)
		, _ButtonStyle(&FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
		, _ButtonIcon()
		, _ButtonText()
		, _ButtonToolTip()
		, _OnButtonClicked()
	{
	}

	SLATE_ATTRIBUTE(FText, Text)
	SLATE_ATTRIBUTE(EVisibility, ThrobberVisibility)
	SLATE_ATTRIBUTE(EVisibility, ButtonVisibility)
	SLATE_ATTRIBUTE(bool, IsButtonEnabled)
	SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
	SLATE_ATTRIBUTE(const FSlateBrush*, ButtonIcon)
	SLATE_ATTRIBUTE(FText, ButtonText)
	SLATE_ATTRIBUTE(FText, ButtonToolTip)
	SLATE_EVENT( FOnClicked, OnButtonClicked)

	SLATE_END_ARGS();

public:
	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SCircularThrobber)
					.Visibility(InArgs._ThrobberVisibility)
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(InArgs._Text).Justification(ETextJustify::Center)
				]

				+SVerticalBox::Slot()
				.Padding(0, 4, 0, 0)
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonStyle(InArgs._ButtonStyle)
					.Visibility(InArgs._ButtonVisibility)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.IsEnabled(InArgs._IsButtonEnabled)
					.OnClicked(InArgs._OnButtonClicked)
					.ToolTipText(InArgs._ButtonToolTip)
					.ContentPadding(FMargin(8, 4))
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 3, 0)
						[
							SNew(SImage).Image(InArgs._ButtonIcon)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Bottom)
						[
							SNew(STextBlock).Text(InArgs._ButtonText)
						]
					]
				]
			]
		];
	}
};


/**
 * Displayed when something is not available.
 */
class SConcertNoAvailability : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConcertNoAvailability) : _Text() {}
	SLATE_ATTRIBUTE(FText, Text)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SConcertDiscovery) // Reuse this panel, but only show the message.
				.Text(InArgs._Text)
				.ThrobberVisibility(EVisibility::Collapsed)
				.ButtonVisibility(EVisibility::Collapsed)
		];
	}
};


/**
 * Enables the user to browse/search/filter/sort active and archived sessions, create new session,
 * archive active sessions, restore archived sessions, join a session and open the settings dialog.
 */
class SConcertSessionBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConcertSessionBrowser) { }
	SLATE_END_ARGS();

	/**
	* Constructs the Browser.
	* @param InArgs The Slate argument list.
	* @param InConcertClient The concert client used to list, join, delete the sessions.
	* @param[in,out] InSearchText The text to set in the search box and to remember (as output). Cannot be null.
	*/
	void Construct(const FArguments& InArgs, IConcertClientPtr InConcertClient, TSharedPtr<FText> InSearchText);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// Layout the 'session|details' split view.
	TSharedRef<SWidget> MakeBrowserContent();

	// Layout the sessions view and controls.
	TSharedRef<SWidget> MakeControlBar();
	TSharedRef<SWidget> MakeButtonBar();
	TSharedRef<SWidget> MakeSessionTableView();
	TSharedRef<SWidget> MakeSessionViewOptionsBar();
	TSharedRef<ITableRow> OnGenerateSessionRowWidget(TSharedPtr<FConcertSessionItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// Layouts the session detail panel.
	TSharedRef<SWidget> MakeSessionDetails(TSharedPtr<FConcertSessionItem> Item);
	TSharedRef<SWidget> MakeActiveSessionDetails(TSharedPtr<FConcertSessionItem> Item);
	TSharedRef<SWidget> MakeArchivedSessionDetails(TSharedPtr<FConcertSessionItem> Item);
	TSharedRef<ITableRow> OnGenerateClientRowWidget(TSharedPtr<FConcertSessionClientInfo> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void PopulateSessionInfoGrid(SGridPanel& Grid, const FConcertSessionInfo& SessionInfo);

	// Creates row widgets for session list view, validates user inputs and forward user requests for processing to a delegate function implemented by this class.
	TSharedRef<ITableRow> MakeActiveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ActiveItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeArchivedSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ArchivedItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeNewSessionRowWidget(const TSharedPtr<FConcertSessionItem>& NewItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeRestoreSessionRowWidget(const TSharedPtr<FConcertSessionItem>& RestoreItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeSaveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ArchivedItem, const TSharedRef<STableViewBase>& OwnerTable);

	// Creates the contextual menu when right clicking a session list view row.
	TSharedPtr<SWidget> MakeContextualMenu();

	// The buttons above the session view.
	bool IsNewButtonEnabled() const;
	bool IsJoinButtonEnabled() const;
	bool IsRestoreButtonEnabled() const;
	bool IsArchiveButtonEnabled() const;
	bool IsRenameButtonEnabled() const;
	bool IsDeleteButtonEnabled() const;
	bool IsLaunchServerButtonEnabled() const;
	bool IsAutoJoinButtonEnabled() const;
	bool IsCancelAutoJoinButtonEnabled() const;
	FReply OnNewButtonClicked();
	FReply OnJoinButtonClicked();
	FReply OnRestoreButtonClicked();
	FReply OnArchiveButtonClicked();
	FReply OnDeleteButtonClicked();
	FReply OnLaunchServerButtonClicked();
	FReply OnShutdownServerButtonClicked();
	FReply OnAutoJoinButtonClicked();
	FReply OnCancelAutoJoinButtonClicked();
	void OnBeginEditingSessionName(TSharedPtr<FConcertSessionItem> Item);

	// Manipulates the sessions view (the array and the UI).
	void OnSessionSelectionChanged(TSharedPtr<FConcertSessionItem> SelectedSession, ESelectInfo::Type SelectInfo);
	void InsertNewSessionEditableRow();
	void InsertRestoreSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& ArchivedItem);
	void InsertArchiveSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& ActiveItem);
	void InsertEditableSessionRow(TSharedPtr<FConcertSessionItem> EditableItem, TSharedPtr<FConcertSessionItem> ParentItem);
	void RemoveSessionRow(const TSharedPtr<FConcertSessionItem>& Item);

	// Sessions sorting. (Sorts the session view)
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;
	EColumnSortPriority::Type GetColumnSortPriority(const FName ColumnId) const;
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode);
	void SortSessionList();
	void EnsureEditableParentChildOrder();

	// Sessions filtering. (Filters the session view)
	void OnSearchTextChanged(const FText& InFilterText);
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);
	void OnFilterMenuChecked(const FName MenuName);
	void PopulateSearchStrings(const FConcertSessionItem& Item, TArray<FString>& OutSearchStrings) const;
	bool IsFilteredOut(const FConcertSessionItem& Item) const;
	FText HighlightSearchText() const;

	// Passes the user requests to FConcertBrowserController.
	void RequestCreateSession(const TSharedPtr<FConcertSessionItem>& NewItem);
	void RequestJoinSession(const TSharedPtr<FConcertSessionItem>& ActiveItem);
	void RequestArchiveSession(const TSharedPtr<FConcertSessionItem>& ActiveItem, const FString& ArchiveName);
	void RequestRestoreSession(const TSharedPtr<FConcertSessionItem>& RestoreItem, const FString& SessionName);
	void RequestRenameSession(const TSharedPtr<FConcertSessionItem>& RenamedItem, const FString& NewName);
	void RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& DeletedItem);

	// Update server/session/clients lists.
	EActiveTimerReturnType TickDiscovery(double InCurrentTime, float InDeltaTime);
	void UpdateDiscovery();
	void RefreshSessionList();
	void RefreshClientList(const TArray<FConcertSessionClientInfo>& LastestClientList);

private:
	// Gives access to the concert data (servers, sessions, clients, etc).
	TUniquePtr<FConcertBrowserController> Controller;

	// Keeps persistent user preferences, like the filters.
	TStrongObjectPtr<UConcertSessionBrowserSettings> PersitentSettings;

	// The items displayed in the session list view. It might be filtered and sorted compared to the full list hold by the controller.
	TArray<TSharedPtr<FConcertSessionItem>> Sessions;

	// The session list view.
	TSharedPtr<SListView<TSharedPtr<FConcertSessionItem>>> SessionsView;

	// The item corresponding to a row used to create/archive/restore a session. There is only one at the time
	TSharedPtr<FConcertSessionItem> EditableSessionRow;
	TSharedPtr<FConcertSessionItem> EditableSessionRowParent; // For archive/restore, indicate which element is archived or restored.

	// Sorting.
	EColumnSortMode::Type PrimarySortMode;
	EColumnSortMode::Type SecondarySortMode;
	FName PrimarySortedColumn;
	FName SecondarySortedColumn;

	// Filtering.
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<TTextFilter<const FConcertSessionItem&>> SearchTextFilter;
	TSharedPtr<FText> SearchedText;
	bool bRefreshSessionFilter = true;
	FString DefaultServerURL;

	// Selected Session Details.
	TSharedPtr<SBorder> SessionDetailsView;
	TSharedPtr<SExpandableArea> DetailsArea;
	TArray<TSharedPtr<FConcertSessionClientInfo>> Clients;
	TSharedPtr<SExpandableArea> ClientsArea;
	TSharedPtr<SListView<TSharedPtr<FConcertSessionClientInfo>>> ClientsView;

	// Used to compare the version used by UI versus the version cached in the controller.
	uint32 DisplayedSessionListVersion = 0;
	uint32 DisplayedClientListVersion = 0;
	uint32 ServerListVersion = 0;
	bool bLocalServerRunning = false;

	TSharedPtr<SWidget> ServerDiscoveryPanel; // Displayed until a server is found.
	TSharedPtr<SWidget> SessionDiscoveryPanel; // Displayed until a session is found.
	TSharedPtr<SWidget> NoSessionSelectedPanel; // Displays 'select a session to view details' message in the details section.
	TSharedPtr<SWidget> NoSessionDetailsPanel; // Displays 'no details available' message in the details section.
	TSharedPtr<SWidget> NoClientPanel; // Displays the 'no client connected' message in Clients expendable area.
};


void SConcertSessionBrowser::Construct(const FArguments& InArgs, IConcertClientPtr InConcertClient, TSharedPtr<FText> InSearchText)
{
	if (!InConcertClient.IsValid())
	{
		return; // Don't build the UI if ConcertClient is not available.
	}

	Controller = MakeUnique<FConcertBrowserController>(InConcertClient);

	// Reload the persistent settings, such as the filters.
	PersitentSettings = TStrongObjectPtr<UConcertSessionBrowserSettings>(GetMutableDefault<UConcertSessionBrowserSettings>());

	// Setup search filter.
	SearchedText = InSearchText; // Reload a previous search text (in any). Useful to remember searched text between join/leave sessions, but not persistent if the tab is closed.
	SearchTextFilter = MakeShared<TTextFilter<const FConcertSessionItem&>>(TTextFilter<const FConcertSessionItem&>::FItemToStringArray::CreateSP(this, &SConcertSessionBrowser::PopulateSearchStrings));
	SearchTextFilter->OnChanged().AddSP(this, &SConcertSessionBrowser::RefreshSessionList);

	// Displayed if no server is available.
	ServerDiscoveryPanel = SNew(SConcertDiscovery)
		.Text(LOCTEXT("LookingForServer", "Looking for Multi-User Servers..."))
		.Visibility_Lambda([this]() { return Controller->GetServers().Num() == 0 ? EVisibility::Visible : EVisibility::Hidden; })
		.IsButtonEnabled(this, &SConcertSessionBrowser::IsLaunchServerButtonEnabled)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
		.ButtonIcon(FConcertFrontendStyle::Get()->GetBrush("Concert.NewServer.Small"))
		.ButtonText(LOCTEXT("LaunchLocalServer", "Launch a Server"))
		.ButtonToolTip(LOCTEXT("LaunchServerTooltip", "Launch a Multi-User server on your computer unless one is already running"))
		.OnButtonClicked(this, &SConcertSessionBrowser::OnLaunchServerButtonClicked);

	// Controls the text displayed in the 'No sessions' panel.
	auto GetNoSessionText = [this]()
	{
		if (!Controller->HasReceivedInitialSessionList())
		{
			return LOCTEXT("LookingForSession", "Looking for Multi-User Sessions...");
		}

		return Controller->GetActiveSessions().Num() == 0 && Controller->GetArchivedSessions().Num() == 0 ?
			LOCTEXT("NoSessionAvailable", "No Sessions Available") :
			LOCTEXT("AllSessionsFilteredOut", "No Sessions Match the Filters\nChange Your Filter to View Sessions");
	};

	// Displayed when discovering session or if no session is available.
	SessionDiscoveryPanel = SNew(SConcertDiscovery)
		.Text_Lambda(GetNoSessionText)
		.Visibility_Lambda([this]() { return Controller->GetServers().Num() > 0 && Sessions.Num() == 0 && !Controller->IsCreatingSession() ? EVisibility::Visible : EVisibility::Hidden; })
		.ThrobberVisibility_Lambda([this]() { return !Controller->HasReceivedInitialSessionList() ? EVisibility::Visible : EVisibility::Collapsed; })
		.ButtonVisibility_Lambda([this]() { return Controller->HasReceivedInitialSessionList() && Controller->GetActiveSessions().Num() == 0 && Controller->GetArchivedSessions().Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
		.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
		.ButtonIcon(FConcertFrontendStyle::Get()->GetBrush("Concert.NewSession.Small"))
		.ButtonText(LOCTEXT("CreateSession", "Create Session"))
		.ButtonToolTip(LOCTEXT("CreateSessionTooltip", "Create a new session"))
		.OnButtonClicked(FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnNewButtonClicked));

	// Displayed when the selected session client view is empty (no client to display).
	NoClientPanel = SNew(SConcertNoAvailability)
		.Text(LOCTEXT("NoClientAvailable", "No Connected Clients"))
		.Visibility_Lambda([this]() { return Clients.Num() == 0 ? EVisibility::Visible : EVisibility::Hidden; });

	// Displayed as details when no session is selected. (No session selected or the selected session doesn't have any)
	NoSessionSelectedPanel = SNew(SConcertNoAvailability)
		.Text(LOCTEXT("NoSessionSelected", "Select a Session to View Details"));

	// Displayed as details when the selected session has not specific details to display.
	NoSessionDetailsPanel = SNew(SConcertNoAvailability)
		.Text(LOCTEXT("NoSessionDetails", "The Selected Session Has No Details"));

	// List used in details panel to display clients connected to an active session.
	ClientsView = SNew(SListView<TSharedPtr<FConcertSessionClientInfo>>)
		.ListItemsSource(&Clients)
		.OnGenerateRow(this, &SConcertSessionBrowser::OnGenerateClientRowWidget)
		.SelectionMode(ESelectionMode::Single)
		.AllowOverscroll(EAllowOverscroll::No);

	ChildSlot
	[
		MakeBrowserContent()
	];

	// Create a timer to periodically poll the server for sessions and session clients at a lower frequency than the normal tick.
	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SConcertSessionBrowser::TickDiscovery));

	if (!SearchedText->IsEmpty())
	{
		SearchBox->SetText(*SearchedText); // This trigger the chain of actions to apply the search filter.
	}

	bLocalServerRunning = IMultiUserClientModule::Get().IsConcertServerRunning();
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeBrowserContent()
{
	return SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			// Splitter upper part displaying the available sessions/(server).
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			.MinimumSlotHeight(80.0f) // Prevent widgets from overlapping.
			+SSplitter::Slot()
			.Value(0.6)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(1.0f, 2.0f))
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						MakeControlBar()
					]

					// Session list.
					+SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(1.0f, 2.0f)
					[
						SNew(SOverlay)
						+SOverlay::Slot()
						[
							MakeSessionTableView()
						]
						+SOverlay::Slot()
						.Padding(0, 20, 0, 0) // To ensure the panel is below the row header.
						[
							SessionDiscoveryPanel.ToSharedRef()
						]
						+SOverlay::Slot()
						.Padding(0, 20, 0, 0) // To ensure the panel is below the row header.
						[
							ServerDiscoveryPanel.ToSharedRef()
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(2.0f, 0.0f))
					[
						SNew(SSeparator)
					]

					// Session Count/View options filter.
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(2.0f, 0.0f))
					[
						MakeSessionViewOptionsBar()
					]
				]
			]

			// Session details.
			+SSplitter::Slot()
			.Value(0.4)
			[
				SAssignNew(SessionDetailsView, SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					NoSessionSelectedPanel.ToSharedRef()
				]
			]
		];
}

EActiveTimerReturnType SConcertSessionBrowser::TickDiscovery(double InCurrentTime, float InDeltaTime)
{
	// Cache the result of this function because it is very expensive. It kills the framerate if polled every frame.
	bLocalServerRunning = IMultiUserClientModule::Get().IsConcertServerRunning();

	UpdateDiscovery();
	return EActiveTimerReturnType::Continue;
}

void SConcertSessionBrowser::UpdateDiscovery()
{
	// Check if the controller has updated data since the last 'tick' and fire new asynchronous requests for future 'tick'.
	TPair<uint32, uint32> ServerSessionListVersions = Controller->TickServersAndSessionsDiscovery();
	ServerListVersion = ServerSessionListVersions.Key;

	if (ServerSessionListVersions.Value != DisplayedSessionListVersion) // Need to refresh the list?
	{
		RefreshSessionList();
		DisplayedSessionListVersion = ServerSessionListVersions.Value;
	}

	// If an active session is selected.
	TArray<TSharedPtr<FConcertSessionItem>> SelectedSession = SessionsView->GetSelectedItems();
	if (SelectedSession.Num() && SelectedSession[0]->Type == FConcertSessionItem::EType::ActiveSession)
	{
		// Ensure to poll its clients.
		uint32 CachedClientListVersion = Controller->TickClientsDiscovery(SelectedSession[0]->ServerAdminEndpointId, SelectedSession[0]->SessionId);
		if (CachedClientListVersion != DisplayedClientListVersion) // Need to refresh the list?
		{
			RefreshClientList(Controller->GetClients(SelectedSession[0]->ServerAdminEndpointId, SelectedSession[0]->SessionId));
			DisplayedClientListVersion = CachedClientListVersion;
		}
	}
}

void SConcertSessionBrowser::RefreshSessionList()
{
	// Remember the selected instances (if any).
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	TArray<TSharedPtr<FConcertSessionItem>> ReselectedItems;
	TSharedPtr<FConcertSessionItem> NewEditableRowParent;

	// Predicate returning true if the specified item should be re-selected.
	auto IsSelected = [&SelectedItems](const FConcertSessionItem& Item)
	{
		return SelectedItems.ContainsByPredicate([Item](const TSharedPtr<FConcertSessionItem>& Visited) { return *Visited == Item; });
	};

	// Matches the object instances before the update to the new instance after the update.
	auto ReconcileObjectInstances = [&](TSharedPtr<FConcertSessionItem> NewItem)
	{
		if (IsSelected(*NewItem))
		{
			ReselectedItems.Add(NewItem);
		}
		else if (EditableSessionRowParent.IsValid() && !NewEditableRowParent.IsValid() && *EditableSessionRowParent == *NewItem)
		{
			NewEditableRowParent = NewItem;
		}
	};

	// Clear sessions.
	Sessions.Reset();

	// Populate the live sessions.
	for (const TSharedPtr<FConcertBrowserController::FActiveSessionInfo>& ActiveSession : Controller->GetActiveSessions())
	{
		FConcertSessionItem NewItem(FConcertSessionItem::EType::ActiveSession, ActiveSession->SessionInfo.SessionName, ActiveSession->SessionInfo.SessionId, ActiveSession->ServerInfo.ServerName, ActiveSession->ServerInfo.AdminEndpointId, ActiveSession->ServerInfo.ServerFlags);
		if (!IsFilteredOut(NewItem))
		{
			Sessions.Emplace(MakeShared<FConcertSessionItem>(MoveTemp(NewItem)));
			ReconcileObjectInstances(Sessions.Last());
		}
	}

	// Populate the archived.
	for (const TSharedPtr<FConcertBrowserController::FArchivedSessionInfo>& ArchivedSession : Controller->GetArchivedSessions())
	{
		FConcertSessionItem NewItem(FConcertSessionItem::EType::ArchivedSession, ArchivedSession->SessionInfo.SessionName, ArchivedSession->SessionInfo.SessionId, ArchivedSession->ServerInfo.ServerName, ArchivedSession->ServerInfo.AdminEndpointId, ArchivedSession->ServerInfo.ServerFlags);
		if (!IsFilteredOut(NewItem))
		{
			Sessions.Emplace(MakeShared<FConcertSessionItem>(MoveTemp(NewItem)));
			ReconcileObjectInstances(Sessions.Last());
		}
	}

	// Restore the editable row state. (SortSessionList() below will ensure the parent/child relationship)
	EditableSessionRowParent = NewEditableRowParent;
	if (EditableSessionRow.IsValid())
	{
		if (EditableSessionRow->Type == FConcertSessionItem::EType::NewSession)
		{
			Sessions.Insert(EditableSessionRow, 0); // Always put 'new session' row at the top.
		}
		else if (EditableSessionRowParent.IsValid())
		{
			Sessions.Add(EditableSessionRow); // SortSessionList() called below will ensure the correct parent/child order.
		}
	}

	// Restore previous selection.
	if (ReselectedItems.Num())
	{
		for (const TSharedPtr<FConcertSessionItem>& Item : ReselectedItems)
		{
			SessionsView->SetItemSelection(Item, true);
		}
	}

	SortSessionList();
	SessionsView->RequestListRefresh();
}

void SConcertSessionBrowser::RefreshClientList(const TArray<FConcertSessionClientInfo>& LastestClientList)
{
	// Remember which client is selected.
	TArray<TSharedPtr<FConcertSessionClientInfo>> SelectedItems = ClientsView->GetSelectedItems();

	// Copy the list of clients.
	TArray<TSharedPtr<FConcertSessionClientInfo>> LatestClientPtrs;
	Algo::Transform(LastestClientList, LatestClientPtrs, [](const FConcertSessionClientInfo& Client) { return MakeShared<FConcertSessionClientInfo>(Client); });

	// Merge the current list with the new list, removing client that disappeared and adding client that appeared.
	ConcertFrontendUtils::SyncArraysByPredicate(Clients, MoveTemp(LatestClientPtrs), [](const TSharedPtr<FConcertSessionClientInfo>& ClientToFind)
	{
		return [ClientToFind](const TSharedPtr<FConcertSessionClientInfo>& PotentialClientMatch)
		{
			return PotentialClientMatch->ClientEndpointId == ClientToFind->ClientEndpointId && PotentialClientMatch->ClientInfo == ClientToFind->ClientInfo;
		};
	});

	// Sort the list item alphabetically.
	Clients.StableSort([](const TSharedPtr<FConcertSessionClientInfo>& Lhs, const TSharedPtr<FConcertSessionClientInfo>& Rhs)
	{
		return Lhs->ClientInfo.DisplayName < Rhs->ClientInfo.DisplayName;
	});

	// Preserve previously selected item (if any).
	if (SelectedItems.Num())
	{
		ClientsView->SetSelection(SelectedItems[0]);
	}

	ClientsView->RequestListRefresh();
}

void SConcertSessionBrowser::OnSearchTextChanged(const FText& InFilterText)
{
	SearchTextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError( SearchTextFilter->GetFilterErrorText() );
	*SearchedText = InFilterText;

	bRefreshSessionFilter = true;
}

void SConcertSessionBrowser::OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType)
{
	if (!InFilterText.EqualTo(*SearchedText))
	{
		OnSearchTextChanged(InFilterText);
	}
}

void SConcertSessionBrowser::PopulateSearchStrings(const FConcertSessionItem& Item, TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(Item.ServerName);
	OutSearchStrings.Add(Item.SessionName);
}

bool SConcertSessionBrowser::IsFilteredOut(const FConcertSessionItem& Item) const
{
	bool bIsDefaultServer = DefaultServerURL.IsEmpty() || Item.ServerName == DefaultServerURL;

	return (!PersitentSettings->bShowActiveSessions && (Item.Type == FConcertSessionItem::EType::ActiveSession || Item.Type == FConcertSessionItem::EType::SaveSession)) ||
	       (!PersitentSettings->bShowArchivedSessions && (Item.Type == FConcertSessionItem::EType::ArchivedSession || Item.Type == FConcertSessionItem::EType::RestoreSession)) ||
	       (PersitentSettings->bShowDefaultServerSessionsOnly && !bIsDefaultServer) ||
	       !SearchTextFilter->PassesFilter(Item);
}

FText SConcertSessionBrowser::HighlightSearchText() const
{
	return *SearchedText;
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeControlBar()
{
	return SNew(SHorizontalBox)

		// The New/Join/Restore/Delete/Archive buttons
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeButtonBar()
		]

		// The search text.
		+SHorizontalBox::Slot()
		.FillWidth(1.0)
		.Padding(4.0f, 3.0f, 8.0f, 3.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search Session"))
			.OnTextChanged(this, &SConcertSessionBrowser::OnSearchTextChanged)
			.OnTextCommitted(this, &SConcertSessionBrowser::OnSearchTextCommitted)
			.DelayChangeNotificationsWhileTyping(true)
		]

		// The user "Avatar color" displayed as a small square colored by the user avatar color.
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
			.ColorAndOpacity_Lambda([this]() { return Controller->GetConcertClient()->GetClientInfo().AvatarColor; })
			.Text(FEditorFontGlyphs::Square)
		]

		// The user "Display Name".
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(3, 0, 2, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() {return FText::FromString(Controller->GetConcertClient()->GetClientInfo().DisplayName);} )
		]

		// The "Settings" icons.
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Fill)
		.AutoWidth()
		.Padding(0, 0, 1, 0)
		[
			SNew(SButton)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton"))
			.OnClicked_Lambda([](){FModuleManager::GetModulePtr<ISettingsModule>("Settings")->ShowViewer("Project", "Plugins", "Concert"); return FReply::Handled(); })
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
				.Text(FEditorFontGlyphs::Cogs)
			]
		];
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeButtonBar()
{
	TAttribute<FText> AutoJoinTooltip = TAttribute<FText>::Create([this]()
	{
		if (Controller->GetConcertClient()->CanAutoConnect()) // Default session and server are configured?
		{
			return FText::Format(LOCTEXT("JoinDefaultSessionTooltip", "Join the default session '{0}' on '{1}'"),
				FText::FromString(Controller->GetConcertClient()->GetConfiguration()->DefaultSessionName),
				FText::FromString(Controller->GetConcertClient()->GetConfiguration()->DefaultServerURL));
		}
		else
		{
			return LOCTEXT("JoinDefaultSessionConfiguredTooltip", "Join the default session configured in the Multi-Users settings");
		}
	});

	TAttribute<FText> CancelAutoJoinTooltip = TAttribute<FText>::Create([this]()
	{
		return FText::Format(LOCTEXT("CancelJoinDefaultSessionTooltip", "Cancel joining the default session '{0}' on '{1}'"),
			FText::FromString(Controller->GetConcertClient()->GetConfiguration()->DefaultSessionName),
			FText::FromString(Controller->GetConcertClient()->GetConfiguration()->DefaultServerURL));
	});

	constexpr float PaddingBetweenButtons = 3.0f;

	return SNew(SHorizontalBox)

		// Launch server.
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.NewServer"), LOCTEXT("LaunchServerTooltip", "Launch a Multi-User server on your computer unless one is already running"),
				TAttribute<bool>(this, &SConcertSessionBrowser::IsLaunchServerButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnLaunchServerButtonClicked),
				TAttribute<EVisibility>::Create([this]() { return IsLaunchServerButtonEnabled() ? EVisibility::Visible : EVisibility::Collapsed; }))
		]

		// Stop server.
		+SHorizontalBox::Slot()
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.CloseServer"), LOCTEXT("ShutdownServerTooltip", "Shutdown the Multi-User server running on this computer."),
				true, // Always enabled, but might be collapsed.
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnShutdownServerButtonClicked),
				TAttribute<EVisibility>::Create([this]() { return IsLaunchServerButtonEnabled() ? EVisibility::Collapsed : EVisibility::Visible; }))
		]

		// New Session
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.NewSession"), LOCTEXT("NewButtonTooltip", "Create a new session"),
				TAttribute<bool>(this, &SConcertSessionBrowser::IsNewButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnNewButtonClicked))
		]

		// Separator
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 1, PaddingBetweenButtons, 1)
		[
			SNew(SSeparator).Orientation(Orient_Vertical)
		]

		// Auto-join
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.JoinDefaultSession"), AutoJoinTooltip,
				TAttribute<bool>(this, &SConcertSessionBrowser::IsAutoJoinButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnAutoJoinButtonClicked),
				TAttribute<EVisibility>::Create([this]() { return !IsCancelAutoJoinButtonEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })) // Default button shown if both auto-join/cancel are disabled.
		]

		// Cancel auto join. (Shares the same slot as Auto-join)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.CancelAutoJoin"), CancelAutoJoinTooltip,
				TAttribute<bool>(this, &SConcertSessionBrowser::IsCancelAutoJoinButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnCancelAutoJoinButtonClicked),
				TAttribute<EVisibility>::Create([this]() { return IsCancelAutoJoinButtonEnabled() ? EVisibility::Visible : EVisibility::Collapsed; }))
		]

		// Join
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.JoinSession"), LOCTEXT("JoinButtonTooltip", "Join the selected session"),
				TAttribute<bool>(this, &SConcertSessionBrowser::IsJoinButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnJoinButtonClicked),
				TAttribute<EVisibility>::Create([this]() { return !IsRestoreButtonEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })) // Default button shown if both join/restore are disabled.
		]

		// Restore (Share the same slot as Join)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.RestoreSession"), LOCTEXT("RestoreButtonTooltip", "Restore the selected session"),
				TAttribute<bool>(this, &SConcertSessionBrowser::IsRestoreButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnRestoreButtonClicked),
				TAttribute<EVisibility>::Create([this]() { return IsRestoreButtonEnabled() ? EVisibility::Visible : EVisibility::Collapsed; }))
		]

		// Archive.
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, PaddingBetweenButtons, 0)
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.ArchiveSession"), LOCTEXT("ArchiveButtonTooltip", "Archive the selected session"),
				TAttribute<bool>(this, &SConcertSessionBrowser::IsArchiveButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnArchiveButtonClicked))
		]

		// Delete.
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			ConcertBrowserUtils::MakeIconButton(TEXT("FlatButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.DeleteSession"), LOCTEXT("DeleteButtonTooltip", "Delete the selected session if permitted"),
				TAttribute<bool>(this, &SConcertSessionBrowser::IsDeleteButtonEnabled),
				FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnDeleteButtonClicked))
		];
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeSessionTableView()
{
	PrimarySortedColumn = ConcertBrowserUtils::IconColName;
	PrimarySortMode = EColumnSortMode::Ascending;
	SecondarySortedColumn = ConcertBrowserUtils::SessionColName;
	SecondarySortMode = EColumnSortMode::Ascending;

	return SAssignNew(SessionsView, SListView<TSharedPtr<FConcertSessionItem>>)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(&Sessions)
		.OnGenerateRow(this, &SConcertSessionBrowser::OnGenerateSessionRowWidget)
		.SelectionMode(ESelectionMode::Single)
		.OnSelectionChanged(this, &SConcertSessionBrowser::OnSessionSelectionChanged)
		.OnContextMenuOpening(this, &SConcertSessionBrowser::MakeContextualMenu)
		.HeaderRow(
			SNew(SHeaderRow)

			+SHeaderRow::Column(ConcertBrowserUtils::IconColName)
			.DefaultLabel(FText::GetEmpty())
			.SortPriority(this, &SConcertSessionBrowser::GetColumnSortPriority, ConcertBrowserUtils::IconColName)
			.SortMode(this, &SConcertSessionBrowser::GetColumnSortMode, ConcertBrowserUtils::IconColName)
			.OnSort(this, &SConcertSessionBrowser::OnColumnSortModeChanged)
			.FixedWidth(20)

			+SHeaderRow::Column(ConcertBrowserUtils::SessionColName)
			.DefaultLabel(LOCTEXT("SessioName", "Session"))
			.SortPriority(this, &SConcertSessionBrowser::GetColumnSortPriority, ConcertBrowserUtils::SessionColName)
			.SortMode(this, &SConcertSessionBrowser::GetColumnSortMode, ConcertBrowserUtils::SessionColName)
			.OnSort(this, &SConcertSessionBrowser::OnColumnSortModeChanged)

			+SHeaderRow::Column(ConcertBrowserUtils::ServerColName)
			.DefaultLabel(LOCTEXT("Server", "Server"))
			.SortPriority(this, &SConcertSessionBrowser::GetColumnSortPriority, ConcertBrowserUtils::ServerColName)
			.SortMode(this, &SConcertSessionBrowser::GetColumnSortMode, ConcertBrowserUtils::ServerColName)
			.OnSort(this, &SConcertSessionBrowser::OnColumnSortModeChanged));
}

EColumnSortMode::Type SConcertSessionBrowser::GetColumnSortMode(const FName ColumnId) const
{
	if (ColumnId == PrimarySortedColumn)
	{
		return PrimarySortMode;
	}
	else if (ColumnId == SecondarySortedColumn)
	{
		return SecondarySortMode;
	}

	return EColumnSortMode::None;
}

EColumnSortPriority::Type SConcertSessionBrowser::GetColumnSortPriority(const FName ColumnId) const
{
	if (ColumnId == PrimarySortedColumn)
	{
		return EColumnSortPriority::Primary;
	}
	else if (ColumnId == SecondarySortedColumn)
	{
		return EColumnSortPriority::Secondary;
	}

	return EColumnSortPriority::Max; // No specific priority.
}

void SConcertSessionBrowser::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	if (SortPriority == EColumnSortPriority::Primary)
	{
		PrimarySortedColumn = ColumnId;
		PrimarySortMode = InSortMode;

		if (ColumnId == SecondarySortedColumn) // Cannot be primary and secondary at the same time.
		{
			SecondarySortedColumn = FName();
			SecondarySortMode = EColumnSortMode::None;
		}
	}
	else if (SortPriority == EColumnSortPriority::Secondary)
	{
		SecondarySortedColumn = ColumnId;
		SecondarySortMode = InSortMode;
	}

	SortSessionList();
	SessionsView->RequestListRefresh();
}

void SConcertSessionBrowser::SortSessionList()
{
	check(!PrimarySortedColumn.IsNone()); // Should always have a primary column. User cannot clear this one.

	auto Compare = [this](const TSharedPtr<FConcertSessionItem>& Lhs, const TSharedPtr<FConcertSessionItem>& Rhs, const FName& ColName, EColumnSortMode::Type SortMode)
	{
		if (Lhs->Type == FConcertSessionItem::EType::NewSession) // Always keep editable 'new session' row at the top.
		{
			return true;
		}
		else if (Rhs->Type == FConcertSessionItem::EType::NewSession)
		{
			return false;
		}

		if (ColName == ConcertBrowserUtils::IconColName)
		{
			return SortMode == EColumnSortMode::Ascending ? Lhs->Type < Rhs->Type : Lhs->Type > Rhs->Type;
		}
		else if (ColName == ConcertBrowserUtils::SessionColName)
		{
			return SortMode == EColumnSortMode::Ascending ? Lhs->SessionName < Rhs->SessionName : Lhs->SessionName > Rhs->SessionName;
		}
		else
		{
			return SortMode == EColumnSortMode::Ascending ? Lhs->ServerName < Rhs->ServerName : Lhs->ServerName > Rhs->ServerName;
		}
	};

	Sessions.Sort([&](const TSharedPtr<FConcertSessionItem>& Lhs, const TSharedPtr<FConcertSessionItem>& Rhs)
	{
		if (Compare(Lhs, Rhs, PrimarySortedColumn, PrimarySortMode))
		{
			return true; // Lhs must be before Rhs based on the primary sort order.
		}
		else if (Compare(Rhs, Lhs, PrimarySortedColumn, PrimarySortMode)) // Invert operands order (goal is to check if operands are equal or not)
		{
			return false; // Rhs must be before Lhs based on the primary sort.
		}
		else // Lhs == Rhs on the primary column, need to order accoding the secondary column if one is set.
		{
			return SecondarySortedColumn.IsNone() ? false : Compare(Lhs, Rhs, SecondarySortedColumn, SecondarySortMode);
		}
	});

	EnsureEditableParentChildOrder();
}

void SConcertSessionBrowser::EnsureEditableParentChildOrder()
{
	// This is for Archiving or Restoring a session. We keep the editable row below the session to archive or restore and visually link them with small wires in UI.
	if (EditableSessionRowParent.IsValid())
	{
		check(EditableSessionRow.IsValid());
		Sessions.Remove(EditableSessionRow);

		int32 ParentIndex = Sessions.IndexOfByKey(EditableSessionRowParent);
		if (ParentIndex != INDEX_NONE)
		{
			Sessions.Insert(EditableSessionRow, ParentIndex + 1); // Insert the 'child' below its parent.
		}
	}
}

TSharedRef<ITableRow> SConcertSessionBrowser::OnGenerateSessionRowWidget(TSharedPtr<FConcertSessionItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	switch (Item->Type)
	{
		case FConcertSessionItem::EType::ActiveSession:
			return MakeActiveSessionRowWidget(Item, OwnerTable);

		case FConcertSessionItem::EType::ArchivedSession:
			return MakeArchivedSessionRowWidget(Item, OwnerTable);

		case FConcertSessionItem::EType::NewSession:
			return MakeNewSessionRowWidget(Item, OwnerTable);

		case FConcertSessionItem::EType::RestoreSession:
			return MakeRestoreSessionRowWidget(Item, OwnerTable);

		default:
			check(Item->Type == FConcertSessionItem::EType::SaveSession);
			return MakeSaveSessionRowWidget(Item, OwnerTable);
	}
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeActiveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ActiveItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FConcertSessionInfo* SessionInfo = Controller->GetActiveSessionInfo(ActiveItem->ServerAdminEndpointId, ActiveItem->SessionId);

	// Add an 'Active Session' row. Clicking the row icon joins the session.
	return SNew(SSessionRow, ActiveItem, OwnerTable, Controller->GetConcertClient())
		.OnDoubleClickFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RequestJoinSession(Item); })
		.OnRenameFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& NewName) { RequestRenameSession(Item, NewName); })
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText)
		.ToolTipText(SessionInfo ? SessionInfo->ToDisplayString() : FText::GetEmpty())
		.IsSelected_Lambda([this, ActiveItem]() { return SessionsView->GetSelectedItems().Num() == 1 && SessionsView->GetSelectedItems()[0] == ActiveItem; });
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeArchivedSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ArchivedItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FConcertSessionInfo* SessionInfo = Controller->GetArchivedSessionInfo(ArchivedItem->ServerAdminEndpointId, ArchivedItem->SessionId);

	// Add an 'Archived Session' row. Clicking the row icon adds a 'Restore as' row to the table.
	return SNew(SSessionRow, ArchivedItem, OwnerTable, Controller->GetConcertClient())
		.OnDoubleClickFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { InsertRestoreSessionAsEditableRow(Item); })
		.OnRenameFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& NewName) { RequestRenameSession(Item, NewName); })
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText)
		.ToolTipText(SessionInfo ? SessionInfo->ToDisplayString() : FText::GetEmpty())
		.IsSelected_Lambda([this, ArchivedItem]() { return SessionsView->GetSelectedItems().Num() == 1 && SessionsView->GetSelectedItems()[0] == ArchivedItem; });
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeNewSessionRowWidget(const TSharedPtr<FConcertSessionItem>& NewItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Add an editable 'New Session' row in the table to let user pick a name and a server.
	return SNew(SNewSessionRow, NewItem, OwnerTable, Controller->GetConcertClient())
		.GetServerFunc([this]() { return TPair<uint32, const TArray<FConcertServerInfo>&>(ServerListVersion, Controller->GetServers()); }) // Let the row pull the servers for the combo box.
		.OnAcceptFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RequestCreateSession(Item); }) // Accepting creates the session.
		.OnDeclineFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RemoveSessionRow(Item); })  // Declining removes the editable 'new session' row from the view.
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText);
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeSaveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& SaveItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Add an editable 'Save Session' row in the table to let the user enter an archive name.
	return SNew(SSaveRestoreSessionRow, SaveItem, OwnerTable)
		.OnAcceptFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& ArchiveName) { RequestArchiveSession(Item, ArchiveName); }) // Accepting archive the session.
		.OnDeclineFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RemoveSessionRow(Item); }) // Declining removes the editable 'save session as' row from the view.
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText);
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeRestoreSessionRowWidget(const TSharedPtr<FConcertSessionItem>& RestoreItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Add an editable 'Restore Session' row in the table to let the user enter a session name.
	return SNew(SSaveRestoreSessionRow, RestoreItem, OwnerTable)
		.OnAcceptFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& SessionName) { RequestRestoreSession(Item, SessionName); }) // Accepting restores the session.
		.OnDeclineFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RemoveSessionRow(Item); }) // Declining removes the editable 'restore session as' row from the view.
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText);
}

void SConcertSessionBrowser::InsertNewSessionEditableRow()
{
	// Insert a 'new session' editable row.
	InsertEditableSessionRow(MakeShared<FConcertSessionItem>(FConcertSessionItem::EType::NewSession, FString(), FGuid(), FString(), FGuid(), EConcertServerFlags::None), nullptr);
}

void SConcertSessionBrowser::InsertRestoreSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& ArchivedItem)
{
	// Insert the 'restore session as ' editable row just below the 'archived' item to restore.
	InsertEditableSessionRow(MakeShared<FConcertSessionItem>(FConcertSessionItem::EType::RestoreSession, ArchivedItem->SessionName, ArchivedItem->SessionId, ArchivedItem->ServerName, ArchivedItem->ServerAdminEndpointId, ArchivedItem->ServerFlags), ArchivedItem);
}

void SConcertSessionBrowser::InsertArchiveSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& LiveItem)
{
	// Insert the 'save session as' editable row just below the 'active' item to save.
	InsertEditableSessionRow(MakeShared<FConcertSessionItem>(FConcertSessionItem::EType::SaveSession, LiveItem->SessionName, LiveItem->SessionId, LiveItem->ServerName, LiveItem->ServerAdminEndpointId, LiveItem->ServerFlags), LiveItem);
}

void SConcertSessionBrowser::InsertEditableSessionRow(TSharedPtr<FConcertSessionItem> EditableItem, TSharedPtr<FConcertSessionItem> ParentItem)
{
	// Insert the new row below its parent (if any).
	int32 ParentIndex = Sessions.IndexOfByKey(ParentItem);
	Sessions.Insert(EditableItem, ParentIndex != INDEX_NONE ? ParentIndex + 1 : 0);

	// Ensure there is only one editable row at the time, removing the row being edited (if any).
	Sessions.Remove(EditableSessionRow);
	EditableSessionRow = EditableItem;
	EditableSessionRowParent = ParentItem;

	// Ensure the editable row added is selected and visible.
	SessionsView->SetSelection(EditableItem);
	SessionsView->RequestListRefresh();

	// NOTE: Ideally, I would only use RequestScrollIntoView() to scroll the new item into view, but it did not work. If an item was added into an hidden part,
	//       it was not always scrolled correctly into view. RequestNavigateToItem() worked much better, except when inserting the very first row in the list, in
	//       such case calling the function would give the focus to the list view (showing a white dashed line around it).
	if (ParentIndex == INDEX_NONE)
	{
		SessionsView->ScrollToTop(); // Item is inserted at 0. (New session)
	}
	else
	{
		SessionsView->RequestNavigateToItem(EditableItem);
	}
}

void SConcertSessionBrowser::RemoveSessionRow(const TSharedPtr<FConcertSessionItem>& Item)
{
	Sessions.Remove(Item);

	// Don't keep the editable row if its 'parent' is removed. (if the session to restore or archive gets deleted in the meantime)
	if (Item == EditableSessionRowParent)
	{
		Sessions.Remove(EditableSessionRow);
		EditableSessionRow.Reset();
	}

	// Clear the editable row state if its the one removed.
	if (Item == EditableSessionRow)
	{
		EditableSessionRow.Reset();
		EditableSessionRowParent.Reset();
	}

	SessionsView->RequestListRefresh();
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeSessionViewOptionsBar()
{
	auto AddFilterMenu = [this]()
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ActiveSessions_Label", "Active Sessions"),
			LOCTEXT("ActiveSessions_Tooltip", "Displays Active Sessions"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertSessionBrowser::OnFilterMenuChecked, ConcertBrowserUtils::ActiveSessionsCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return PersitentSettings->bShowActiveSessions; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ArchivedSessions_Label", "Archived Sessions"),
			LOCTEXT("ArchivedSessions_Tooltip", "Displays Archived Sessions"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertSessionBrowser::OnFilterMenuChecked, ConcertBrowserUtils::ArchivedSessionsCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return PersitentSettings->bShowArchivedSessions; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DefaultServer_Label", "Default Server Sessions"),
			LOCTEXT("DefaultServer_Tooltip", "Displays Sessions Hosted By the Default Server"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertSessionBrowser::OnFilterMenuChecked, ConcertBrowserUtils::DefaultServerCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return PersitentSettings->bShowDefaultServerSessionsOnly; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		return MenuBuilder.MakeWidget();
	};

	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text_Lambda([this]()
			{
				// Don't count the 'New Session', 'Restore Session' and 'Archive Session' editable row, they are transient rows used for inline input only.
				int32 DisplayedSessionNum = Sessions.Num() - (EditableSessionRow.IsValid() ? 1 : 0);
				int32 AvailableSessionNum = Controller->GetActiveSessions().Num() + Controller->GetArchivedSessions().Num();
				int32 ServerNum = Controller->GetServers().Num();

				// If all discovered session are displayed (none excluded by a filter).
				if (DisplayedSessionNum == AvailableSessionNum)
				{
					if (Controller->GetServers().Num() == 0)
					{
						return LOCTEXT("NoServerNoFilter", "No servers found");
					}
					else
					{
						return FText::Format(LOCTEXT("NSessionNServerNoFilter", "{0} {0}|plural(one=session,other=sessions) on {1} {1}|plural(one=server,other=servers)"),
							DisplayedSessionNum, ServerNum);
					}
				}
				else // A filter is excluding at least one session.
				{
					if (DisplayedSessionNum == 0)
					{
						return FText::Format(LOCTEXT("NoSessionMatchNServer", "No matching sessions ({0} total on {1} {1}|plural(one=server,other=servers))"),
							AvailableSessionNum, ServerNum);
					}
					else
					{
						return FText::Format(LOCTEXT("NSessionNServer", "Showing {0} of {1} {1}|plural(one=session,other=sessions) on {2} {2}|plural(one=server,other=servers)"),
							DisplayedSessionNum, AvailableSessionNum, ServerNum);
					}
				}
			})
		]

		+SHorizontalBox::Slot()
		.FillWidth(1.0)
		[
			SNew(SSpacer)
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(0)
			.OnGetMenuContent_Lambda(AddFilterMenu)
			.HasDownArrow(true)
			.ContentPadding(FMargin(1, 0))
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage).Image(FEditorStyle::GetBrush("GenericViewButton")) // The eye ball image.
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("ViewOptions", "View Options"))
				]
			]
		];
}

void SConcertSessionBrowser::OnFilterMenuChecked(const FName MenuName)
{
	if (MenuName == ConcertBrowserUtils::ActiveSessionsCheckBoxMenuName)
	{
		PersitentSettings->bShowActiveSessions = !PersitentSettings->bShowActiveSessions;
	}
	else if (MenuName == ConcertBrowserUtils::ArchivedSessionsCheckBoxMenuName)
	{
		PersitentSettings->bShowArchivedSessions = !PersitentSettings->bShowArchivedSessions;
	}
	else if (MenuName == ConcertBrowserUtils::DefaultServerCheckBoxMenuName)
	{
		PersitentSettings->bShowDefaultServerSessionsOnly = !PersitentSettings->bShowDefaultServerSessionsOnly;
	}
	bRefreshSessionFilter = true;

	PersitentSettings->SaveConfig();
}

TSharedPtr<SWidget> SConcertSessionBrowser::MakeContextualMenu()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 0 || (SelectedItems[0]->Type != FConcertSessionItem::EType::ActiveSession && SelectedItems[0]->Type != FConcertSessionItem::EType::ArchivedSession))
	{
		return nullptr; // No menu for editable rows.
	}

	TSharedPtr<FConcertSessionItem> Item = SelectedItems[0];

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	// Section title.
	MenuBuilder.BeginSection(NAME_None, Item->Type == FConcertSessionItem::EType::ActiveSession ?
		LOCTEXT("ActiveSessionSection", "Active Session"):
		LOCTEXT("ArchivedSessionSection", "Archived Session"));

	if (Item->Type == FConcertSessionItem::EType::ActiveSession)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CtxMenuJoin", "Join"),
			LOCTEXT("CtxMenuJoin_Tooltip", "Join the Session"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ OnJoinButtonClicked(); }),
				FCanExecuteAction::CreateLambda([SelectedCount = SelectedItems.Num()] { return SelectedCount == 1; }),
				FIsActionChecked::CreateLambda([this] { return false; })),
			NAME_None,
			EUserInterfaceActionType::Button);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CtxMenuArchive", "Archive"),
			LOCTEXT("CtxMenuArchive_Tooltip", "Archived the Session"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ OnArchiveButtonClicked(); }),
				FCanExecuteAction::CreateLambda([SelectedCount = SelectedItems.Num()] { return SelectedCount == 1; }),
				FIsActionChecked::CreateLambda([this] { return false; })),
			NAME_None,
			EUserInterfaceActionType::Button);
	}
	else // Archive
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CtxMenuRestore", "Restore"),
			LOCTEXT("CtxMenuRestore_Tooltip", "Restore the Session"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ OnRestoreButtonClicked(); }),
				FCanExecuteAction::CreateLambda([SelectedCount = SelectedItems.Num()] { return SelectedCount == 1; }),
				FIsActionChecked::CreateLambda([this] { return false; })),
			NAME_None,
			EUserInterfaceActionType::Button);
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CtxMenuRename", "Rename"),
		LOCTEXT("CtxMenuRename_Tooltip", "Rename the Session"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, Item](){ OnBeginEditingSessionName(Item); }),
			FCanExecuteAction::CreateLambda([this] { return IsRenameButtonEnabled(); }),
			FIsActionChecked::CreateLambda([this] { return false; })),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CtxMenuDelete", "Delete"),
		FText::Format(LOCTEXT("CtxMenuDelete_Tooltip", "Delete the {0}|plural(one=Session,other=Sessions)"), SelectedItems.Num()),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ OnDeleteButtonClicked(); }),
			FCanExecuteAction::CreateLambda([this] { return IsDeleteButtonEnabled(); }),
			FIsActionChecked::CreateLambda([this] { return false; })),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeSessionDetails(TSharedPtr<FConcertSessionItem> Item)
{
	const FConcertSessionInfo* SessionInfo = nullptr;

	if (Item.IsValid())
	{
		if (Item->Type == FConcertSessionItem::EType::ActiveSession || Item->Type == FConcertSessionItem::EType::SaveSession)
		{
			return MakeActiveSessionDetails(Item);
		}
		else if (Item->Type == FConcertSessionItem::EType::ArchivedSession)
		{
			return MakeArchivedSessionDetails(Item);
		}
	}

	return NoSessionSelectedPanel.ToSharedRef();
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeActiveSessionDetails(TSharedPtr<FConcertSessionItem> Item)
{
	const FConcertSessionInfo* SessionInfo = Controller->GetActiveSessionInfo(Item->ServerAdminEndpointId, Item->SessionId);
	if (!SessionInfo)
	{
		return NoSessionSelectedPanel.ToSharedRef();
	}

	TSharedPtr<SGridPanel> Grid;

	// State variables captured and shared by the different lambda functions below.
	TSharedPtr<bool> bDetailsAreaExpanded = MakeShared<bool>(false);
	TSharedPtr<bool> bClientsAreaExpanded = MakeShared<bool>(true);

	auto DetailsAreaSizeRule = [this, bDetailsAreaExpanded]()
	{
		return *bDetailsAreaExpanded ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent;
	};

	auto OnDetailsAreaExpansionChanged = [bDetailsAreaExpanded](bool bExpanded)
	{
		*bDetailsAreaExpanded = bExpanded;
	};

	auto ClientsAreaSizeRule = [this, bClientsAreaExpanded]()
	{
		return *bClientsAreaExpanded ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent;
	};

	auto OnClientsAreaExpansionChanged = [bClientsAreaExpanded](bool bExpanded)
	{
		*bClientsAreaExpanded = bExpanded;
	};

	TSharedRef<SSplitter> Widget = SNew(SSplitter)
		.Orientation(Orient_Vertical)

		// Details.
		+SSplitter::Slot()
		.SizeRule(TAttribute<SSplitter::ESizeRule>::Create(DetailsAreaSizeRule))
		.Value(0.6)
		[
			SAssignNew(DetailsArea, SExpandableArea)
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			.BorderImage_Lambda([this]() { return ConcertFrontendUtils::GetExpandableAreaBorderImage(*DetailsArea); })
			.BodyBorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.BodyBorderBackgroundColor(FLinearColor::White)
			.OnAreaExpansionChanged(FOnBooleanValueChanged::CreateLambda(OnDetailsAreaExpansionChanged))
			.InitiallyCollapsed(!(*bDetailsAreaExpanded))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("Details", "Details"), FText::FromString(Item->SessionName)))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
			]
			.BodyContent()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+SScrollBox::Slot()
				[
					SNew(SBox)
					.Padding(FMargin(0, 2, 0, 2))
					[
						SAssignNew(Grid, SGridPanel)
					]
				]
			]
		]

		// Clients
		+SSplitter::Slot()
		.SizeRule(TAttribute<SSplitter::ESizeRule>::Create(ClientsAreaSizeRule))
		.Value(0.4)
		[
			SAssignNew(ClientsArea, SExpandableArea)
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			.BorderImage_Lambda([this]() { return ConcertFrontendUtils::GetExpandableAreaBorderImage(*ClientsArea); })
			.BodyBorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.BodyBorderBackgroundColor(FLinearColor::White)
			.OnAreaExpansionChanged(FOnBooleanValueChanged::CreateLambda(OnClientsAreaExpansionChanged))
			.InitiallyCollapsed(!(*bClientsAreaExpanded))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Clients", "Clients"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
			]
			.BodyContent()
			[
				SNew(SOverlay)
				+SOverlay::Slot()
				[
					ClientsView.ToSharedRef()
				]
				+SOverlay::Slot()
				[
					NoClientPanel.ToSharedRef()
				]
			]
		];

	// Fill the grid.
	PopulateSessionInfoGrid(*Grid, *SessionInfo);

	// Populate the client list.
	RefreshClientList(Controller->GetClients(Item->ServerAdminEndpointId, Item->SessionId));

	return Widget;
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeArchivedSessionDetails(TSharedPtr<FConcertSessionItem> Item)
{
	const FConcertSessionInfo* SessionInfo = Controller->GetArchivedSessionInfo(Item->ServerAdminEndpointId, Item->SessionId);
	if (!SessionInfo)
	{
		return NoSessionSelectedPanel.ToSharedRef();
	}

	TSharedPtr<SGridPanel> Grid;

	TSharedRef<SExpandableArea> Widget = SAssignNew(DetailsArea, SExpandableArea)
	.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
	.BorderImage_Lambda([this]() { return ConcertFrontendUtils::GetExpandableAreaBorderImage(*DetailsArea); })
	.BodyBorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
	.BodyBorderBackgroundColor(FLinearColor::White)
	.InitiallyCollapsed(true)
	.HeaderContent()
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("Details", "Details"), FText::FromString(Item->SessionName)))
		.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
		.ShadowOffset(FVector2D(1.0f, 1.0f))
	]
	.BodyContent()
	[
		SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+SScrollBox::Slot()
		[
			SNew(SBox)
			.Padding(FMargin(0, 2, 0, 2))
			[
				SAssignNew(Grid, SGridPanel)
			]
		]
	];

	// Fill the grid.
	PopulateSessionInfoGrid(*Grid, *SessionInfo);

	return Widget;
}

void SConcertSessionBrowser::PopulateSessionInfoGrid(SGridPanel& Grid, const FConcertSessionInfo& SessionInfo)
{
	// Local function to populate the details grid.
	auto AddDetailRow = [](SGridPanel& Grid, int32 Row, const FText& Label, const FText& Value)
	{
		const float RowPadding = (Row == 0) ? 0.0f : 4.0f; // Space between line.
		const float ColPadding = 4.0f; // Space between columns. (Minimum)

		Grid.AddSlot(0, Row)
		.Padding(0.0f, RowPadding, ColPadding, 0.0f)
		[
			SNew(STextBlock).Text(Label)
		];

		Grid.AddSlot(1, Row)
		.Padding(0.0f, RowPadding, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(Value)
		];
	};

	int32 Row = 0;
	AddDetailRow(Grid, Row++, LOCTEXT("SessionId", "Session ID:"), FText::FromString(SessionInfo.SessionId.ToString()));
	AddDetailRow(Grid, Row++, LOCTEXT("SessionName", "Session Name:"), FText::FromString(SessionInfo.SessionName));
	AddDetailRow(Grid, Row++, LOCTEXT("Owner", "Owner:"), FText::FromString(SessionInfo.OwnerUserName));
	AddDetailRow(Grid, Row++, LOCTEXT("Project", "Project:"), FText::FromString(SessionInfo.Settings.ProjectName));
	//AddDetailRow(Grid, Row++, LOCTEXT("BaseVersion", "Base Version:"), FText::AsNumber(SessionInfo.Settings.BaseRevision, &FNumberFormattingOptions::DefaultNoGrouping()));
	if (SessionInfo.VersionInfos.Num() > 0)
	{
		const FConcertSessionVersionInfo& VersionInfo = SessionInfo.VersionInfos.Last();
		AddDetailRow(Grid, Row++, LOCTEXT("EngineVersion", "Engine Version:"),
			FText::Format(
				LOCTEXT("EngineVersionFmt", "{0}.{1}.{2}-{3}"),
				FText::AsNumber(VersionInfo.EngineVersion.Major, &FNumberFormattingOptions::DefaultNoGrouping()),
				FText::AsNumber(VersionInfo.EngineVersion.Minor, &FNumberFormattingOptions::DefaultNoGrouping()),
				FText::AsNumber(VersionInfo.EngineVersion.Patch, &FNumberFormattingOptions::DefaultNoGrouping()),
				FText::AsNumber(VersionInfo.EngineVersion.Changelist, &FNumberFormattingOptions::DefaultNoGrouping())
			));
	}
	AddDetailRow(Grid, Row++, LOCTEXT("ServerEndPointId", "Server Endpoint ID:"), FText::FromString(SessionInfo.ServerEndpointId.ToString()));
}

TSharedRef<ITableRow> SConcertSessionBrowser::OnGenerateClientRowWidget(TSharedPtr<FConcertSessionClientInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FConcertSessionClientInfo>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		.ToolTipText(Item->ToDisplayString())

		// The user "Avatar color" displayed as a small square colored by the user avatar color.
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
			.ColorAndOpacity(Item->ClientInfo.AvatarColor)
			.Text(FEditorFontGlyphs::Square)
		]

		// The user "Display Name".
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(FMargin(4.0, 2.0))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->ClientInfo.DisplayName))
		]
	];
}

void SConcertSessionBrowser::OnSessionSelectionChanged(TSharedPtr<FConcertSessionItem> SelectedSession, ESelectInfo::Type SelectInfo)
{
	// Cancel editing the row to create, archive or restore a session (if any), unless the row was selected in code.
	if (EditableSessionRow.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		check (SelectedSession != EditableSessionRow); // User should not be able to reselect an editable row as we remove it as soon as it is unselected.
		RemoveSessionRow(EditableSessionRow);
		check(!EditableSessionRow.IsValid() && !EditableSessionRowParent.IsValid()); // Expect to be cleared by RemoveSessionRow().
	}

	// Clear the list of clients (if any)
	Clients.Reset();

	// Update the details panel.
	SessionDetailsView->SetContent(MakeSessionDetails(SelectedSession));
}

bool SConcertSessionBrowser::IsNewButtonEnabled() const
{
	return Controller->GetServers().Num() > 0;
}

bool SConcertSessionBrowser::IsJoinButtonEnabled() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession;
}

bool SConcertSessionBrowser::IsRestoreButtonEnabled() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0]->Type == FConcertSessionItem::EType::ArchivedSession;
}

bool SConcertSessionBrowser::IsArchiveButtonEnabled() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession;
}

bool SConcertSessionBrowser::IsRenameButtonEnabled() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() != 1)
	{
		return false;
	}

	return (SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession && Controller->CanRenameActiveSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId)) ||
	       (SelectedItems[0]->Type == FConcertSessionItem::EType::ArchivedSession && Controller->CanRenameArchivedSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId));
}

bool SConcertSessionBrowser::IsDeleteButtonEnabled() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		return false;
	}

	return (SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession && Controller->CanDeleteActiveSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId)) ||
	       (SelectedItems[0]->Type == FConcertSessionItem::EType::ArchivedSession && Controller->CanDeleteArchivedSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId));
}

bool SConcertSessionBrowser::IsLaunchServerButtonEnabled() const
{
	return !bLocalServerRunning;
}

bool SConcertSessionBrowser::IsAutoJoinButtonEnabled() const
{
	return Controller->GetConcertClient()->CanAutoConnect() && !Controller->GetConcertClient()->IsAutoConnecting();
}

bool SConcertSessionBrowser::IsCancelAutoJoinButtonEnabled() const
{
	return Controller->GetConcertClient()->IsAutoConnecting();
}

FReply SConcertSessionBrowser::OnNewButtonClicked()
{
	InsertNewSessionEditableRow();
	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnJoinButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		RequestJoinSession(SelectedItems[0]);
	}
	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnRestoreButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		InsertRestoreSessionAsEditableRow(SelectedItems[0]);
	}

	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnArchiveButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		InsertArchiveSessionAsEditableRow(SelectedItems[0]);
	}

	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnDeleteButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	for (const TSharedPtr<FConcertSessionItem>& Item : SelectedItems)
	{
		RequestDeleteSession(Item);
	}

	return FReply::Handled();
}

void SConcertSessionBrowser::OnBeginEditingSessionName(TSharedPtr<FConcertSessionItem> Item)
{
	Item->OnBeginEditSessionNameRequest.Broadcast(); // Signal the row widget to enter in edit mode.
}

FReply SConcertSessionBrowser::OnLaunchServerButtonClicked()
{
	IMultiUserClientModule::Get().LaunchConcertServer();
	bLocalServerRunning = IMultiUserClientModule::Get().IsConcertServerRunning(); // Immediately update the cache state to avoid showing buttons enabled for a split second.
	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnShutdownServerButtonClicked()
{
	if (bLocalServerRunning)
	{
		IMultiUserClientModule::Get().ShutdownConcertServer();
	}
	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnAutoJoinButtonClicked()
{
	IConcertClientPtr ConcertClient = Controller->GetConcertClient();

	// Start the 'auto connect' routine. It will try until it succeeded or gets canceled. Creating or Joining a session automatically cancels it.
	ConcertClient->StartAutoConnect();
	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnCancelAutoJoinButtonClicked()
{
	Controller->GetConcertClient()->StopAutoConnect();
	return FReply::Handled();
}

void SConcertSessionBrowser::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// If the model received updates.
	if (Controller->GetAndClearDiscoveryUpdateFlag())
	{
		// Don't wait next TickDiscovery() running at lower frequency, update immediately.
		UpdateDiscovery();
	}

	// Ensure the 'default server' filter is updated when the configuration of the default server changes.
	if (DefaultServerURL != Controller->GetConcertClient()->GetConfiguration()->DefaultServerURL)
	{
		DefaultServerURL = Controller->GetConcertClient()->GetConfiguration()->DefaultServerURL;
		bRefreshSessionFilter = true;
	}

	// Should refresh the session filter?
	if (bRefreshSessionFilter)
	{
		RefreshSessionList();
		bRefreshSessionFilter = false;
	}
}

FReply SConcertSessionBrowser::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// NOTE: When an 'editable row' text box has the focus the keys are grabbed by the text box but
	//       if the editable row is still selected, but the text field doesn't have the focus anymore
	//       the keys will end up here if the browser has the focus.

	if (InKeyEvent.GetKey() == EKeys::Delete && !EditableSessionRow.IsValid()) // Delete selected row(s) unless the selected row is an 'editable' one.
	{
		for (TSharedPtr<FConcertSessionItem>& Item : SessionsView->GetSelectedItems())
		{
			RequestDeleteSession(Item);
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Escape && EditableSessionRow.IsValid()) // Cancel 'new session', 'archive session' or 'restore session' action.
	{
		RemoveSessionRow(EditableSessionRow);
		check(!EditableSessionRow.IsValid() && !EditableSessionRowParent.IsValid()); // Expect to be cleared by RemoveSessionRow().
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::F2 && !EditableSessionRow.IsValid())
	{
		TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			SelectedItems[0]->OnBeginEditSessionNameRequest.Broadcast(); // Broadcast the request.
		}
	}

	return FReply::Unhandled();
}

void SConcertSessionBrowser::RequestCreateSession(const TSharedPtr<FConcertSessionItem>& NewItem)
{
	Controller->CreateSession(NewItem->ServerAdminEndpointId, NewItem->SessionName);
	RemoveSessionRow(NewItem); // The row used to edit the session name and pick the server.
}

void SConcertSessionBrowser::RequestJoinSession(const TSharedPtr<FConcertSessionItem>& LiveItem)
{
	Controller->JoinSession(LiveItem->ServerAdminEndpointId, LiveItem->SessionId);
}

void SConcertSessionBrowser::RequestArchiveSession(const TSharedPtr<FConcertSessionItem>& SaveItem, const FString& ArchiveName)
{
	Controller->ArchiveSession(SaveItem->ServerAdminEndpointId, SaveItem->SessionId, ArchiveName, FConcertSessionFilter());
	RemoveSessionRow(SaveItem); // The row used to edit the archive name.
}

void SConcertSessionBrowser::RequestRestoreSession(const TSharedPtr<FConcertSessionItem>& RestoreItem, const FString& SessionName)
{
	Controller->RestoreSession(RestoreItem->ServerAdminEndpointId, RestoreItem->SessionId, SessionName, FConcertSessionFilter());
	RemoveSessionRow(RestoreItem); // The row used to edit the restore as name.
}

void SConcertSessionBrowser::RequestRenameSession(const TSharedPtr<FConcertSessionItem>& RenamedItem, const FString& NewName)
{
	if (RenamedItem->Type == FConcertSessionItem::EType::ActiveSession)
	{
		Controller->RenameActiveSession(RenamedItem->ServerAdminEndpointId, RenamedItem->SessionId, NewName);
	}
	else if (RenamedItem->Type == FConcertSessionItem::EType::ArchivedSession)
	{
		Controller->RenameArchivedSession(RenamedItem->ServerAdminEndpointId, RenamedItem->SessionId, NewName);
	}

	// Display the new name until the server response is received. If the server refuses the new name, the discovery will reset the
	// name (like if another client renamed it back) and the user will get a toast saying the rename failed.
	RenamedItem->SessionName = NewName;
}

void SConcertSessionBrowser::RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& DeletedItem)
{
	const FText SessionNameInText = FText::FromString(DeletedItem->SessionName);
	const FText SeverNameInText = FText::FromString(DeletedItem->ServerName);
	const FText ConfirmationMessage = FText::Format(LOCTEXT("DeleteSessionConfirmationMessage", "Do you really want to delete the session \"{0}\" from the server \"{1}\"?"), SessionNameInText, SeverNameInText);
	const FText ConfirmationTitle = LOCTEXT("DeleteSessionConfirmationTitle", "Delete Session Confirmation");

	if (FMessageDialog::Open(EAppMsgType::YesNo, ConfirmationMessage, &ConfirmationTitle) == EAppReturnType::Yes) // Confirmed?
	{
		if (DeletedItem->Type == FConcertSessionItem::EType::ActiveSession)
		{
			Controller->DeleteActiveSession(DeletedItem->ServerAdminEndpointId, DeletedItem->SessionId);
		}
		else if (DeletedItem->Type == FConcertSessionItem::EType::ArchivedSession)
		{
			Controller->DeleteArchivedSession(DeletedItem->ServerAdminEndpointId, DeletedItem->SessionId);
		}

		UpdateDiscovery(); // Don't wait up to 1s, kick discovery right now.
	}
}


void SConcertBrowser::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow, TWeakPtr<IConcertSyncClient> InSyncClient)
{
	if (!MultiUserClientUtils::HasServerCompatibleCommunicationPluginEnabled())
	{
		// Output a log.
		MultiUserClientUtils::LogNoCompatibleCommunicationPluginEnabled();

		// Show a message in the browser.
		ChildSlot.AttachWidget(SNew(SConcertNoAvailability)
			.Text(MultiUserClientUtils::GetNoCompatibleCommunicationPluginEnabledText()));

		return; // Installing a plug-in implies an editor restart, don't bother initializing the rest.
	}

	WeakConcertSyncClient = InSyncClient;
	if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = WeakConcertSyncClient.Pin())
	{
		SearchedText = MakeShared<FText>(); // Will keep in memory the session browser search text between join/leave UI transitions.

		IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();
		check(ConcertClient->IsConfigured());
		
		ConcertClient->OnSessionConnectionChanged().AddSP(this, &SConcertBrowser::HandleSessionConnectionChanged);

		// Attach the panel corresponding the current state.
		AttachChildWidget(ConcertClient->GetSessionConnectionStatus());
	}
}

void SConcertBrowser::HandleSessionConnectionChanged(IConcertClientSession& InSession, EConcertConnectionStatus ConnectionStatus)
{
	AttachChildWidget(ConnectionStatus);
}

void SConcertBrowser::AttachChildWidget(EConcertConnectionStatus ConnectionStatus)
{
	if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = WeakConcertSyncClient.Pin())
	{
		if (ConnectionStatus == EConcertConnectionStatus::Connected)
		{
			ChildSlot.AttachWidget(SNew(SActiveSession, ConcertSyncClient));
		}
		else if (ConnectionStatus == EConcertConnectionStatus::Disconnected)
		{
			ChildSlot.AttachWidget(SNew(SConcertSessionBrowser, ConcertSyncClient->GetConcertClient(), SearchedText));
		}
	}
}

#undef LOCTEXT_NAMESPACE
