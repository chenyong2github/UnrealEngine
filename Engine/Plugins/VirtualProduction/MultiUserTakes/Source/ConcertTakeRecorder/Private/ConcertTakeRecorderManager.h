// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConcertTakeRecorderMessages.h"
#include "ConcertMessages.h"
#include "Delegates/IDelegateInstance.h"

#include "ConcertTakeRecorderClientSessionCustomization.h"

DECLARE_LOG_CATEGORY_EXTERN(LogConcertTakeRecorder, Log, All);

class IConcertSyncClient;
class IConcertClientSession;
struct FConcertSessionContext;
class UTakeRecorder;
enum class ECheckBoxState : uint8;
struct EVisibility;

/**
 * Take Recorder manager that is held by the client sync module that keeps track of when a take is started, stopped or cancelled.
 * Events are registered to client sessions that will then operate on the Take Recorder UIs
 */
class FConcertTakeRecorderManager
{
public:
	/**
	 * Constructor - registers TakeRecorderInitialized handler with the take recorder module
	 */
	FConcertTakeRecorderManager();

	/**
	 * Destructor - unregisters TakeRecorderInitialized handler from the take recorder module
	 */
	~FConcertTakeRecorderManager();

	/**
	 * Register all custom take recorder events for the specified client session
	 *
	 * @param InSession	The client session to register custom events with
	 */
	void Register(TSharedRef<IConcertClientSession> InSession);

	/**
	 * Unregister previously registered custom take recorder events from the specified client session
	 *
	 * @param InSession The client session to unregister custom events from
	 */
	void Unregister(TSharedRef<IConcertClientSession> InSession);

private:
	//~ Take recorder delegate handlers
	void OnTakeRecorderInitialized(UTakeRecorder* TakeRecorder);
	void OnRecordingFinished(UTakeRecorder* TakeRecorder);
	void OnRecordingCancelled(UTakeRecorder* TakeRecorder);

	//~ Concert event handlers
	void OnTakeInitializedEvent(const FConcertSessionContext&, const FConcertTakeInitializedEvent& InEvent);
	void OnRecordingFinishedEvent(const FConcertSessionContext&, const FConcertRecordingFinishedEvent&);
	void OnRecordingCancelledEvent(const FConcertSessionContext&, const FConcertRecordingCancelledEvent&);

	void OnRecordSettingsChangeEvent(const FConcertSessionContext&, const FConcertRecordSettingsChangeEvent&);
	void OnMultiUserSyncChangeEvent(const FConcertSessionContext&, const FConcertMultiUserSyncChangeEvent&);

	void OnSessionClientChanged(IConcertClientSession&, EConcertClientStatus ClientStatus, const FConcertSessionClientInfo& InClientInfo);
	void OnSessionConnectionChanged(IConcertClientSession&, EConcertConnectionStatus ConnectionStatus);

	//~ Widget extension handlers
	void RegisterExtensions();
	void UnregisterExtensions();

	void CreateExtensionWidget(TArray<TSharedRef<class SWidget>>& OutExtensions);
	void CreateRecordButtonOverlay(TArray<TSharedRef<SWidget>>& OutExtensions);

	EVisibility GetMultiUserIconVisibility() const;

	bool IsTakeSyncEnabled() const;
	ECheckBoxState IsTakeSyncChecked() const;
	void HandleTakeSyncCheckBox(ECheckBoxState State) const;
	EVisibility HandleTakeSyncButtonVisibility() const;
	FText GetWarningText() const;
	EVisibility HandleTakeSyncWarningVisibility() const;

	void OnObjectModified(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
	void CacheTakeSyncFolderFiltered();

	void UpdateSessionClientList();
	void DisconnectFromSession();
	void ConnectToSession(IConcertClientSession&);

private:
	void ReportRecordingError(FText &);
	bool CanRecord() const;
	bool CanAnyRecord() const;

	void SendInitialState(IConcertClientSession&);
	void OnTakeSyncPropertyChange(bool Value);

	void RecordSettingChange(const FConcertClientRecordSetting& RecordSetting);
	void AddRemoteClient(const FConcertSessionClientInfo& ClientInfo);

	/** Weak pointer to the client session with which to send events. May be null or stale. */
	TWeakPtr<IConcertClientSession> WeakSession;

	/**
	 * Whether the take sync folder is filtered from multi user transactions.
	 * If not filtered, take sync cannot be activated since multi-user would try to synchronize
	 * the takes generated from every clients' Take Recorder.
	 */
	bool bTakeSyncFolderFiltered = false;

	/**
	 * Used to prevent sending out events that were just received by this client.
	 */
	struct FTakeRecorderState
	{
		FString LastStartedTake;
		FString LastStoppedTake;
	} TakeRecorderState;

	TSharedPtr<FConcertTakeRecorderClientSessionCustomization> Customization;

	/** Delegate for any changes in client state. */
	FDelegateHandle				 ClientChangeDelegate;

	/** Delegate for any changes take sync status. */
	FDelegateHandle				 TakeSyncDelegate;

	/**
	 * Denotes if we are currently in recording mode.
	 */
	bool bIsRecording = false;
};
