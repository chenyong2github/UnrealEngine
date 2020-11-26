// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/PackageResourceManager.h"

#include "Misc/PackageSegment.h"
#include "Misc/PreloadableFile.h"
#include "Misc/ScopeLock.h"
#include "Serialization/Archive.h"
#include "UObject/PackageResourceManagerFile.h"

DEFINE_LOG_CATEGORY(LogPackageResourceManager);

namespace
{
	IPackageResourceManager* GPackageResourceManager = nullptr;
	FSetPackageResourceManager GSetPackageResourceManagerDelegate;
}

IPackageResourceManager& IPackageResourceManager::Get()
{
	check(GPackageResourceManager);
	return *GPackageResourceManager;
}

FSetPackageResourceManager& IPackageResourceManager::GetSetPackageResourceManagerDelegate()
{
	return GSetPackageResourceManagerDelegate;
}

void IPackageResourceManager::Initialize()
{
	FSetPackageResourceManager& SetManagerDelegate = GetSetPackageResourceManagerDelegate(); 
	if (SetManagerDelegate.IsBound())
	{
		// Allow the editor or licensee project to define the PackageResourceManager
		GPackageResourceManager = SetManagerDelegate.Execute();
	}

	// Assign the default PackageResourceManager if it was not set by a higher level source
	if (!GPackageResourceManager)
	{
		GPackageResourceManager = MakePackageResourceManagerFile();
	}
}

void IPackageResourceManager::Shutdown()
{
	delete GPackageResourceManager;
	GPackageResourceManager = nullptr;
}

bool IPackageResourceManager::DoesPackageExist(const FPackagePath& PackagePath, FPackagePath* OutUpdatedPath)
{
	return DoesPackageExist(PackagePath, EPackageSegment::Header, OutUpdatedPath);
}

int64 IPackageResourceManager::FileSize(const FPackagePath& PackagePath, FPackagePath* OutUpdatedPath)
{
	return FileSize(PackagePath, EPackageSegment::Header, OutUpdatedPath);
}

FOpenPackageResult IPackageResourceManager::OpenReadPackage(const FPackagePath& PackagePath, FPackagePath* OutUpdatedPath)
{
	return OpenReadPackage(PackagePath, EPackageSegment::Header, OutUpdatedPath);
}

IAsyncReadFileHandle* IPackageResourceManager::OpenAsyncReadPackage(const FPackagePath& PackagePath)
{
	return OpenAsyncReadPackage(PackagePath, EPackageSegment::Header);
}

IMappedFileHandle* IPackageResourceManager::OpenMappedHandleToPackage(const FPackagePath& PackagePath, FPackagePath* OutUpdatedPath)
{
	return OpenMappedHandleToPackage(PackagePath, EPackageSegment::Header, OutUpdatedPath);
}

void IPackageResourceManager::FindPackagesRecursive(TArray<FPackagePath>& OutPackages, FStringView PackageMount,
	FStringView FileMount, FStringView RootRelPath, FStringView BasenameWildcard)
{
	TArray<TPair<FPackagePath, EPackageSegment>> PackageSegments;
	FindPackagesRecursive(PackageSegments, PackageMount, FileMount, RootRelPath, BasenameWildcard);
	OutPackages.Reserve(PackageSegments.Num());
	for (const TPair<FPackagePath, EPackageSegment>& PackageSegment : PackageSegments)
	{
		OutPackages.Add(PackageSegment.Get<0>());
	}
}

void IPackageResourceManager::IteratePackagesInPath(FStringView PackageMount, FStringView FileMount, FStringView RootRelPath,
	FPackagePathVisitor Callback)
{
	IteratePackagesInPath(PackageMount, FileMount, RootRelPath,
		[&Callback](const FPackagePath& PackagePath, EPackageSegment Segment) -> bool
		{
			if (Segment != EPackageSegment::Header)
			{
				return true;
			}
			return Callback(PackagePath);
		});
}

void IPackageResourceManager::IteratePackagesInLocalOnlyDirectory(FStringView RootDir, FPackagePathVisitor Callback)
{
	IteratePackagesInLocalOnlyDirectory(RootDir,
		[&Callback](const FPackagePath& PackagePath, EPackageSegment Segment) -> bool
		{
			if (Segment != EPackageSegment::Header)
			{
				return true;
			}
			return Callback(PackagePath);
		});
}

void IPackageResourceManager::IteratePackagesStatInPath(FStringView PackageMount, FStringView FileMount, FStringView RootRelPath,
	FPackagePathStatVisitor Callback)
{
	IteratePackagesStatInPath(PackageMount, FileMount, RootRelPath,
		[&Callback](const FPackagePath& PackagePath, EPackageSegment Segment, const FFileStatData& StatData) -> bool
		{
			if (Segment != EPackageSegment::Header)
			{
				return true;
			}
			return Callback(PackagePath, StatData);
		});
}

/**
 * Call the callback - with stat data - on all packages in the given local path
 *
 * PackageResourceManagers that do not support LocalOnly paths will return without calling the Callback
 *
 * @param RootDir The local path on disk to search
 * @param Callback The callback called on each package
 * @param bOutSupported If nonnull, receives a true or false value for whether this PackageResourceManager supports
 *        searching LocalOnlyDirectories
 */
void IPackageResourceManager::IteratePackagesStatInLocalOnlyDirectory(FStringView RootDir, FPackagePathStatVisitor Callback)
{
	IteratePackagesStatInLocalOnlyDirectory(RootDir,
		[&Callback](const FPackagePath& PackagePath, EPackageSegment Segment, const FFileStatData& StatData) -> bool
		{
			if (Segment != EPackageSegment::Header)
			{
				return true;
			}
			return Callback(PackagePath, StatData);
		});
}

#if WITH_EDITOR
TMap<FName, TPair<TSharedPtr<FPreloadableArchive>, EPackageFormat>> IPackageResourceManager::PreloadedPaths;
FCriticalSection IPackageResourceManager::PreloadedPathsLock;

bool IPackageResourceManager::TryRegisterPreloadableArchive(const FPackagePath& PackagePath,
	const TSharedPtr<FPreloadableArchive>& PreloadableArchive, EPackageFormat PackageFormat)
{
	const FName PackageName = PackagePath.GetPackageFName();
	if (PackageName.IsNone())
	{
		return false;
	}
	check(PreloadableArchive.IsValid());
	FScopeLock ScopeLock(&PreloadedPathsLock);
	TPair<TSharedPtr<FPreloadableArchive>, EPackageFormat>& ExistingData = PreloadedPaths.FindOrAdd(PackageName);
	TSharedPtr<FPreloadableArchive>& ExistingArchive = ExistingData.Get<0>();
	EPackageFormat& ExistingFormat = ExistingData.Get<1>();
	if (ExistingArchive)
	{
		if (ExistingArchive.Get() == PreloadableArchive.Get())
		{
			check(ExistingFormat == PackageFormat);
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		ExistingArchive = PreloadableArchive;
		ExistingFormat = PackageFormat;
		return true;
	}
}

bool IPackageResourceManager::TryTakePreloadableArchive(const FPackagePath& PackagePath, FOpenPackageResult& OutResult)
{
	OutResult.Format = EPackageFormat::Binary;
	OutResult.Archive = nullptr;
	const FName PackageName = PackagePath.GetPackageFName();
	if (PackageName.IsNone())
	{
		return false;
	}
	FScopeLock ScopeLock(&PreloadedPathsLock);
	if (PreloadedPaths.Num() == 0)
	{
		return false;
	}

	TPair<TSharedPtr<FPreloadableArchive>, EPackageFormat> ExistingData;
	if (!PreloadedPaths.RemoveAndCopyValue(PackageName, ExistingData))
	{
		return false;
	}
	TSharedPtr<FPreloadableArchive>& PreloadableArchive = ExistingData.Get<0>();
	EPackageFormat PackageFormat = ExistingData.Get<1>();
	if (!PreloadableArchive || !PreloadableArchive->IsInitialized())
	{
		// Someone has called Close on the Archive already.
		return false;
	}

	OutResult.Format = PackageFormat;
	OutResult.Archive = TUniquePtr<FArchive>(PreloadableArchive->DetachLowerLevel());
	// If DetachedLowerLevel returns non-null, the PreloadableArchive is in PreloadHandle mode;
	// it is not preloading bytes, but instead is only providing a pre-opened (and possibly primed) sync handle
	if (!OutResult.Archive)
	{
		// Otherwise the archive is in PreloadBytes mode, and we need to return a proxy to it
		OutResult.Archive = TUniquePtr<FArchive>(new FPreloadableArchiveProxy(PreloadableArchive));
	}
	return true;
}

bool IPackageResourceManager::UnRegisterPreloadableArchive(const FPackagePath& PackagePath)
{
	FName PackageName = PackagePath.GetPackageFName();
	if (PackageName.IsNone())
	{
		return false;
	}
	FScopeLock ScopeLock(&PreloadedPathsLock);
	int32 NumRemoved = PreloadedPaths.Remove(PackageName);
	return NumRemoved > 0;
}
#endif