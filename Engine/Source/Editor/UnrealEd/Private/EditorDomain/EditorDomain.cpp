// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDomain/EditorDomain.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/AsyncFileHandleNull.h"
#include "Containers/UnrealString.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheRecord.h"
#include "EditorDomain/EditorDomainArchive.h"
#include "EditorDomain/EditorDomainSave.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "HAL/CriticalSection.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackagePath.h"
#include "Misc/PackageSegment.h"
#include "Misc/ScopeLock.h"
#include "Serialization/Archive.h"
#include "Serialization/CompactBinary.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"
#include "UObject/PackageResourceManagerFile.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogEditorDomain);

/** Add a hook to the PackageResourceManager's startup delegate to use the EditorDomain as the IPackageResourceManager */
class FEditorDomainRegisterAsPackageResourceManager
{
public:
	FEditorDomainRegisterAsPackageResourceManager()
	{
		IPackageResourceManager::GetSetPackageResourceManagerDelegate().BindStatic(SetPackageResourceManager);
	}

	static IPackageResourceManager* SetPackageResourceManager()
	{
		bool bEditorDomainEnabled = false;
		if (GIsEditor && (!IsRunningCommandlet() || IsRunningCookCommandlet()))
		{
			GConfig->GetBool(TEXT("CookSettings"), TEXT("EditorDomainEnabled"), bEditorDomainEnabled, GEditorIni);
			if (bEditorDomainEnabled)
			{
				// Set values for config settings EditorDomain depends on
				GAllowUnversionedContentInEditor = 1;

				// Create the editor domain and return it as the package resource manager
				check(FEditorDomain::RegisteredEditorDomain == nullptr);
				FEditorDomain::RegisteredEditorDomain = new FEditorDomain();
				return FEditorDomain::RegisteredEditorDomain;
			}
		}
		return nullptr;
	}
} GRegisterAsPackageResourceManager;

FEditorDomain* FEditorDomain::RegisteredEditorDomain = nullptr;

FEditorDomain::FEditorDomain()
{
	Locks = TRefCountPtr<FLocks>(new FLocks(*this));
	Workspace.Reset(MakePackageResourceManagerFile());
	GConfig->GetBool(TEXT("CookSettings"), TEXT("EditorDomainExternalSave"), bExternalSave, GEditorIni);
	if (bExternalSave)
	{
		SaveClient.Reset(new FEditorDomainSaveClient());
	}
	AssetRegistry = IAssetRegistry::Get();
	// We require calling SearchAllAssets, because we rely on being able to call WaitOnAsset
	// without needing to call ScanPathsSynchronous
	AssetRegistry->SearchAllAssets(false /* bSynchronousSearch */);

	bEditorDomainReadEnabled = !FParse::Param(FCommandLine::Get(), TEXT("noeditordomainread"));

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FEditorDomain::OnPostEngineInit);
}

FEditorDomain::~FEditorDomain()
{
	FScopeLock ScopeLock(&Locks->Lock);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	Locks->Owner = nullptr;
	AssetRegistry = nullptr;
	Workspace.Reset();

	if (RegisteredEditorDomain == this)
	{
		RegisteredEditorDomain = nullptr;
	}
}

FEditorDomain* FEditorDomain::Get()
{
	return RegisteredEditorDomain;
}

bool FEditorDomain::SupportsLocalOnlyPaths()
{
	// Local Only paths are supported by falling back to the WorkspaceDomain
	return true;
}

bool FEditorDomain::SupportsPackageOnlyPaths()
{
	return true;
}

bool FEditorDomain::DoesPackageExist(const FPackagePath& PackagePath, EPackageSegment PackageSegment,
	FPackagePath* OutUpdatedPath)
{
	return Workspace->DoesPackageExist(PackagePath, PackageSegment, OutUpdatedPath);
}

FEditorDomain::FLocks::FLocks(FEditorDomain& InOwner)
	:Owner(&InOwner)
{
}

bool FEditorDomain::TryFindOrAddPackageSource(const FPackagePath& PackagePath, TRefCountPtr<FPackageSource>& OutSource)
{
	// Called within Locks.Lock
	using namespace UE::EditorDomain;

	// EDITOR_DOMAIN_TODO: Need to delete entries from PackageSources when the assetregistry reports the package is
	// resaved on disk.
	FName PackageName = PackagePath.GetPackageFName();
	if (PackageName.IsNone())
	{
		OutSource.SafeRelease();
		return false;
	}

	TRefCountPtr<FPackageSource>& PackageSource = PackageSources.FindOrAdd(PackageName);
	if (PackageSource)
	{
		OutSource = PackageSource;
		return true;
	}

	FPackageDigest PackageDigest;
	EPackageDigestResult Result = GetPackageDigest(*AssetRegistry, PackageName, PackageDigest);
	switch (Result)
	{
	case EPackageDigestResult::Success:
		PackageSource = new FPackageSource();
		PackageSource->Digest = PackageDigest;
		OutSource = PackageSource;
		if (!bEditorDomainReadEnabled)
		{
			PackageSource->Source = EPackageSource::Workspace;
		}
		return true;
	case EPackageDigestResult::FileDoesNotExist:
		OutSource.SafeRelease();
		// Remove the entry in PackageSources that we added; we added it to avoid a double lookup for new packages,
		// but for non-existent packages we want it not to be there to avoid wasting memory on it
		PackageSources.Remove(PackageName);
		return false;
	case EPackageDigestResult::WrongThread:
		PackageSource = new FPackageSource();
		PackageSource->Source = EPackageSource::Workspace;
		OutSource = PackageSource;
		return true;
	default:
		check(false);
		return false;
	}
}

TRefCountPtr<FEditorDomain::FPackageSource> FEditorDomain::FindPackageSource(const FPackagePath& PackagePath)
{
	// Called within Locks.Lock
	using namespace UE::EditorDomain;

	FName PackageName = PackagePath.GetPackageFName();
	if (!PackageName.IsNone())
	{
		TRefCountPtr<FPackageSource>* PackageSource = PackageSources.Find(PackageName);
		if (PackageSource)
		{
			return *PackageSource;
		}
	}

	return TRefCountPtr<FPackageSource>();
}

void FEditorDomain::MarkNeedsLoadFromWorkspace(const FPackagePath& PackagePath, TRefCountPtr<FPackageSource>& PackageSource)
{
	PackageSource->Source = FEditorDomain::EPackageSource::Workspace;
	if (bExternalSave)
	{
		SaveClient->RequestSave(PackagePath);
	}
	// Otherwise, we will note the need for save in OnEndLoad

}

int64 FEditorDomain::FileSize(const FPackagePath& PackagePath, EPackageSegment PackageSegment,
	FPackagePath* OutUpdatedPath)
{
	using namespace UE::EditorDomain;

	if (PackageSegment != EPackageSegment::Header)
	{
		return Workspace->FileSize(PackagePath, PackageSegment, OutUpdatedPath);
	}

	UE::DerivedData::FRequest Request;
	int64 FileSize = -1;
	{
		FScopeLock ScopeLock(&Locks->Lock);
		TRefCountPtr<FPackageSource> PackageSource;
		if (!TryFindOrAddPackageSource(PackagePath, PackageSource) || PackageSource->Source == EPackageSource::Workspace)
		{
			return Workspace->FileSize(PackagePath, PackageSegment, OutUpdatedPath);
		}

		UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();
		auto MetaDataGetComplete =
			[&FileSize, &PackageSource, &PackagePath, PackageSegment, Locks=this->Locks, OutUpdatedPath]
			(UE::DerivedData::FCacheGetCompleteParams&& Params)
		{
			FScopeLock ScopeLock(&Locks->Lock);
			if ((PackageSource->Source == FEditorDomain::EPackageSource::Undecided || PackageSource->Source == FEditorDomain::EPackageSource::Editor) &&
				Params.Status == UE::DerivedData::EStatus::Ok)
			{
				const FCbObject& MetaData = Params.Record.GetMeta();
				FileSize = MetaData["FileSize"].AsInt64();
				PackageSource->Source = EPackageSource::Editor;
			}
			else
			{
				checkf(PackageSource->Source == EPackageSource::Undecided || PackageSource->Source == EPackageSource::Workspace,
					TEXT("%s was previously loaded from the EditorDomain but now is unavailable."),
					*PackagePath.GetDebugName());
				if (Locks->Owner)
				{
					FEditorDomain& EditorDomain = *Locks->Owner;
					EditorDomain.MarkNeedsLoadFromWorkspace(PackagePath, PackageSource);
					FileSize = EditorDomain.Workspace->FileSize(PackagePath, PackageSegment, OutUpdatedPath);
				}
				else
				{
					UE_LOG(LogEditorDomain, Warning, TEXT("%s size read after EditorDomain shutdown. Returning -1."),
						*PackagePath.GetDebugName());
					FileSize = -1;
				}
			}
		};
		Request = RequestEditorDomainPackage(PackagePath, PackageSource->Digest,
			UE::DerivedData::EPriority::Highest, MoveTemp(MetaDataGetComplete));
	}
	Request.Wait();
	return FileSize;
}

FOpenPackageResult FEditorDomain::OpenReadPackage(const FPackagePath& PackagePath, EPackageSegment PackageSegment,
	FPackagePath* OutUpdatedPath)
{
	using namespace UE::EditorDomain;

	FScopeLock ScopeLock(&Locks->Lock);
	if (PackageSegment != EPackageSegment::Header)
	{
		return Workspace->OpenReadPackage(PackagePath, PackageSegment, OutUpdatedPath);
	}
	TRefCountPtr<FPackageSource> PackageSource;
	if (!TryFindOrAddPackageSource(PackagePath, PackageSource) ||	(PackageSource->Source == EPackageSource::Workspace))
	{
		return Workspace->OpenReadPackage(PackagePath, PackageSegment, OutUpdatedPath);
	}

	FEditorDomainReadArchive* Result = new FEditorDomainReadArchive(Locks, PackagePath, PackageSource);
	const FPackageDigest PackageSourceDigest = PackageSource->Digest;
	const bool bHasEditorSource = (PackageSource->Source == EPackageSource::Editor);

	// Unlock before requesting the package because the completion callback takes the lock.
	ScopeLock.Unlock();

	UE::DerivedData::FRequest Request = RequestEditorDomainPackage(PackagePath, PackageSourceDigest,
		UE::DerivedData::EPriority::Normal,
		[Result](UE::DerivedData::FCacheGetCompleteParams&& Params)
		{
			// Note that ~FEditorDomainReadArchive waits for this callback to be called, so Result cannot dangle
			Result->OnCacheRequestComplete(MoveTemp(Params));
		});
	Result->SetRequest(Request);

	if (OutUpdatedPath)
	{
		*OutUpdatedPath = PackagePath;
	}

	// EDITOR_DOMAIN_TODO: Reading GetPackageFormat forces us to wait for the cache response
	// We should read just the metadata for the package so we don't have to block here on the transfer of the bytes.
	const EPackageFormat Format = bHasEditorSource ? EPackageFormat::Binary : Result->GetPackageFormat();
	return FOpenPackageResult{ TUniquePtr<FArchive>(Result), Format };
}

IAsyncReadFileHandle* FEditorDomain::OpenAsyncReadPackage(const FPackagePath& PackagePath, EPackageSegment PackageSegment)
{
	using namespace UE::EditorDomain;

	FScopeLock ScopeLock(&Locks->Lock);
	if (PackageSegment != EPackageSegment::Header)
	{
		return Workspace->OpenAsyncReadPackage(PackagePath, PackageSegment);
	}

	TRefCountPtr<FPackageSource> PackageSource;
	if (!TryFindOrAddPackageSource(PackagePath, PackageSource) ||
		(PackageSource->Source == EPackageSource::Workspace))
	{
		return Workspace->OpenAsyncReadPackage(PackagePath, PackageSegment);
	}

	FEditorDomainAsyncReadFileHandle* Result = new FEditorDomainAsyncReadFileHandle(Locks, PackagePath, PackageSource);
	UE::DerivedData::FRequest Request = RequestEditorDomainPackage(PackagePath, PackageSource->Digest,
		UE::DerivedData::EPriority::Normal,
		[Result](UE::DerivedData::FCacheGetCompleteParams&& Params)
		{
			// Note that ~FEditorDomainAsyncReadFileHandle waits for this callback to be called, so Result cannot dangle
			Result->OnCacheRequestComplete(MoveTemp(Params));
		});
	Result->SetRequest(Request);

	return Result;
}

IMappedFileHandle* FEditorDomain::OpenMappedHandleToPackage(const FPackagePath& PackagePath,
	EPackageSegment PackageSegment, FPackagePath* OutUpdatedPath)
{
	// No need to implement this runtime feature in the editor domain.
	return nullptr;
}

bool FEditorDomain::TryMatchCaseOnDisk(const FPackagePath& PackagePath, FPackagePath* OutNormalizedPath)
{
	return Workspace->TryMatchCaseOnDisk(PackagePath, OutNormalizedPath);
}

TUniquePtr<FArchive> FEditorDomain::OpenReadExternalResource(EPackageExternalResource ResourceType, FStringView Identifier)
{
	return Workspace->OpenReadExternalResource(ResourceType, Identifier);
}

bool FEditorDomain::DoesExternalResourceExist(EPackageExternalResource ResourceType, FStringView Identifier)
{
	return Workspace->DoesExternalResourceExist(ResourceType, Identifier);
}

IAsyncReadFileHandle* FEditorDomain::OpenAsyncReadExternalResource(
	EPackageExternalResource ResourceType, FStringView Identifier)
{
	return Workspace->OpenAsyncReadExternalResource(ResourceType, Identifier);
}

void FEditorDomain::FindPackagesRecursive(TArray<TPair<FPackagePath, EPackageSegment>>& OutPackages,
	FStringView PackageMount, FStringView FileMount, FStringView RootRelPath, FStringView BasenameWildcard)
{
	return Workspace->FindPackagesRecursive(OutPackages, PackageMount, FileMount, RootRelPath, BasenameWildcard);
}

void FEditorDomain::IteratePackagesInPath(FStringView PackageMount, FStringView FileMount, FStringView RootRelPath,
	FPackageSegmentVisitor Callback)
{
	Workspace->IteratePackagesInPath(PackageMount, FileMount, RootRelPath, Callback);

}
void FEditorDomain::IteratePackagesInLocalOnlyDirectory(FStringView RootDir, FPackageSegmentVisitor Callback)
{
	Workspace->IteratePackagesInLocalOnlyDirectory(RootDir, Callback);
}

void FEditorDomain::IteratePackagesStatInPath(FStringView PackageMount, FStringView FileMount,
	FStringView RootRelPath, FPackageSegmentStatVisitor Callback)
{
	Workspace->IteratePackagesStatInPath(PackageMount, FileMount, RootRelPath, Callback);
}

void FEditorDomain::IteratePackagesStatInLocalOnlyDirectory(FStringView RootDir, FPackageSegmentStatVisitor Callback)
{
	Workspace->IteratePackagesStatInLocalOnlyDirectory(RootDir, Callback);
}

void FEditorDomain::Tick(float DeltaTime)
{
	if (bExternalSave)
	{
		SaveClient->Tick(DeltaTime);
	}
}

void FEditorDomain::OnEndLoad(TConstArrayView<UPackage*> LoadedPackages)
{
	if (bExternalSave)
	{
		return;
	}
	TArray<UPackage*> PackagesToSave;
	{
		FScopeLock ScopeLock(&Locks->Lock);
		if (!bHasPassedPostEngineInit)
		{
			return;
		}
		PackagesToSave.Reserve(LoadedPackages.Num());
		for (UPackage* Package : LoadedPackages)
		{
			PackagesToSave.Add(Package);
		}
		FilterKeepPackagesToSave(PackagesToSave);
	}

	for (UPackage* Package : PackagesToSave)
	{
		if (!UE::EditorDomain::TrySavePackage(Package))
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("Could not save package %s into EditorDomain."), *Package->GetName());
		}
	}
}

void FEditorDomain::OnPostEngineInit()
{
	TArray<UPackage*> PackagesToSave;
	{
		FScopeLock ScopeLock(&Locks->Lock);
		bHasPassedPostEngineInit = true;
		if (bExternalSave)
		{
			return;
		}

		FString PackageName;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			Package->GetName(PackageName);
			if (Package->IsFullyLoaded() && !FPackageName::IsScriptPackage(PackageName))
			{
				PackagesToSave.Add(Package);
			}
		}
		FilterKeepPackagesToSave(PackagesToSave);
	}

	for (UPackage* Package : PackagesToSave)
	{
		if (!UE::EditorDomain::TrySavePackage(Package))
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("Could not save package %s into EditorDomain."), *Package->GetName());
		}
	}
}

void FEditorDomain::FilterKeepPackagesToSave(TArray<UPackage*>& InOutPackagesToSave)
{
	FPackagePath PackagePath;
	for (int32 Index = 0; Index < InOutPackagesToSave.Num(); )
	{
		UPackage* Package = InOutPackagesToSave[Index];
		bool bKeep = false;
		if (FPackagePath::TryFromPackageName(Package->GetFName(), PackagePath))
		{
			TRefCountPtr<FPackageSource> PackageSource = FindPackageSource(PackagePath);
			if (PackageSource && PackageSource->NeedsEditorDomainSave())
			{
				PackageSource->bHasSaved = true;
				bKeep = true;
			}
		}
		if (bKeep)
		{
			++Index;
		}
		else
		{
			InOutPackagesToSave.RemoveAtSwap(Index);
		}
	}
}

