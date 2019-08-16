// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "UObject/StructOnScope.h"
#include "ConcertWorkspaceMessages.h"
#include "ConcertSyncSessionTypes.h"

class ISourceControlProvider;
class IConcertClientSession;
class IConcertClientDataStore;

DECLARE_MULTICAST_DELEGATE(FOnWorkspaceSynchronized);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnActivityAddedOrUpdated, const FConcertClientInfo&/*InClientInfo*/, const FConcertSyncActivity&/*InActivity*/, const FStructOnScope&/*InActivitySummary*/);

struct FConcertClientSessionActivity
{
	FConcertClientSessionActivity() = default;

	FConcertClientSessionActivity(const FConcertSyncActivity& InActivity, const FStructOnScope& InActivitySummary)
		: Activity(InActivity)
	{
		ActivitySummary.InitializeFromChecked(InActivitySummary);
	}

	FConcertClientSessionActivity(FConcertSyncActivity&& InActivity, FStructOnScope&& InActivitySummary)
		: Activity(MoveTemp(InActivity))
	{
		ActivitySummary.InitializeFromChecked(MoveTemp(InActivitySummary));
	}

	FConcertSyncActivity Activity;
	TStructOnScope<FConcertSyncActivitySummary> ActivitySummary;
};

class IConcertClientWorkspace
{
public:
	/**
	 * Get the associated session.
	 */
	virtual IConcertClientSession& GetSession() const = 0;

	/**
	 * @return the client id this workspace uses to lock resources.
	 */
	virtual FGuid GetWorkspaceLockId() const = 0;

	/**
	 * @return a valid client id of the owner of this resource lock or an invalid id if unlocked
	 */
	virtual FGuid GetResourceLockId(const FName InResourceName) const = 0;

	/**
	 * Verify if resources are locked by a particular client
	 * @param ResourceNames list of resources path to verify
	 * @param ClientId the client id to verify
	 * @return true if all resources in ResourceNames are locked by ClientId
	 * @note passing an invalid client id will return true if all resources are unlocked
	 */
	virtual bool AreResourcesLockedBy(TArrayView<const FName> ResourceNames, const FGuid& ClientId) = 0;

	/**
	 * Attempt to lock the given resource.
	 * @note Passing force will always assign the lock to the given endpoint, even if currently locked by another.
	 * @return True if the resource was locked (or already locked by the given endpoint), false otherwise.
	 */
	virtual TFuture<FConcertResourceLockResponse> LockResources(TArray<FName> InResourceName) = 0;

	/**
	 * Attempt to unlock the given resource.
	 * @note Passing force will always clear, even if currently locked by another endpoint.
	 * @return True if the resource was unlocked, false otherwise.
	 */
	virtual TFuture<FConcertResourceLockResponse> UnlockResources(TArray<FName> InResourceName) = 0;

	/**
	 * Tell if a workspace contains session changes.
	 * @return True if the session contains any changes.
	 */
	virtual bool HasSessionChanges() const = 0;

	/**
	 * Gather assets changes that happened on the workspace in this session.
	 * @return a list of asset files that were modified during the session.
	 */
	virtual TArray<FString> GatherSessionChanges() = 0;

	/** Persist the session changes from the file list and prepare it for source control submission */
	virtual bool PersistSessionChanges(TArrayView<const FString> InFilesToPersist, ISourceControlProvider* SourceControlProvider, TArray<FText>* OutFailureReasonMap = nullptr) = 0;

	/**
	 * Get Activities from the session.
	 * @param FirstActivityIdToFetch The ID at which to start fetching activities.
	 * @param MaxNumActivities The maximum number of activities to fetch.
	 * @param OutEndpointClientInfoMap The client info for the activities fetched.
	 * @param OutActivities the activities fetched.
	 */
	virtual void GetActivities(const int64 FirstActivityIdToFetch, const int64 MaxNumActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, TArray<FConcertClientSessionActivity>& OutActivities) const = 0;

	/**
	 * Get the ID of the last activity in the session.
	 */
	virtual int64 GetLastActivityId() const = 0;

	/**
	 * @return the delegate called every time an activity is added to or updated in the session.
	 */
	virtual FOnActivityAddedOrUpdated& OnActivityAddedOrUpdated() = 0;

	/**
	 * Indicate if an asset package is supported for live transactions.
	 *
	 * @param InAssetPackage The package to check
	 * @return true if we support live transactions for objects inside the package
	 */
	virtual bool HasLiveTransactionSupport(class UPackage* InPackage) const = 0;

	/**
	 * Indicate if package dirty event should be ignored for a package
	 * @param InPackage The package to check
	 * @return true if dirty event should be ignored for said package.
	 */
	virtual bool ShouldIgnorePackageDirtyEvent(class UPackage* InPackage) const = 0;

	/**
	 * @param[in] TransactionEventId ID of the transaction to look for.
	 * @param[out] OutTransactionEvent The transaction corresponding to TransactionEventId if found.
	 * @return whether or not the transaction event was found.
	 */
	virtual bool FindTransactionEvent(const int64 TransactionEventId, FConcertSyncTransactionEvent& OutTransactionEvent, const bool bMetaDataOnly) const = 0;

	/**
	 * @param[in] PackageEventId ID of the package to look for.
	 * @param[out] OutPackageEvent Information about the package.
	 * @return whether or not the package event was found.
	 */
	virtual bool FindPackageEvent(const int64 PackageEventId, FConcertSyncPackageEvent& OutPackageEvent, const bool bMetaDataOnly) const = 0;

	/**
	 * @return the delegate called every time the workspace is synced.
	 */
	virtual FOnWorkspaceSynchronized& OnWorkspaceSynchronized() = 0;
	
	/**
	 * @return the key/value store shared by all clients.
	 */
	virtual IConcertClientDataStore& GetDataStore() = 0;

	/**
	 * Returns true if the specified asset has unsaved modifications from any other client than the one corresponding
	 * to this workspace client and possibly returns more information about those other clients.
	 * @param[in] AssetName The asset name.
	 * @param[out] OutOtherClientsWithModifNum If not null, will contain how many other client(s) have modified the specified package.
	 * @param[out] OutOtherClientsWithModifInfo If not null, will contain the other client(s) who modified the packages, up to OtherClientsWithModifMaxFetchNum.
	 * @param[in] OtherClientsWithModifMaxFetchNum The maximum number of client info to store in OutOtherClientsWithModifInfo if the latter is not null.
	 */
	virtual bool IsAssetModifiedByOtherClients(const FName& AssetName, int32* OutOtherClientsWithModifNum = nullptr, TArray<FConcertClientInfo>* OutOtherClientsWithModifInfo = nullptr, int32 OtherClientsWithModifMaxFetchNum = 0) const = 0;
};
