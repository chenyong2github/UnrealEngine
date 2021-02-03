// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDataGatherer.h"

#include <atomic>
#include "Containers/Set.h"
#include "Misc/StringBuilder.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

namespace UE
{
namespace AssetDataGather
{
namespace Private
{

class FMountDir;

/** Enum to specify files and directories that should be completed earlier than others */
enum class EPriority : uint8
{
	/** Game thread is blocked until the file/directory is completed */
	Blocking,
	/** Optional information (e.g. use of the ReferenceViewer) is unavailable until the file/directory is completed */
	High,
	/** Nothing has requested the file/directory yet */
	Normal
};
constexpr uint32 CountEPriority = static_cast<uint32>(EPriority::Normal) + 1;

/** Fields being set in a call to SetProperty */
struct FSetPathProperties
{
	/** The path (usually a plugin's root content path) has been requested for scanning through e.g. ScanPathsSynchronous */
	TOptional<bool> IsWhitelisted;
	/** The directory's list of direct file/subdirectory children has been scanned through a call to IFileManager::IterateDirectoryStat after process start or the last request to rescan it */
	TOptional<bool> HasScanned;
	/**
	 * Paths under the directory should be used (if whitelisted) even if they match blacklist entries. Used only on the MountDir.
	 * Used e.g. when a level is opened in the editor and we want to add to the asset registries all of its one-file-per-actor files that are normally blacklisted.
	 */
	TOptional<bool> IgnoreBlacklist;
	/** Priority that determines which directories are scanned first, for when a directory is being waited on or are not blocking but are wanted for optional features */
	TOptional<EPriority> Priority;

	/** Used to early-exit from tree traversal when all properties have finished being handled */
	bool IsSet() const
	{
		return IsWhitelisted.IsSet() | HasScanned.IsSet() | IgnoreBlacklist.IsSet() | Priority.IsSet();
	}
};

/** Information needed about a discovered asset file or path that is needed by the Discoverer */
struct FDiscoveredPathData
{
	/** The absolute path to the file on disk, relative to the directory that issued the scan */
	FString LocalAbsPath;
	/** The LongPackageName of the path (inherited from the MountDir) */
	FString LongPackageName;
	/** The relative path from the path's parent directory */
	FString RelPath;
	/** If the path is a file, the modification timestamp of the package file (that it had when it was discovered) */
	FDateTime PackageTimestamp;

	FDiscoveredPathData() = default;
	FDiscoveredPathData(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath, const FDateTime& InPackageTimestamp);
	FDiscoveredPathData(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath);
	void Assign(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath, const FDateTime& InPackageTimestamp);
	void Assign(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath);

	/** Return the total amount of heap memory used by the gatherer (including not-yet-claimed search results). Used for performance metrics. */
	uint32 GetAllocatedSize() const;
};

/** Information needed about a discovered asset file or path that is needed by the Gatherer */
struct FGatheredPathData
{
	/** The absolute path to the file on disk, relative to the directory that issued the scan */
	FString LocalAbsPath;
	/** The LongPackageName of the path (inherited from the MountDir) */
	FString LongPackageName;
	/** The modification timestamp of the package file (that it had when it was discovered) */
	FDateTime PackageTimestamp;

	FGatheredPathData() = default;
	FGatheredPathData(FStringView InLocalAbsPath, FStringView InLongPackageName, const FDateTime& InPackageTimestamp);
	explicit FGatheredPathData(const FDiscoveredPathData& DiscoveredData);
	void Assign(FStringView InLocalAbsPath, FStringView InLongPackageName, const FDateTime& InPackageTimestamp);
	void Assign(const FDiscoveredPathData& DiscoveredData);

	/**
	 * Return the total amount of heap memory used by the gatherer (including not-yet-claimed search results).
	 * Used for performance metrics.
	 */
	uint32 GetAllocatedSize() const;
};

/**
 * Tree data node representing a directory in the scan; direct subdirectories are stored as other FScanDir instances
 * referenced from the SubDirs array. Directories are removed from the tree once their scans are finished to save memory.
 * Queries take into account that deleted nodes have been completed.
 *
 * This class is not ThreadSafe; The FAssetDataDiscovery reads/writes its data only while holding the TreeLock critical section.
 */
class FScanDir : public FRefCountBase
{
public:
	FScanDir(FMountDir& InMountDir, FScanDir* InParent, FStringView RelPath);
	~FScanDir();

	/**
	 * Marks that this ScanDir is no longer in use and clear its data. The ScanDir will remain allocated until all threads
	 * have dropped their reference to it.
	 */
	void Shutdown();

	/** Check whether this ScanDir is still alive or if it has been marked for destruction and cleared on another thread. */
	bool IsValid() const;

	FMountDir* GetMountDir() const;

	/** Get this ScanDir's RelPath from its Parent */
	FStringView GetRelPath() const;

	/** Return this ScanDir's accumulated Priority: the maximum of its directly-set priority and its children's priority. */
	EPriority GetPriority() const;

	/** Calculate this ScanDir's full absolute path by accumulating RelPaths from parents and append it. */
	void AppendLocalAbsPath(FStringBuilderBase& OutFullPath) const;
	/** Calculate this ScanDir's full absolute path by accumulating RelPaths from parents and return it as a string. */
	FString GetLocalAbsPath() const;
	/** Calculate the relative path from the MountPoint to this ScanDir and append it to OutRelPath.  */
	void AppendMountRelPath(FStringBuilderBase& OutRelPath) const;
	/** Calculate the relative path from the MountPoint to this ScanDir and return it as a string.  */
	FString GetMountRelPath() const;

	/** Report whether this ScanDir is blacklisted */
	bool IsBlacklisted() const;
	/**
	 * Return whether this ScanDir has been directly marked as whitelisted. A ScanDir may be indirectly whitelisted
	 * because it has a whitelisted parent directory; that is not reported by this function.
	 */
	bool IsDirectWhitelisted() const;
	/**
	 * Return whether this scandir is whitelisted and not blacklisted and hence is in the set of directories that
	 * need to be scanned.
	 * Considers whitelisting/blacklisting inherited from parents in addition to the direct settings on this scandir.
	 */
	bool IsRecursiveMonitored(bool bParentIsWhitelisted) const;
	/** Report whether the given RelPath is whitelisted, because either it or a parent is direct whitelisted. */
	bool IsPathWhitelisted(FStringView InRelPath, bool bParentIsWhitelisted) const;

	/** Report whether this ScanDir will be scanned in the current or future Tick. */
	bool ShouldScan(bool bParentIsWhitelisted) const;
	/** Report whether this ScanDir has been scanned. */
	bool HasScanned() const;
	/** Report whether this ScanDir is complete: has scanned or should not scan, and all subdirs have completed. */
	bool IsComplete() const;

	/** Return the memory used by the tree under this ScanDir; excludes sizeof(*this). */
	uint32 GetAllocatedSize() const;
	/**
	 * Find the Direct parent of InRelPath, or a fallback. Will return null only if !bIsDirectory and InRelPath is empty.
	 * The fallback is returned if InRelPath has already completed and been deleted, or if InRelPath is not recursively whitelisted or is blacklisted.
	 * The fallback is the lowest existing parent directory of InRelPath.
	 */
	FScanDir* GetControllingDir(FStringView InRelPath, bool bIsDirectory, bool bParentIsWhitelisted, bool& bOutIsWhitelisted, FString& OutRelPath);

	/**
	 * Set values of fields on the given directory indicated by InRelPath for all of the properties existing on InProperties.
	 * Returns whether the directory was found and its property was changed; returns false if InRelPath was not a
	 * directory or the property did not need to be changed.
	 */
	bool TrySetDirectoryProperties(FStringView InRelPath, const FSetPathProperties& InProperties, bool bConfirmedExists);
	/** Mark that the given file has already been scanned, so that it will not be double reporting in the upcoming directory scan, if one is upcoming. */
	void MarkFileAlreadyScanned(FStringView BaseName);
	/** Set the direct priority of this ScanDir and update the accumulated priority to match it. */
	void SetDirectPriority(EPriority InPriority);

	/**
	 * Called from the Tick; handle the list of subdirs and files that were found from IterateDirectoryStat called on this ScanDir,
	 * reporting discovered files and updating status variables.
	 */
	void SetScanResults(FStringView LocalAbsPath, TArrayView<FDiscoveredPathData>& InOutSubDirs, TArrayView<FDiscoveredPathData>& InOutFiles);
	/**
	 * Update the completion state of this ScanDir based on its scan status and its child dirs completion state.
	 * Set OutCursor to the highest priority child dir that needs to be updated if any children need to be updated and/or scanned first.
	 * Set OutCursor to the parent ScanDir (or null if there is no parent) if this ScanDir is now complete.
	 */
	void Update(FScanDir*& OutCursor, bool& bInOutParentIsWhitelisted);

	/** Thread-synchronization helper - return true if the Tick thread is in the middle of scanning this directory. */
	bool IsScanInFlight() const;
	/** Thread-synchronization helper - set that the Tick thread is starting/done with the scan of this directory. */
	void SetScanInFlight(bool bInScanInFlight);
	/** Thread-synchronization helper - report whether a non tick thread has marked that this directory is changed or invalidated and the scan should be thrown out. */
	bool IsScanInFlightInvalidated() const;
	/** Thread-synchronization helper - set that the current ongoing scan is invalidated, or clear the marker from the tick thread once it has been consumed. */
	void SetScanInFlightInvalidated(bool bInValidated);

	/** Set completion flags on this and its parents (and optionally its descendents) so that it will be updated again. */
	void MarkDirty(bool bMarkDescendents);

	/** Minimize data in internal buffers. */
	void Shrink();

protected:

	/**
	 * Setting to complete marks that this directory has been scanned, and all of its subdirectories have scanned as well, so it can skipped when it or its parent
	 * is waited on. Setting back to incomplete can occur when a AssetDataGatherer client wants to rescan the directory.
	 */
	virtual void SetComplete(bool bInIsComplete);

	/** Find the ScanDir subdirectory for the given basename, or return nullptr. */
	FScanDir* FindSubDir(FStringView SubDirBaseName);
	const FScanDir* FindSubDir(FStringView SubDirBaseName) const;
	/** Find the ScanDir subdirectory for the given basename, and add it if it does not exist. */
	FScanDir& FindOrAddSubDir(FStringView SubDirBaseName);
	/** Find the ScanDir subdirectory for the given basename, and if it exists, Shutdown and remove it from SubDirs, which will eventually delete it. */
	void RemoveSubDir(FStringView SubDirBaseName);

	/** Find the index of the subdir with the given Relative path. */
	int32 FindLowerBoundSubDir(FStringView SubDirBaseName);

	/** Return the the highest-priority not-yet-complete SubDir, or null if all are complete. */
	FScanDir* FindHighestPrioritySubDir();

	/** Call the given lambda void(FScanDir&) on each existing SubDir. */
	template <typename CallbackType> void ForEachSubDir(const CallbackType& Callback);
	/**
	 * Call the given lambda void(FScanDir&) on each present-in-memory descedent ScanDir of this.
	 * Does not look for directories on disk, only the ones that have already been created in memory.
	 * Depth-first-search traversal, called on parents before children.
	 */
	template <typename CallbackType> void ForEachDescendent(const CallbackType& Callback);

	/** Set the AccumulatedPriority of this ScanDir to the maximum of the DirectPriorities of it and all its child dirs. */
	void UpdateAccumulatedPriority();
	/** Record a changed priority of a direct child ScanDir and update the accumulated priority of this ScanDir. */
	void OnChildPriorityChanged(EPriority InPriority, int32 Delta);

	TArray<TRefCountPtr<FScanDir>> SubDirs; // Sorted
	TArray<FString> AlreadyScannedFiles; // Unsorted
	FMountDir* MountDir = nullptr;
	FScanDir* Parent = nullptr;
	FString RelPath;
	uint8 PriorityRefCounts[CountEPriority] = {}; // Initialize all elements to 0
	EPriority DirectPriority = EPriority::Normal;
	EPriority AccumulatedPriority = EPriority::Normal;
	bool bHasScanned = false;
	bool bIsDirectWhitelisted = false;
	bool bScanInFlight = false;
	bool bScanInFlightInvalidated = false;
	bool bIsComplete = false;
};

/**
 * Gather data about a MountPoint that has been registered with FPackageName
 * The FMountDir holds a FScanTree with information about each directory (that is pruned when not in use).
 * It also holds some data that is needed only per MountPoint, such as the packagename.
 * It also holds some data per subdirectory that is more performant to hold in a map rather than to require the FScanTrees to be kept.
 *
 * This class is not ThreadSafe; The FAssetDataDiscovery reads/writes its data only while holding the TreeLock critical section.
 */
class FMountDir
{
public:
	FMountDir(FAssetDataDiscovery& InDiscovery, FStringView LocalAbsPath, FStringView PackagePath);
	~FMountDir();

	/** The local filesystem path from FPackageName, as an absolute path d:\root\Engine\Content rather than a relative path ../../../Engine/Content. */
	FStringView GetLocalAbsPath() const;
	/** The package path from FPackageName. */
	FStringView GetLongPackageName() const;

	/**
	 * Get the set of blacklist paths that are child paths of this MountDir.
	 * Returned paths are relative paths from the MountDir's root.
	 */
	const TSet<FString>& GetBlacklist() const;
	/** Return the FAssetDataDiscovery that owns this FMountDir. */
	FAssetDataDiscovery& GetDiscovery() const;

	/** Find the direct parent of InRelPath, or the lowest fallback. See FScanDir::GetControllingDir. */
	FScanDir* GetControllingDir(FStringView LocalAbsPath, bool bIsDirectory, bool& bOutIsWhitelisted, FString& OutRelPath);
	/** Return the memory used by the tree under this MountDir, except that sizeof(*this) is excluded. */
	uint32 GetAllocatedSize() const;

	/** Report whether this MountDir is complete: all ScanDirs under it either have scanned or should not scan. */
	bool IsComplete() const;
	/** Return this MountDir's Priority, which is the maximum of any of its ScanDirs' priorities. */
	EPriority GetPriority() const;

	/** Return whether the given path is a child path of *this and is whitelisted. */
	bool IsPathWhitelisted(FStringView LocalAbsPath) const;
	/** Return whether the given path is a child path of *this and matches the blacklist filters. */
	bool IsBlacklisted(FStringView LocalAbsPath) const;
	/**
	 * Return whether the given path is a child path of *this and is whitelisted and is not blacklisted, which means
	 * it will be or has been scanned.
	 */
	bool IsMonitored(FStringView LocalAbsPath) const;

	/*
	 * Set values of fields on the given directory for all of the properties existing on InProperties.
	 * Returns whether the directory was foundand its property was changed; returns false if LocalAbsPath was not a
	 * directory under this MountDir or the property did not need to be changed.
	 */
	bool TrySetDirectoryProperties(FStringView LocalAbsPath, const FSetPathProperties& InProperties, bool bConfirmedExists);
	/** 
	 * Update all ScanDirs under this MountDir in priority order until one is found that needs to be scanned.
	 * Set OutCursor to that ScanDir, or to nullptr if all are complete.
	 * Set bOutCursorParentIsWhitelisted to whether the parent of the OutCursor is whitelisted,
	 * or to false if there is no OutCursor.
	 */
	void Update(FScanDir*& OutCursor, bool& bOutCursorParentIsWhitelisted);

	/** Record that a directory at or under the MountDir's root has been scanned, used to detect if configuration occurs after scanning starts. */
	void SetHasStartedScanning();
	/** Minimize data in internal buffers. */
	void Shrink();

	/** Record that the MountDir is rooted at a childpath of this. The childpath will not be scanned by this. */
	void AddChildMount(FMountDir* ChildMount);
	/** Mark that a childpath MountDir is being deleted and the childpath should be scanned again by this. */
	void RemoveChildMount(FMountDir* ChildMount);
	/** Remove all childmounts. Does not handle properly updating the MountDir to reown those paths; this is used during destruction all MountDirs. */
	void OnDestroyClearChildMounts();
	/** Record the backpointer to the parent mountdir that this mountdir's path is a child path of, or null if the parent no longer exists. */
	void SetParentMount(FMountDir* ParentMount);
	/** Return the parent MountDir. */
	FMountDir* GetParentMount() const;
	/** Return the MountDirs that have been recorded as ChildMounts. */
	TArray<FMountDir*> GetChildMounts() const;

protected:
	/** Inspect the Discovery's blacklists and add the ones applicable to this MountDir into this MountDir's set of Blacklists. */
	void UpdateBlacklist();
	/** Mark that given path needs to be reconsidered by Update. */
	void MarkDirty(FStringView MountRelPath);

	/** Holds data about child paths directly in the MountDir rather than requiring FScanDir children. This is more memory efficient and is performant for rarely accessed data. */
	struct FPathData
	{
		FPathData(FStringView MountRelPath);
		bool IsEmpty() const;
		uint32 GetAllocatedSize() const;

		/** Relative path from the mountdir. */
		FString RelPath;
		/** The path has been marked as should no longer be blacklisted by e.g. ScanPathsSynchronous. */
		bool bIgnoreBlacklist = false;
		/** The path is a childpath; another MountDir exists to scan it and it should not be scanned by this MountDir. */
		bool bIsChildPath = false;
	};
	/** Return the PathData with the given MountRelPath, or nullptr if none exists. */
	FPathData* FindPathData(FStringView MountRelPath);
	/** Return the PathData with the given MountRelPath, creating it if none exists. */
	FPathData& FindOrAddPathData(FStringView MountRelPath);
	/** Remove the PathData with the given MountRelPath if one exists. */
	void RemovePathData(FStringView MountRelPath);

	/** Array of data keyed by relative path from the MountDir for information about child paths that should not be stored in ScanDirs. */
	TArray<FPathData> PathDatas;
	/** Set of relative path from the MountDir paths that should not be scanned, because they were requested blacklisted by clients or because a childmount owns them. */
	TSet<FString> BlacklistedRelPaths;
	/** Absolute path to the root of the MountDir in the local file system. */
	FString LocalAbsPath;
	/** LongPackageName that was assigned to the MountDir in FPackageName. */
	FString LongPackageName;
	/** ScanDir for the root directory of this MountDir; child paths to scan will be created (and destroyed after use) as children of the ScanDir (with the exception of childmounts). */
	TRefCountPtr<FScanDir> Root;
	/** Backpointer to the Discovery that owns this MountDir. */
	FAssetDataDiscovery& Discovery;
	/** If this is a nested MountDir (a nested path was registered with FPackageName) ParentMount is a pointer to the FMountDir that corresponds to the registered parent directory. */
	FMountDir* ParentMount = nullptr;
	/** Records whether any directory at or under the MountDir's root has been scanned. */
	bool bHasStartedScanning = false;
};

/** Subsystem that discovers the files that FAssetDataGatherer should process.
 */
class FAssetDataDiscovery : public FRunnable
{
public:
	FAssetDataDiscovery(const TArray<FString>& InBlacklistLongPackageNames, const TArray<FString>& InBlacklistMountRelativePaths, bool bInIsSynchronous);
	virtual ~FAssetDataDiscovery();


	// Controlling Async behavior

	/** Start the async thread, if this Gatherer was created async. Does nothing if not async or already started. */
	void StartAsync();

	// FRunnable implementation
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Signals to end the thread and waits for it to close before returning */
	void EnsureCompletion();

	// Receiving Results and reading properties (possibly while tick is running)
	/** Gets search results from the file discovery. */
	void GetAndTrimSearchResults(bool& bOutIsComplete, TArray<FString>& OutDiscoveredPaths, TRingBuffer<FGatheredPathData>& OutDiscoveredFiles, int32& OutNumPathsToSearch);
	/** Wait (joining in on the tick) until all currently monitored paths have been scanned. */
	void WaitForIdle();
	/** Optionally set some scan properties for the given path and then wait for the scan of it to finish. */
	void SetPropertiesAndWait(const FString& LocalAbsPath, bool bAddToWhitelist, bool bForceRescan, bool bIgnoreBlackListScanFilters);
	/** Return whether the given path is whitelisted due to e.g. TrySetDirectoryProperties with IsWhitelisted. */
	bool IsWhitelisted(FStringView LocalAbsPath) const;
	/** Return whether the given path is blacklisted due to matching blacklist settings and not having been marked IgnoreBlacklist. */
	bool IsBlacklisted(FStringView LocalAbsPath) const;
	/** Return whether the given path should be scanned (possibly already has been) because it is whitelisted and not blacklisted. */
	bool IsMonitored(FStringView LocalAbsPath) const;
	/** Return the memory used by *this. sizeof(*this) is not included. */
	uint32 GetAllocatedSize() const;

	// Events and setting of properties (possibly while tick is running)
	/** Register the given LocalAbsPath/LongPackageName pair that came from FPackageName's list of mount points as a mountpoint to track. Will not be scanned until whitelisted. */
	void AddMountPoint(const FString& LocalAbsPath, FStringView LongPackageName);
	/** Remove the mountpoint because FPackageName has removed it. */
	void RemoveMountPoint(const FString& LocalAbsPath);
	/** Set properties on the directory, called from AssetDataGatherer when files are requested whitelisted, blacklisted, or rescanned. */
	bool TrySetDirectoryProperties(const FString& LocalAbsPath, const UE::AssetDataGather::Private::FSetPathProperties& Properties, bool bConfirmedExists);
	/** Event called from the directory watcher when a directory is created under a mount point. It will be added to the scan if IsMonitored. */
	void OnDirectoryCreated(FStringView LocalAbsPath);
	/** Event called from the directory watcher when files are created under a mount point. Each will be reported to the Gatherer if IsMonitored. */
	void OnFilesCreated(TConstArrayView<FString> LocalAbsPaths);

private:
	/** Find the MountDir with a root that contains LocalAbsPath, finding the lowest child if there are multiple. */
	FMountDir* FindContainingMountPoint(FStringView LocalAbsPath);
	const FMountDir* FindContainingMountPoint(FStringView LocalAbsPath) const;
	/** Find the mountpoint with the given root. */
	FMountDir* FindMountPoint(FStringView LocalAbsPath);
	/** Find the mountpoint with the given root, creating it with root and packagename if it does not already exist. Handle registration of child mountdirs. */
	FMountDir& FindOrAddMountPoint(FStringView LocalAbsPath, FStringView LongPackageName);
	/** Execute the search on the sorted-by-path mountdirs, returning the first mountdir with root >= the given path. */
	int32 FindLowerBoundMountPoint(FStringView LocalAbsPath) const;

	/** Run the tick, either called from the async Run or called on thread from a thread executing a synchronous wait. */
	void TickInternal();
	/** Search all of the MountDirs for a highest priority ScanDir that needs to be Updated. */
	void FindFirstCursor(FScanDir*& OutCursor, bool& bOutCursorParentIsWhitelisted);
	/** Invalidate the current cursor set from the tick, called when properties are set and we need to reconsider whether directories ShouldBeScanned. */
	void InvalidateCursor();
	/** Mark that this discoverer has either finished and is idle, or has received new data and is no longer idle. Update properties dependent upon the idle state. */
	void SetIsIdle(bool bInIdle);

	/** Store the given discovered files and directories in the results. */
	void AddDiscovered(TConstArrayView<FDiscoveredPathData> SubDirs, TConstArrayView<FDiscoveredPathData> Files);
	/**
	 * Return whether a directory with the given LongPackageName should be reported to the AssetRegistry
	 * We do not report some directories because they are paths that should not enter the AssetRegistry list of paths if empty,
	 * and reporting a path to the AssetRegistry adds it unconditionally to the list of paths.
	 * If ShouldDirBeReported returns false, the directory will still be added to the catalog if non-empty, because the AssetRegistry adds the path of every added file.
	 */
	bool ShouldDirBeReported(FStringView LongPackageName) const;

	/** Handle the actions necessary for a single created file. */
	void OnFileCreated(const FString& LocalPaths);
	/** Helper for TrySetDirectoryProperties and other internals that need to set it, called from within the critical section. */
	bool TrySetDirectoryPropertiesInternal(const FString& LocalAbsPath, const UE::AssetDataGather::Private::FSetPathProperties& Properties, bool bConfirmedExists);

	/** Add the given path as a MountPoint and update child registrations. */
	void AddMountPointInternal(const FString& LocalAbsPath, FStringView LongPackageName);
	/** Remove the given path as a MountPoint and update child registrations. */
	void RemoveMountPointInternal(const FString& LocalAbsPath);

	/** Minimize memory usage in the buffers used during gathering. */
	void Shrink();

	/** Scoped guard for pausing the asynchronous tick. */
	struct FScopedPause
	{
		FScopedPause(const FAssetDataDiscovery& InOwner);
		~FScopedPause();
		const FAssetDataDiscovery& Owner;
	};

private:
	/**
	 * Prevent simultaneous ticks from two different threads and protect access to Tick-specific data.
	 * To prevent DeadLocks, TickLock can not be entered from within any of the other locks on this class.
	 */
	mutable FGathererCriticalSection TickLock;
	/**
	 * Protect access to data in the ScanDir tree which can be read/write from the tick or from SetProperties.
	 * To prevent DeadLocks, TreeLock can be entered while holding TickLock, but can not be entered from
	 * within any of the other locks on this class.
	 */
	mutable FGathererCriticalSection TreeLock;
	/**
	 * Protect access to the data written from tick and read/written from GetAndTrimSearchResults.
	 * ResultsLock can be entered while holding TickLock or TreeLock or both.
	 */
	mutable FGathererCriticalSection ResultsLock;


	// Variable section for variables that are constant during threading.

	/** Blacklisted paths that are full absolute paths. Child paths will not be scanned unless requested to ignore blacklists. Constant during threading. */
	TArray<FString> BlacklistLongPackageNames;
	/** Blacklisted paths that are relative paths in each mount. Child paths will not be scanned unless requested to ignore blacklists. Constant during threading. */
	TArray<FString> BlacklistMountRelativePaths;
	/** LongPackageNames for directories that should not be reported, see ShouldDirBeReported. Constant during threading. */
	TSet<FString> DirLongPackageNamesToNotReport;
	/** Thread to run the discovery FRunnable on. Read-only while threading is possible. Constant during threading. */
	FRunnableThread* Thread;
	/** True if this gather request is synchronous (i.e, IsRunningCommandlet()). Constant during threading. */
	bool bIsSynchronous;


	// Variable section for variables that are atomics read/writable from outside critical sections.

	/**
	 * Whether this Discoverer has finished all work (may be still present in the results.)
	 * Readable anywhere. Writable only within TreeLock.
	 */
	std::atomic<bool> bIsIdle;
	/** > 0 if we've been asked to abort work in progress at the next opportunity. Read/writable anywhere. */
	std::atomic<uint32> IsStopped;
	/** > 0 if we've been asked to pause the worker thread so a synchronous function can take over the tick. Read/writable anywhere. */
	mutable std::atomic<uint32> IsPaused;
	/**
	 * Number of directories that have been discovered and IsMonitored but have not yet been scanned.
	 * Used for progress tracking. Read/writable anywhere.
	 */
	FThreadSafeCounter NumDirectoriesToScan;


	// Variable section for variables that are read/writable only within ResultsLock.

	/** Directories found in the scan; may be empty. Read/writable only within ResultsLock. */
	TArray<FString> DiscoveredDirectories;
	/** Files found found in the scan. Read/writable only within ResultsLock. */
	TArray<FGatheredPathData> DiscoveredFiles;
	/** Time at which the scan was started or last resumed from idle. Used for logging. Read/writable only within ResultsLock. */
	double DiscoverStartTime = 0.;
	/** Number of files discovered during scanning since start or resumed from idle. Used for logging. Read/writable only within ResultsLock. */
	int32 NumDiscoveredFiles = 0;


	// Variable section for variables that are read/writable only within TreeLock.

	/**
	 * Sorted list of MountDirs, sorted by FPackagePath::Less on the absolute paths. Read/writable only within TreeLock.
	 * Each MountDir contains a ScanDir tree and other data that configures the scanning within that MountPoint.
	 * Read/writable only with TreeLock, both the list and all data owned by each MountDir.
	 */
	TArray<TUniquePtr<FMountDir>> MountDirs;
	/** The next ScanDir to update in Tick. Read/writable only within TreeLock. */
	TRefCountPtr<FScanDir> Cursor = nullptr;
	/** The value of ParentIsWhitelisted for Cursor (or false if Cursor is null). Read/writable only within TreeLock. */
	bool bCursorParentIsWhitelisted = false;


	// Variable section for variables that are read/writable only within TickLock.

	/** Scratch space to store discovered subdirs during the tick, to avoid allocations. Read/writable only within TickLock. */
	TArray<FDiscoveredPathData> IteratedSubDirs;
	/** Scratch space to store discovered files during the tick, to avoid allocations. Read/writable only within TickLock. */
	TArray<FDiscoveredPathData> IteratedFiles;


	friend class FMountDir;
	friend class FScanDir;
};

} // namespace Private
} // namespace AssetDataGather
} // namespace UE