// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/PackageId.h"
#include "IO/PackageStore.h"

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
	virtual ~FFilePackageStore() = default;

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
	};

	void Update();

	FRWLock EntriesLock;
	FCriticalSection UpdateLock;
	TArray<FMountedContainer> MountedContainers;
	TMap<FPackageId, const FFilePackageStoreEntry*> StoreEntriesMap;
	TMap<FPackageId, TTuple<FName, FPackageId>> RedirectsPackageMap;
	TMap<FPackageId, FName> LocalizedPackages;
	bool bNeedsUpdate = false;

	static thread_local bool bIsLockedOnThread;
};
