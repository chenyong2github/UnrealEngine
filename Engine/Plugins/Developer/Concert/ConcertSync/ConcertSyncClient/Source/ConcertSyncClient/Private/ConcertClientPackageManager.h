// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertSessionHandler.h"
#include "IConcertClientPackageBridge.h"

class FConcertSyncClientLiveSession;
class FConcertSandboxPlatformFile;
class ISourceControlProvider;
class UPackage;

struct FAssetData;
struct FConcertPackage;
struct FConcertPackageInfo;
struct FConcertPackageRejectedEvent;

enum class EConcertPackageUpdateType : uint8;

class FConcertClientPackageManager
{
public:
	FConcertClientPackageManager(TSharedRef<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge);
	~FConcertClientPackageManager();
	
	/**
	 * @return true if dirty even should be ignored for InPackage
	 */
	bool ShouldIgnorePackageDirtyEvent(class UPackage* InPackage) const;

	/** 
	 * @return the map of persisted files to their current package ledger version.
	 */
	TMap<FString, int64> GetPersistedFiles() const;

	/**
	 * Synchronize files that should be considered as already persisted from session.
	 * @param PersistedFiles Map of persisted files to their package version to mark as persisted if their ledger version match.
	 */
	void SynchronizePersistedFiles(const TMap<FString, int64>& PersistedFiles);

	/**
	 * Synchronize any pending updates to in-memory packages (hot-reloads or purges) to keep them up-to-date with the on-disk state.
	 */
	void SynchronizeInMemoryPackages();

	/**
	 * Called to handle a local package having its changes discarded.
	 */
	void HandlePackageDiscarded(UPackage* InPackage);

	/**
	 * Called to handle a remote package being received.
	 */
	void HandleRemotePackage(const FGuid& InSourceEndpointId, const int64 InPackageEventId, const bool bApply);

	/**
	 * Called to apply the head revision data for all packages.
	 */
	void ApplyAllHeadPackageData();

	/**
	 * Tell if package changes happened during this session.
	 * @return True if the session contains package changes.
	 */
	bool HasSessionChanges() const;

	/**
	 * Persist the session changes from the package name list and prepare it for source control submission.
	 */
	bool PersistSessionChanges(TArrayView<const FName> InPackagesToPersist, ISourceControlProvider* SourceControlProvider, TArray<FText>* OutFailureReasons = nullptr);

private:
	/**
	 * Apply the data in the given package to disk and update the in-memory state.
	 */
	void ApplyPackageUpdate(const FConcertPackage& InPackage);

	/**
	 * Handle a rejected package event, those are sent by the server when a package update is refused.
	 */
	void HandlePackageRejectedEvent(const FConcertSessionContext& InEventContext, const FConcertPackageRejectedEvent& InEvent);

	/**
	 * Called when the dirty state of a package changed.
	 * Used to track currently dirty packages for hot-reload when discarding the manager.
	 */
	void HandlePackageDirtyStateChanged(UPackage* InPackage);

	/**
	 * Called to handle a local package event.
	 */
	void HandleLocalPackageEvent(const FConcertPackage& Package);

	/**
	 * Utility to save new package data to disk, and also queue if for hot-reload.
	 */
	void SavePackageFile(const FConcertPackage& Package);

	/**
	 * Utility to remove existing package data from disk, and also queue if for purging.
	 */
	void DeletePackageFile(const FConcertPackage& Package);

	/**
	 * Can we currently perform content hot-reloads or purges?
	 * True if we are neither suspended nor unable to perform a blocking action, false otherwise.
	 */
	bool CanHotReloadOrPurge() const;

	/**
	 * Hot-reload any pending in-memory packages to keep them up-to-date with the on-disk state.
	 */
	void HotReloadPendingPackages();

	/**
	 * Purge any pending in-memory packages to keep them up-to-date with the on-disk state.
	 */
	void PurgePendingPackages();

#if WITH_EDITOR
	/**
	 * Sandbox for storing package changes to disk within a Concert session.
	 */
	TUniquePtr<FConcertSandboxPlatformFile> SandboxPlatformFile; // TODO: Will need to ensure the sandbox also works on cooked clients
#endif

	/**
	 * Session instance this package manager was created for.
	 */
	TSharedPtr<FConcertSyncClientLiveSession> LiveSession;

	/**
	 * Package bridge used by this manager.
	 */
	IConcertClientPackageBridge* PackageBridge;

	/**
	 * Flag to indicate package dirty event should be ignored.
	 */
	bool bIgnorePackageDirtyEvent;

	/**
	 * Set of package names that are currently dirty.
	 * Only used to properly track packages that need hot-reloading when discarding the manager but
	 * currently escape the sandbox and live transaction tracking.
	 */
	TSet<FName> DirtyPackages;

	/**
	 * Array of package names that are pending a content hot-reload.
	 */
	TArray<FName> PackagesPendingHotReload;

	/**
	 * Array of package names that are pending an in-memory purge.
	 */
	TArray<FName> PackagesPendingPurge;
};
