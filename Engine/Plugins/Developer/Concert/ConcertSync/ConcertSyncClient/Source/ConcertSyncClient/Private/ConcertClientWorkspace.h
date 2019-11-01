// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ITransaction.h"
#include "IConcertClientWorkspace.h"
#include "IConcertSessionHandler.h"
#include "ConcertSyncSessionFlags.h"
#include "ConcertWorkspaceMessages.h"
#include "ConcertClientWorkspaceData.h"

class IConcertClientSession;
class IConcertClientPackageBridge;
class FConcertClientPackageManager;
class IConcertClientTransactionBridge;
class FConcertClientTransactionManager;
class FConcertClientLockManager;
class FConcertClientLiveTransactionAuthors;
class FConcertSyncClientLiveSession;
class ISourceControlProvider;
class FConcertClientDataStore;

struct FScopedSlowTask;

class FConcertClientWorkspace : public IConcertClientWorkspace
{
public:
	FConcertClientWorkspace(TSharedRef<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge, IConcertClientTransactionBridge* InTransactionBridge);
	virtual ~FConcertClientWorkspace();

	// IConcertClientWorkspace interface
	virtual IConcertClientSession& GetSession() const override;
	virtual FGuid GetWorkspaceLockId() const override;
	virtual FGuid GetResourceLockId(const FName InResourceName) const override;
	virtual bool AreResourcesLockedBy(TArrayView<const FName> ResourceNames, const FGuid& ClientId) override;
	virtual TFuture<FConcertResourceLockResponse> LockResources(TArray<FName> InResourceNames) override;
	virtual TFuture<FConcertResourceLockResponse> UnlockResources(TArray<FName> InResourceNames) override;
	virtual bool HasSessionChanges() const override;
	virtual TArray<FName> GatherSessionChanges(bool IgnorePersisted = true) override;
	virtual bool PersistSessionChanges(TArrayView<const FName> InPackagesToPersist, ISourceControlProvider* SourceControlProvider, TArray<FText>* OutFailureReasons = nullptr) override;
	virtual bool HasLiveTransactionSupport(UPackage* InPackage) const override;
	virtual bool ShouldIgnorePackageDirtyEvent(class UPackage* InPackage) const override;
	virtual bool FindTransactionEvent(const int64 TransactionEventId, FConcertSyncTransactionEvent& OutTransactionEvent, const bool bMetaDataOnly) const override;
	virtual TFuture<TOptional<FConcertSyncTransactionEvent>> FindOrRequestTransactionEvent(const int64 TransactionEventId, const bool bMetaDataOnly) override;
	virtual bool FindPackageEvent(const int64 PackageEventId, FConcertSyncPackageEvent& OutPackageEvent, const bool bMetaDataOnly) const override;
	virtual TFuture<TOptional<FConcertSyncPackageEvent>> FindOrRequestPackageEvent(const int64 PackageEventId, const bool bMetaDataOnly) override;
	virtual void GetActivities(const int64 FirstActivityIdToFetch, const int64 MaxNumActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, TArray<FConcertClientSessionActivity>& OutActivities) const override;
	virtual int64 GetLastActivityId() const override;
	virtual FOnActivityAddedOrUpdated& OnActivityAddedOrUpdated() override;
	virtual FOnWorkspaceSynchronized& OnWorkspaceSynchronized() override;
	virtual IConcertClientDataStore& GetDataStore() override;
	virtual bool IsAssetModifiedByOtherClients(const FName& AssetName, int32* OutOtherClientsWithModifNum, TArray<FConcertClientInfo>* OutOtherClientsWithModifInfo, int32 OtherClientsWithModifMaxFetchNum) const override;
	virtual void SetIgnoreOnRestoreFlagForEmittedActivities(bool bIgnore) override;

private:
	/** Bind the workspace to this session. */
	void BindSession(TSharedPtr<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge, IConcertClientTransactionBridge* InTransactionBridge);

	/** Unbind the workspace to its bound session. */
	void UnbindSession();

	/**
	 * Load client side info associated with this session if any. (i.e already persisted files)
	 */
	void LoadSessionData();

	/**
	 * Save client side info associated with this session if any. (i.e already persisted files)
	 */
	void SaveSessionData();

	/** */
	void HandleConnectionChanged(IConcertClientSession& InSession, EConcertConnectionStatus Status);

#if WITH_EDITOR
	/**
	 * Save all live transactions to packages.
	 */
	void SaveLiveTransactionsToPackages();

	/**
	 * Save live transactions, if any, to the specified packages, adding a client side dummy package event to the db
	 */
	void SaveLiveTransactionsToPackage(const FName PackageName);

	/** */
	void HandleAssetLoaded(UObject* InAsset);

	/** */
	void HandlePackageDiscarded(UPackage* InPackage);

	/** */
	void HandlePostPIEStarted(const bool InIsSimulating);

	/** */
	void HandleSwitchBeginPIEAndSIE(const bool InIsSimulating);

	/** */
	void HandleEndPIE(const bool InIsSimulating);
#endif	// WITH_EDITOR

	/** */
	void OnEndFrame();

	/** */
	void HandleWorkspaceSyncEndpointEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncEndpointEvent& Event);

	/** */
	void HandleWorkspaceSyncActivityEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncActivityEvent& Event);

	/** */
	void HandleWorkspaceSyncLockEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncLockEvent& Event);

	/** */
	void HandleWorkspaceSyncCompletedEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncCompletedEvent& Event);

	/**
	 * Set an endpoint in the session database, creating or replacing it.
	 *
	 * @param InEndpointId				The ID of the endpoint to set.
	 * @param InEndpointData			The endpoint data to set.
	 */
	void SetEndpoint(const FGuid& InEndpointId, const FConcertSyncEndpointData& InEndpointData);

	/**
	 * Set a connection activity in the session database, creating or replacing it.
	 * @note The endpoint ID referenced by the activity must exist in the database (@see SetEndpoint).
	 *
	 * @param InConnectionActivity		The connection activity to set.
	 */
	void SetConnectionActivity(const FConcertSyncConnectionActivity& InConnectionActivity);

	/**
	 * Set a lock activity in the session database, creating or replacing it.
	 * @note The endpoint ID referenced by the activity must exist in the database (@see SetEndpoint).
	 *
	 * @param InLockActivity			The lock activity to set.
	 */
	void SetLockActivity(const FConcertSyncLockActivity& InLockActivity);

	/**
	 * Set a transaction activity in the session database, creating or replacing it.
	 * @note The endpoint ID referenced by the activity must exist in the database (@see SetEndpoint).
	 *
	 * @param InTransactionActivity		The transaction activity to set.
	 */
	void SetTransactionActivity(const FConcertSyncTransactionActivity& InTransactionActivity);

	/**
	 * Set a package activity in the session database, creating or replacing it.
	 * @note The endpoint ID referenced by the activity must exist in the database (@see SetEndpoint).
	 *
	 * @param InPackageActivity		The package activity to set.
	 */
	void SetPackageActivity(const FConcertSyncPackageActivity& InPackageActivity);

	/**
	 * Called after any updated in the session database.
	 *
	 * @param InActivity			The activity that was updated.
	 */
	void PostActivityUpdated(const FConcertSyncActivity& InActivity);

	/**
	 * Check whether a package activity is partially synced, i.e. that only the meta data
	 * was synced because the activity event was superseded by another one and the package data
	 * wasn't required to reconstruct the state of a level.
	 */
	bool IsPackageEventPartiallySynced(const FConcertSyncPackageEvent& PackageEvent) const;

	/**
	 * Check whether a transaction activity is partially synced, i.e. that only the meta data
	 * was synced because the activity event was superseded by another one and the transaction data
	 * wasn't required to reconstruct the state of a level.
	 */
	bool IsTransactionEventPartiallySynced(const FConcertSyncTransactionEvent& TransactionEvent) const;

	/** */
	TUniquePtr<FConcertClientTransactionManager> TransactionManager;

	/** */
	TUniquePtr<FConcertClientPackageManager> PackageManager;

	/** */
	TUniquePtr<FConcertClientLockManager> LockManager;

	/**
	 * Tracks the clients that have live transactions on any given packages.
	 */
	TUniquePtr<FConcertClientLiveTransactionAuthors> LiveTransactionAuthors;

	/** */
	IConcertClientPackageBridge* PackageBridge;

	/** */
	TSharedPtr<FConcertSyncClientLiveSession> LiveSession;

	/** Persistent client workspace data associated with this workspaces session. */
	FConcertClientWorkspaceData SessionData;

	/** True if this client has performed its initial sync with the server session */
	bool bHasSyncedWorkspace;

	/** True if a request to finalize a workspace sync has been requested */
	bool bFinalizeWorkspaceSyncRequested;

	/** Slow task used during the initial sync of this workspace */
	TUniquePtr<FScopedSlowTask> InitialSyncSlowTask;

	/** The delegate called every time activity is added to or updated in this session. */
	FOnActivityAddedOrUpdated OnActivityAddedOrUpdatedDelegate;

	/** The delegate called every time the workspace is synced. */
	FOnWorkspaceSynchronized OnWorkspaceSyncedDelegate;
	
	/** The session key/value store proxy. The real store is held by the server and shared across all clients. */
	TUniquePtr<FConcertClientDataStore> DataStore;

	/** True if the client has marked the further transaction as 'non-ignored'. This is sent at the end of the frame. */
	bool bPendingStopIgnoringActivityOnRestore = false;
};
