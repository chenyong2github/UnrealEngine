// Copyright Epic Games, Inc. All Rights Reserved.
#include "FilePackageStore.h"
#include "IO/IoContainerId.h"
#include "Misc/CommandLine.h"
#include "IO/IoContainerHeader.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Internationalization/PackageLocalizationManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/PackageName.h"

//PRAGMA_DISABLE_OPTIMIZATION

DEFINE_LOG_CATEGORY_STATIC(LogFilePackageStore, Log, All);

thread_local int32 FFilePackageStore::LockedOnThreadCount = 0;

FFilePackageStore::FFilePackageStore()
{
#if WITH_EDITOR
	OnContentPathMountedDelegateHandle = FPackageName::OnContentPathMounted().AddLambda([this](const FString& InAssetPath, const FString& InFilesystemPath)
		{
			{
				FScopeLock _(&UncookedPackageRootsLock);
				PendingUncookedPackageRoots.Add(InFilesystemPath);
			}
			{
				FWriteScopeLock _(EntriesLock);
				bNeedsUpdate = true;
			}
		});
#endif
}

FFilePackageStore::~FFilePackageStore()
{
#if WITH_EDITOR
	FPackageName::OnContentPathMounted().Remove(OnContentPathMountedDelegateHandle);
#endif
}

void FFilePackageStore::Initialize()
{
#if WITH_EDITOR
	TArray<FString> RootPaths;
	FPackageName::QueryRootContentPaths(RootPaths);
	{
		FScopeLock _(&UncookedPackageRootsLock);
		PendingUncookedPackageRoots.Append(RootPaths);
	}
	{
		FWriteScopeLock _(EntriesLock);
		bNeedsUpdate = true;
	}
#endif
}

void FFilePackageStore::Lock()
{
	if (!LockedOnThreadCount)
	{
		EntriesLock.ReadLock();
		if (bNeedsUpdate)
		{
			Update();
		}
	}
	++LockedOnThreadCount;
}

void FFilePackageStore::Unlock()
{
	check(LockedOnThreadCount > 0);
	if (--LockedOnThreadCount == 0)
	{
		EntriesLock.ReadUnlock();
	}
}

bool FFilePackageStore::DoesPackageExist(FPackageId PackageId)
{
	check(LockedOnThreadCount);
	return PackageId.IsValid() && StoreEntriesMap.Contains(PackageId);
}

EPackageStoreEntryStatus FFilePackageStore::GetPackageStoreEntry(FPackageId PackageId, FPackageStoreEntry& OutPackageStoreEntry)
{
	check(LockedOnThreadCount);
#if WITH_EDITOR
	const FUncookedPackage* FindUncookedPackage = UncookedPackagesMap.Find(PackageId);
	if (FindUncookedPackage)
	{
		OutPackageStoreEntry.UncookedPackageName = FindUncookedPackage->PackageName;
		OutPackageStoreEntry.UncookedPackageHeaderExtension = static_cast<uint8>(FindUncookedPackage->HeaderExtension);
		return EPackageStoreEntryStatus::Ok;
	}
#endif
	const FFilePackageStoreEntry* FindEntry = StoreEntriesMap.FindRef(PackageId);
	if (FindEntry)
	{
		OutPackageStoreEntry.ExportInfo.ExportCount = FindEntry->ExportCount;
		OutPackageStoreEntry.ExportInfo.ExportBundleCount = FindEntry->ExportBundleCount;
		OutPackageStoreEntry.ImportedPackageIds = MakeArrayView(FindEntry->ImportedPackages.Data(), FindEntry->ImportedPackages.Num());
		OutPackageStoreEntry.ShaderMapHashes = MakeArrayView(FindEntry->ShaderMapHashes.Data(), FindEntry->ShaderMapHashes.Num());
		return EPackageStoreEntryStatus::Ok;
	}
	return EPackageStoreEntryStatus::Missing;
}

bool FFilePackageStore::GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId)
{
	check(LockedOnThreadCount);
	TTuple<FName, FPackageId>* FindRedirect = RedirectsPackageMap.Find(PackageId);
	if (FindRedirect)
	{
		OutSourcePackageName = FindRedirect->Get<0>();
		OutRedirectedToPackageId = FindRedirect->Get<1>();
		UE_LOG(LogFilePackageStore, Verbose, TEXT("Redirecting from %s to 0x%llx"), *OutSourcePackageName.ToString(), OutRedirectedToPackageId.Value());
		return true;
	}
	
	const FName* FindLocalizedPackageSourceName = LocalizedPackages.Find(PackageId);
	if (FindLocalizedPackageSourceName)
	{
		FName LocalizedPackageName = FPackageLocalizationManager::Get().FindLocalizedPackageName(*FindLocalizedPackageSourceName);
		if (!LocalizedPackageName.IsNone())
		{
			FPackageId LocalizedPackageId = FPackageId::FromName(LocalizedPackageName);
			if (StoreEntriesMap.Find(LocalizedPackageId))
			{
				OutSourcePackageName = *FindLocalizedPackageSourceName;
				OutRedirectedToPackageId = LocalizedPackageId;
				UE_LOG(LogFilePackageStore, Verbose, TEXT("Redirecting from localized package %s to 0x%llx"), *OutSourcePackageName.ToString(), OutRedirectedToPackageId.Value());
				return true;
			}
		}
	}
	return false;
}

void FFilePackageStore::Mount(const FIoContainerHeader* ContainerHeader, uint32 Order)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	FWriteScopeLock _(EntriesLock);
	MountedContainers.Add({ ContainerHeader, Order });
	Algo::Sort(MountedContainers, [](const FMountedContainer& A, const FMountedContainer& B)
		{
			return A.Order < B.Order;
		});
	bNeedsUpdate = true;
}

void FFilePackageStore::Unmount(const FIoContainerHeader* ContainerHeader)
{
	FWriteScopeLock _(EntriesLock);
	for (auto It = MountedContainers.CreateIterator(); It; ++It)
	{
		if (It->ContainerHeader == ContainerHeader)
		{
			It.RemoveCurrent();
			bNeedsUpdate = true;
			return;
		}
	}
}

void FFilePackageStore::Update()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateFilePackageStore);

	FScopeLock Lock(&UpdateLock);
	if (!bNeedsUpdate)
	{
		return;
	}

	StoreEntriesMap.Empty();
	LocalizedPackages.Empty();
	RedirectsPackageMap.Empty();

	uint32 TotalPackageCount = 0;
	for (const FMountedContainer& MountedContainer : MountedContainers)
	{
		TotalPackageCount += MountedContainer.ContainerHeader->PackageCount;
	}

	StoreEntriesMap.Reserve(TotalPackageCount);
	for (const FMountedContainer& MountedContainer : MountedContainers)
	{
		const FIoContainerHeader* ContainerHeader = MountedContainer.ContainerHeader;
		TArrayView<const FFilePackageStoreEntry> ContainerStoreEntries(reinterpret_cast<const FFilePackageStoreEntry*>(ContainerHeader->StoreEntries.GetData()), ContainerHeader->PackageCount);
		int32 Index = 0;
		for (const FFilePackageStoreEntry& StoreEntry : ContainerStoreEntries)
		{
			const FPackageId& PackageId = ContainerHeader->PackageIds[Index];
			check(PackageId.IsValid());
			StoreEntriesMap.FindOrAdd(PackageId, &StoreEntry);
			++Index;
		}

		for (const FIoContainerHeaderLocalizedPackage& LocalizedPackage : ContainerHeader->LocalizedPackages)
		{
			FName& LocalizedPackageSourceName = LocalizedPackages.FindOrAdd(LocalizedPackage.SourcePackageId);
			if (LocalizedPackageSourceName.IsNone())
			{
				FNameEntryId NameEntry = ContainerHeader->RedirectsNameMap[LocalizedPackage.SourcePackageName.GetIndex()];
				LocalizedPackageSourceName = FName::CreateFromDisplayId(NameEntry, LocalizedPackage.SourcePackageName.GetNumber());
			}
		}

		for (const FIoContainerHeaderPackageRedirect& Redirect : ContainerHeader->PackageRedirects)
		{
			FNameEntryId NameEntry = ContainerHeader->RedirectsNameMap[Redirect.SourcePackageName.GetIndex()];
			FName SourcePackageName = FName::CreateFromDisplayId(NameEntry, Redirect.SourcePackageName.GetNumber());
			RedirectsPackageMap.Emplace(Redirect.SourcePackageId, MakeTuple(SourcePackageName, Redirect.TargetPackageId));
		}
	}

#if WITH_EDITOR
	{
		FScopeLock _(&UncookedPackageRootsLock);
		uint64 TotalAddedCount = 0;
		if (!PendingUncookedPackageRoots.IsEmpty())
		{
			UE_LOG(LogFilePackageStore, Display, TEXT("Searching for uncooked packages in %d new roots..."), PendingUncookedPackageRoots.Num());
			for (const FString& RootPath : PendingUncookedPackageRoots)
			{
				TotalAddedCount += AddUncookedPackagesFromRoot(RootPath);
			}
			PendingUncookedPackageRoots.Empty();
			UE_LOG(LogFilePackageStore, Display, TEXT("Found %lld uncooked packages"), TotalAddedCount);
		}
	}
#endif

	bNeedsUpdate = false;
}

#if WITH_EDITOR
uint64 FFilePackageStore::AddUncookedPackagesFromRoot(const FString& RootPath)
{
	uint64 TotalAddedCount = 0;
	FPackageName::IteratePackagesInDirectory(RootPath, [this, &RootPath, &TotalAddedCount](const TCHAR* InPackageFileName) -> bool
		{
			FPackagePath PackagePath = FPackagePath::FromLocalPath(InPackageFileName);
			FName PackageName = PackagePath.GetPackageFName();
			if (!PackageName.IsNone())
			{
				FPackageId PackageId = FPackageId::FromName(PackageName);
				FUncookedPackage& UncookedPackage = UncookedPackagesMap.FindOrAdd(PackageId);
				UncookedPackage.PackageName = PackageName;
				UncookedPackage.HeaderExtension = PackagePath.GetHeaderExtension();
				++TotalAddedCount;
			}
			return true;
		});
	return TotalAddedCount;
}
#endif

//PRAGMA_ENABLE_OPTIMIZATION