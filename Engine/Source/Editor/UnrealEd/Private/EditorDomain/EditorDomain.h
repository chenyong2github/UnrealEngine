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
#include "TickableEditorObject.h"
#include "UObject/NameTypes.h"
#include "UObject/PackageResourceManager.h"

class FAssetPackageData;
class FEditorDomainSaveClient;
class IAssetRegistry;
class UPackage;
namespace UE::EditorDomain
{
	typedef FIoHash FPackageDigest;

	/** A UClass's data that is used in the EditorDomain Digest */
	struct FClassDigestData
	{
		FBlake3Hash SchemaHash;
		bool bNative;
	};
	/** Threadsafe cache of ClassName -> Digest data for calculating EditorDomain Digests */
	struct FClassDigestMap
	{
		TMap<FName, FClassDigestData> Map;
		FCriticalSection Lock;
	};
}

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
	virtual IAsyncReadFileHandle* OpenAsyncReadPackage(const FPackagePath& PackagePath,
		EPackageSegment PackageSegment) override;
	virtual IMappedFileHandle* OpenMappedHandleToPackage(const FPackagePath& PackagePath,
		EPackageSegment PackageSegment, FPackagePath* OutUpdatedPath = nullptr) override;
	virtual bool TryMatchCaseOnDisk(const FPackagePath& PackagePath, FPackagePath* OutNormalizedPath = nullptr) override;
	virtual TUniquePtr<FArchive> OpenReadExternalResource(EPackageExternalResource ResourceType, FStringView Identifier) override;
	virtual bool DoesExternalResourceExist(EPackageExternalResource ResourceType, FStringView Identifier) override;
	virtual IAsyncReadFileHandle* OpenAsyncReadExternalResource(
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
	virtual void OnEndLoad(TConstArrayView<UPackage*> LoadedPackages) override;

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override { return TStatId(); }

	// EditorDomain interface
	void PrecachePackageDigest(FName PackageName);

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
	/** Different options for which domain a package comes from. */
	enum class EPackageSource
	{
		Undecided,
		Workspace,
		Editor
	};
	/**
	 * Data about which domain a package comes from. Multiple queries of the same
	 * package have to align (for e.g. BulkData offsets) so we have to keep track of this.
	 */
	struct FPackageSource : public FThreadSafeRefCountedObject
	{
		UE::EditorDomain::FPackageDigest Digest;
		EPackageSource Source = EPackageSource::Undecided;
		bool bHasSaved = false;
		bool NeedsEditorDomainSave() const
		{
			return !bHasSaved && Source == EPackageSource::Workspace;
		}
	};

	/** Disallow copy constructors */
	FEditorDomain(const FEditorDomain& Other) = delete;
	FEditorDomain(FEditorDomain&& Other) = delete;

	/** Read the PackageSource data (domain&digest) from PackageSources, or from the asset registry if not in PackageSources. */
	bool TryFindOrAddPackageSource(const FPackagePath& PackagePath, TRefCountPtr<FPackageSource>& OutSource);
	/** Return the PackageSource data in PackageSources, if it exists */
	TRefCountPtr<FPackageSource> FindPackageSource(const FPackagePath& PackagePath);
	/** Mark that we had to load the Package from the workspace domain, and schedule its save into the EditorDomain. */
	void MarkNeedsLoadFromWorkspace(const FPackagePath& PackagePath, TRefCountPtr<FPackageSource>& PackageSource);
	/** Callback for PostEngineInit, to handle saving of packages which we could not save before then. */
	void OnPostEngineInit();
	/** For each of the now-loaded packages, if we had to load from workspace domain, save into the editor domain. */
	void FilterKeepPackagesToSave(TArray<UPackage*>& InOutLoadedPackages);

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
	/** Cache of GetSchemaHash by class name */
	UE::EditorDomain::FClassDigestMap ClassDigests;
	/** True by default, set to false when reading is disabled for testing. */
	bool bEditorDomainReadEnabled = true;
	/** If true, use an out-of-process EditorDomainSaveServer for saves, else save in process in EndLoad */
	bool bExternalSave = false;
	/** Marker for whether our PostEngineInit callback has been called */
	bool bHasPassedPostEngineInit = false;

	static FEditorDomain* RegisteredEditorDomain;

	friend class FEditorDomainRegisterAsPackageResourceManager;
	friend class FEditorDomainReadArchive;
	friend class FEditorDomainAsyncReadFileHandle;
};