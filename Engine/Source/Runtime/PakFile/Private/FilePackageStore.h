// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/PackageId.h"
#include "IO/PackageStore.h"
#include "Misc/PackagePath.h"

struct FIoContainerHeader;
struct FFilePackageStoreEntry;

/*
 * File/container based package store.
 */
class FFilePackageStore
	: public FPackageStoreBase
{
public:
	FFilePackageStore();
	virtual ~FFilePackageStore();

	virtual void Initialize() override;
	virtual void Lock() override;
	virtual void Unlock() override;
	virtual bool DoesPackageExist(FPackageId PackageId) override;
	virtual EPackageStoreEntryStatus GetPackageStoreEntry(FPackageId PackageId, FPackageStoreEntry& OutPackageStoreEntry) override;
	virtual bool GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId) override;

	void Mount(const FIoContainerHeader* ContainerHeader, uint32 Order);
	void Unmount(const FIoContainerHeader* ContainerHeader);

private:
	struct FMountedContainer
	{
		const FIoContainerHeader* ContainerHeader;
		uint32 Order;
		uint32 Sequence;
	};

#if WITH_EDITOR
	struct FUncookedPackage
	{
		FName PackageName;
		EPackageExtension HeaderExtension;
	};
#endif //if WITH_EDITOR

	void Update();
#if WITH_EDITOR
	uint64 AddUncookedPackagesFromRoot(const FString& RootPath);
	uint64 RemoveUncookedPackagesFromRoot(const TSet<FString>& RootPath);
#endif //if WITH_EDITOR

	FRWLock EntriesLock;
	FCriticalSection UpdateLock;
	TArray<FMountedContainer> MountedContainers;
	TAtomic<uint32> NextSequence{ 0 };
	TMap<FPackageId, const FFilePackageStoreEntry*> StoreEntriesMap;
	TMap<FPackageId, TTuple<FName, FPackageId>> RedirectsPackageMap;
	TMap<FPackageId, FName> LocalizedPackages;
	bool bNeedsUpdate = false;

#if WITH_EDITOR
	FDelegateHandle OnContentPathMountedDelegateHandle;
	FDelegateHandle OnContentPathDismountedDelegateHandle;
	FCriticalSection UncookedPackageRootsLock;
	TSet<FString> PendingAddUncookedPackageRoots;
	TSet<FString> PendingRemoveUncookedPackageRoots;
	TMap<FPackageId, FUncookedPackage> UncookedPackagesMap;
	TMap<FPackageId, const FFilePackageStoreEntry*> OptionalSegmentStoreEntriesMap;
#endif //if WITH_EDITOR

	static thread_local int32 LockedOnThreadCount;
};
