// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/PackageStore.h"

#if !UE_BUILD_SHIPPING

class FStorageServerConnection;
struct FFilePackageStoreEntry;

class FStorageServerPackageStore
	: public FPackageStoreBase
{
public:
	FStorageServerPackageStore(FStorageServerConnection& Connection);
	virtual ~FStorageServerPackageStore() = default;

	virtual void Initialize() override
	{
	}

	virtual void Lock() override
	{
	}

	virtual void Unlock() override
	{
	}

	virtual bool DoesPackageExist(FPackageId PackageId) override;
	virtual EPackageStoreEntryStatus GetPackageStoreEntry(FPackageId PackageIde, FPackageStoreEntry& OutPackageStoreEntry) override;
	
	virtual bool GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId) override
	{
		return false;
	}

private:
	TArray<uint8> StoreEntriesData;
	TMap<FPackageId, const FFilePackageStoreEntry*> StoreEntriesMap;
};

#endif