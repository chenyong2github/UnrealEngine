// Copyright Epic Games, Inc. All Rights Reserved.
#include "FilePackageStore.h"
#include "IO/IoContainerId.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"
#include "IO/IoContainerHeader.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Internationalization/PackageLocalizationManager.h"
#include "Misc/ScopeLock.h"

//PRAGMA_DISABLE_OPTIMIZATION

thread_local bool FFilePackageStore::bIsLockedOnThread = false;

FFilePackageStore::FFilePackageStore()
{
}

void FFilePackageStore::Initialize()
{
	FInternationalization::Get().OnCultureChanged().AddLambda([this]
	{
		FWriteScopeLock Lock(EntriesLock);
		bNeedsUpdate = true;
	});
}

void FFilePackageStore::Lock()
{
	EntriesLock.ReadLock();
	if (bNeedsUpdate)
	{
		Update();
	}
	bIsLockedOnThread = true;
}

void FFilePackageStore::Unlock()
{
	check(bIsLockedOnThread);
	EntriesLock.ReadUnlock();
	bIsLockedOnThread = false;
}

bool FFilePackageStore::DoesPackageExist(FPackageId PackageId)
{
	check(bIsLockedOnThread);
	return PackageId.IsValid() && StoreEntriesMap.Contains(PackageId);
}

EPackageStoreEntryStatus FFilePackageStore::GetPackageStoreEntry(FPackageId PackageId, FPackageStoreEntry& OutPackageStoreEntry)
{
	check(bIsLockedOnThread);
	const FFilePackageStoreEntry* FindEntry = StoreEntriesMap.FindRef(PackageId);
	if (FindEntry)
	{
		OutPackageStoreEntry.ExportInfo.ExportCount = FindEntry->ExportCount;
		OutPackageStoreEntry.ExportInfo.ExportBundleCount = FindEntry->ExportBundleCount;
		OutPackageStoreEntry.ImportedPackageIds = MakeArrayView(FindEntry->ImportedPackages.Data(), FindEntry->ImportedPackages.Num());
		OutPackageStoreEntry.ShaderMapHashes = MakeArrayView(FindEntry->ShaderMapHashes.Data(), FindEntry->ShaderMapHashes.Num());
		return EPackageStoreEntryStatus::Ok;
	}
	else
	{
		return EPackageStoreEntryStatus::Missing;
	}
}

bool FFilePackageStore::GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId)
{
	check(bIsLockedOnThread);
	TTuple<FName, FPackageId>* FindRedirect = RedirectsPackageMap.Find(PackageId);
	if (FindRedirect)
	{
		OutSourcePackageName = FindRedirect->Get<0>();
		OutRedirectedToPackageId = FindRedirect->Get<1>();

		UE_LOG(LogCore, Display, TEXT("Redirecting from %s to 0x%llx"), *OutSourcePackageName.ToString(), OutRedirectedToPackageId.Value());

		return true;
	}
	else
	{
		return false;
	}
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
	RedirectsPackageMap.Empty();

	uint32 TotalPackageCount = 0;
	for (const FMountedContainer& MountedContainer : MountedContainers)
	{
		TotalPackageCount += MountedContainer.ContainerHeader->PackageCount;
	}

	StoreEntriesMap.Reserve(TotalPackageCount);
	TMap<FPackageId, FName> AllLocalizedPackages;
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
			FName& SourcePackageName = AllLocalizedPackages.FindOrAdd(LocalizedPackage.SourcePackageId);
			if (SourcePackageName.IsNone())
			{
				FNameEntryId NameEntry = ContainerHeader->RedirectsNameMap[LocalizedPackage.SourcePackageName.GetIndex()];
				SourcePackageName = FName::CreateFromDisplayId(NameEntry, LocalizedPackage.SourcePackageName.GetNumber());
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreRedirects);
			for (const FIoContainerHeaderPackageRedirect& Redirect : ContainerHeader->PackageRedirects)
			{
				FNameEntryId NameEntry = ContainerHeader->RedirectsNameMap[Redirect.SourcePackageName.GetIndex()];
				FName SourcePackageName = FName::CreateFromDisplayId(NameEntry, Redirect.SourcePackageName.GetNumber());
				RedirectsPackageMap.Emplace(Redirect.SourcePackageId, MakeTuple(SourcePackageName, Redirect.TargetPackageId));
			}
		}
	}

	for (const auto& KV : AllLocalizedPackages)
	{
		const FPackageId& SourcePackageId = KV.Key;
		const FName& SourcePackageName = KV.Value;
		FName LocalizedPackageName = FPackageLocalizationManager::Get().FindLocalizedPackageName(SourcePackageName);
		if (!LocalizedPackageName.IsNone())
		{
			FPackageId RedirectedToPackageId = FPackageId::FromName(LocalizedPackageName);
			if (StoreEntriesMap.Find(RedirectedToPackageId))
			{
				RedirectsPackageMap.Emplace(SourcePackageId, MakeTuple(SourcePackageName, RedirectedToPackageId));
			}
		}
	}
	
	bNeedsUpdate = false;
}

//PRAGMA_ENABLE_OPTIMIZATION