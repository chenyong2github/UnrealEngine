// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "Hash/Blake3.h"
#include "IO/IoHash.h"
#include "Logging/LogMacros.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"
#include "TickableEditorObject.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/PackageResourceManager.h"

class FAssetPackageData;
class FEditorDomainSaveClient;
class IAssetRegistry;
class UObject;
class UPackage;
struct FAssetData;
namespace UE::DerivedData { class FRequestOwner; }

namespace UE::EditorDomain
{

/** Flags for whether a package is allowed to be loaded/saved in the EditorDomain. */
enum class EDomainUse : uint8
{
	None = 0x0,
	/** The package can be loaded from the EditorDomain. */
	LoadEnabled = 0x1,
	/** The package can be saved to the EditorDomain. */
	SaveEnabled = 0x2,
};
ENUM_CLASS_FLAGS(EDomainUse);

/** Information about a package's loading from the EditorDomain. */
struct FPackageDigest
{
	enum class EStatus : uint8
	{
		NotYetRequested,
		Successful,
		InvalidPackageName,
		DoesNotExistInAssetRegistry,
		MissingClass,
		MissingCustomVersion,
	};

	FPackageDigest() = default;
	FPackageDigest(EStatus InStatus, FName InStatusArg = NAME_None);
	bool IsSuccessful() const;
	FString GetStatusString() const;

	/**
	 * Hash used to lookup the resaved package in the cache;
	 * created from properties that change if the saved version would change (package bytes, serialization versions).
	 */
	FIoHash Hash;
	/** Classes used by the resaved package: imports or instances that can be created by PostLoad/PreSave */
	TArray<FName> ImportedClasses;
	/** List of CustomVersions used to save the package. */
	UE::AssetRegistry::FPackageCustomVersionsHandle CustomVersions;
	/** Allow flags for whether the package can be saved/loaded from EditorDomain. */
	EDomainUse DomainUse = EDomainUse::None;
	/** Status for creation of this digest. Either Success or an error code for why it couldn't be created. */
	EStatus Status = EStatus::NotYetRequested;
	/** Extended information for the status description (e.g. missing class name). */
	FName StatusArg;
};

}

extern FString LexToString(UE::EditorDomain::FPackageDigest::EStatus Status, FName StatusArg);

DECLARE_LOG_CATEGORY_EXTERN(LogEditorDomain, Log, All);

/**
 * The EditorDomain is container for optimized but still editor-usable versions of WorkspaceDomain packages.
 * The WorkspaceDomain is the source data for Unreal packages; packages created by the editor or compatible importers
 * that can be read by any future build of the project's editor. This source data is converted to an optimized format
 * for the current binary and saved into the EditorDomain, for faster loads when requested again by a later invocation
 * of the editor. The optimizations include running upgrades in UObjects' PostLoad and Serialize, and saving the
 * package in Unversioned format.
 *
 * FEditorDomain is a subclass of IPackageResourceManager that handles PackagePath requests by looking up the
 * package in the EditorDomain cache, stored in the DerivedDataCache. If a version of the package matching the current
 * WorkspaceDomain package and the current binary does not exist, then the EditorDomain falls back to loading from
 * the WorkspaceDomain (through ordinary IFileManager operations on the Root/Game/Content folders) and creates
 * the EditorDomain version for next time.
 */
class FEditorDomain : public IPackageResourceManager, public FTickableEditorObject
{
public:
	/** Different options for which domain a package comes from. */
	enum class EPackageSource
	{
		Undecided,
		Workspace,
		Editor
	};

	FEditorDomain();
	virtual ~FEditorDomain();

	/** Return the EditorDomain that is registered as the global PackageResourceManager, if there is one. */
	static FEditorDomain* Get();

	// IPackageResourceManager interface
	virtual bool SupportsLocalOnlyPaths() override;
	virtual bool SupportsPackageOnlyPaths() override;
	virtual bool DoesPackageExist(const FPackagePath& PackagePath, EPackageSegment PackageSegment,
		FPackagePath* OutUpdatedPath = nullptr) override;
	virtual int64 FileSize(const FPackagePath& PackagePath, EPackageSegment PackageSegment,
		FPackagePath* OutUpdatedPath = nullptr) override;
	virtual FOpenPackageResult OpenReadPackage(const FPackagePath& PackagePath, EPackageSegment PackageSegment,
		FPackagePath* OutUpdatedPath = nullptr) override;
	virtual FOpenAsyncPackageResult OpenAsyncReadPackage(const FPackagePath& PackagePath,
		EPackageSegment PackageSegment) override;
	virtual IMappedFileHandle* OpenMappedHandleToPackage(const FPackagePath& PackagePath,
		EPackageSegment PackageSegment, FPackagePath* OutUpdatedPath = nullptr) override;
	virtual bool TryMatchCaseOnDisk(const FPackagePath& PackagePath, FPackagePath* OutNormalizedPath = nullptr) override;
	virtual TUniquePtr<FArchive> OpenReadExternalResource(EPackageExternalResource ResourceType, FStringView Identifier) override;
	virtual bool DoesExternalResourceExist(EPackageExternalResource ResourceType, FStringView Identifier) override;
	virtual FOpenAsyncPackageResult OpenAsyncReadExternalResource(
		EPackageExternalResource ResourceType, FStringView Identifier) override;
	virtual void FindPackagesRecursive(TArray<TPair<FPackagePath, EPackageSegment>>& OutPackages, FStringView PackageMount,
		FStringView FileMount, FStringView RootRelPath, FStringView BasenameWildcard) override;
	virtual void IteratePackagesInPath(FStringView PackageMount, FStringView FileMount, FStringView RootRelPath,
		FPackageSegmentVisitor Callback) override;
	virtual void IteratePackagesInLocalOnlyDirectory(FStringView RootDir, FPackageSegmentVisitor Callback) override;
	virtual void IteratePackagesStatInPath(FStringView PackageMount, FStringView FileMount, FStringView RootRelPath,
		FPackageSegmentStatVisitor Callback) override;
	virtual void IteratePackagesStatInLocalOnlyDirectory(FStringView RootDir, 
		FPackageSegmentStatVisitor Callback) override;

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override { return TStatId(); }

	// EditorDomain interface
	/** Fetch data from game-thread sources that is required to calculate the PackageDigest of the given PackageName. */
	void PrecachePackageDigest(FName PackageName);
	/**
	 * Calculate the PackageDigest for the given PackageName.
	 * Callable from any thread, but will fail if game-thread data is required and not cached.
	 */
	UE::EditorDomain::FPackageDigest GetPackageDigest(FName PackageDigest);

	/** Request the download of the given packages from the upstream DDC server. */
	void BatchDownload(TArrayView<FName> PackageNames);

private:
	/**
	 * Reference-counted struct holding the locks used for multithreaded synchronization.
	 * Shared with Archives and other helpers that might outlive *this.
	 */
	class FLocks : public FThreadSafeRefCountedObject
	{
	public:
		FLocks(FEditorDomain& InOwner);
		FEditorDomain* Owner;
		FCriticalSection Lock;
	};
	/**
	 * Data about which domain a package comes from. Multiple queries of the same
	 * package have to align (for e.g. BulkData offsets) so we have to keep track of this.
	 */
	struct FPackageSource : public FThreadSafeRefCountedObject
	{
		FPackageSource()
			:bHasSaved(false), bHasLoaded(false), bHasQueriedCatalog(false), bLoadedAfterCatalogLoaded(false)
		{
		}

		/** Return whether we should call TrySavePackage into the EditorDomain. */
		bool NeedsEditorDomainSave(FEditorDomain& EditorDomain) const;
		/** Mark that a load is being attempted, which can impact whether we need to save. */
		void SetHasLoaded();

		UE::EditorDomain::FPackageDigest Digest;
		EPackageSource Source = EPackageSource::Undecided;
		bool bHasSaved : 1;
		bool bHasLoaded : 1;
		bool bHasQueriedCatalog : 1;
		bool bLoadedAfterCatalogLoaded : 1;
	};

	/** Disallow copy constructors */
	FEditorDomain(const FEditorDomain& Other) = delete;
	FEditorDomain(FEditorDomain&& Other) = delete;

	/** Read the PackageSource data from PackageSources, or from the asset registry if not in PackageSources. */
	bool TryFindOrAddPackageSource(FName PackageName, TRefCountPtr<FPackageSource>& OutSource,
		UE::EditorDomain::FPackageDigest* OutErrorDigest=nullptr);
	/** Return the PackageSource data in PackageSources, if it exists */
	TRefCountPtr<FPackageSource> FindPackageSource(const FPackagePath& PackagePath);
	/** Mark that we had to load the Package from the workspace domain, and schedule its save into the EditorDomain. */
	void MarkNeedsLoadFromWorkspace(const FPackagePath& PackagePath, TRefCountPtr<FPackageSource>& PackageSource);
	/** Callback for PostEngineInit, to handle saving of packages which we could not save before then. */
	void OnPostEngineInit();
	/** EndLoad callback to handle saving the EditorDomain version of the package. */
	void OnEndLoadPackage(TConstArrayView<UPackage*> LoadedPackages);
	/** For each of the now-loaded packages, if we had to load from workspace domain, save into the editor domain. */
	void FilterKeepPackagesToSave(TArray<UPackage*>& InOutLoadedPackages);
	/** PackageSaved context to invalidate our information about where it should be loaded from. */
	void OnPackageSavedWithContext(const FString& PackageFileName, UPackage* Package,
		FObjectPostSaveContext ObjectSaveContext);
	/** AssetUpdated event to invalidate our information about where it should be loaded from. */
	void OnAssetUpdatedOnDisk(const FAssetData& AssetData);
	/** Same As GetPackageDigest, but assumes lock is already held. */
	UE::EditorDomain::FPackageDigest GetPackageDigest_WithinLock(FName PackageDigest);


	/** Subsystem used to request the save of missing packages into the EditorDomain from a separate process. */
	TUniquePtr<FEditorDomainSaveClient> SaveClient;
	/** PackageResourceManagerFile to fall back to WorkspaceDomain when packages are missing from EditorDomain. */
	TUniquePtr<IPackageResourceManager> Workspace;
	/** Cached pointer to the global AssetRegistry. */
	IAssetRegistry* AssetRegistry = nullptr;
	/** Locks used by *this and its helper objects. */
	TRefCountPtr<FLocks> Locks;
	/** Digests previously found for a package. Used for optimization, but also to record loaded-from-domain. */
	TMap<FName, TRefCountPtr<FPackageSource>> PackageSources;
	/** RequestOwner used for BatchDownloads from upstream server. */
	TUniquePtr<UE::DerivedData::FRequestOwner> BatchDownloadOwner;

	/** True by default, set to false when reading is disabled for testing. */
	bool bEditorDomainReadEnabled = true;
	/** If true, use an out-of-process EditorDomainSaveServer for saves, else save in process in EndLoad */
	bool bExternalSave = false;
	/** Marker for whether our PostEngineInit callback has been called */
	bool bHasPassedPostEngineInit = false;
	/**
	 * Configuration value for whether saves into the EditorDomain should be skipped for packages loaded before
	 * the DDC server reports whether they exist in the upstream DDC.
	 */
	bool bSkipSavesUntilCatalogLoaded = false;

	static FEditorDomain* RegisteredEditorDomain;

	friend class FEditorDomainRegisterAsPackageResourceManager;
	friend class FEditorDomainPackageSegments;
	friend class FEditorDomainReadArchive;
	friend class FEditorDomainAsyncReadFileHandle;
};