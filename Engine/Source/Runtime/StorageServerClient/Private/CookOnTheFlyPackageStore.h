// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_COTF

#include "IO/PackageStore.h"
#include "CookOnTheFly.h"

class FCookOnTheFlyPackageStore final
	: public FPackageStoreBase
{
public:
	struct FEntryInfo
	{
		EPackageStoreEntryStatus Status = EPackageStoreEntryStatus::None;
		int32 EntryIndex = INDEX_NONE;
	};

	struct FPackageStats
	{
		TAtomic<uint32> Cooked{ 0 };
		TAtomic<uint32> Failed{ 0 };
	};

	FCookOnTheFlyPackageStore(UE::Cook::ICookOnTheFlyServerConnection& InCookOnTheFlyServerConnection);

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
	virtual EPackageStoreEntryStatus GetPackageStoreEntry(FPackageId PackageId, FPackageStoreEntry& OutPackageStoreEntry) override;
	
	virtual bool GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId) override
	{
		return false;
	}
	
private:
	EPackageStoreEntryStatus CreatePackageStoreEntry(const FEntryInfo& EntryInfo, FPackageStoreEntry& OutPackageStoreEntry);
	void AddPackages(TArray<FPackageStoreEntryResource> Entries, TArray<FPackageId> FailedPackageIds);
	void OnCookOnTheFlyMessage(const UE::Cook::FCookOnTheFlyMessage& Message);
	void CheckActivity();

	UE::Cook::ICookOnTheFlyServerConnection& CookOnTheFlyServerConnection;
	FCriticalSection CriticalSection;
	TMap<FPackageId, FEntryInfo> PackageIdToEntryInfo;
	TChunkedArray<FPackageStoreEntryResource> PackageEntries;
	FPackageStats PackageStats;

	const double MaxInactivityTime = 20;
	const double TimeBetweenWarning = 10;
	double LastClientActivtyTime = 0;
	double LastServerActivtyTime = 0;
	double LastWarningTime = 0;
};

#endif // WITH_COTF
