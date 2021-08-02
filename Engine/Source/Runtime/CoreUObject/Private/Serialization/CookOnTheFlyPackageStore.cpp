// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CookOnTheFlyPackageStore.h"

#if WITH_COTF

#include "CookOnTheFly.h"
#include "HAL/PlatformTime.h"
#include "Containers/ChunkedArray.h"
#include "Serialization/MemoryReader.h"

namespace UE { namespace PackageStore { namespace Messaging
{

FArchive& operator<<(FArchive& Ar, FPackageStoreData& PackageStoreData)
{
	Ar << PackageStoreData.CookedPackages;
	Ar << PackageStoreData.FailedPackages;
	Ar << PackageStoreData.TotalCookedPackages;
	Ar << PackageStoreData.TotalFailedPackages;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FCookPackageRequest& Request)
{
	Ar << Request.PackageName;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FCookPackageResponse& Response)
{
	uint32 Status = static_cast<uint32>(Response.Status);
	Ar << Status;

	if (Ar.IsLoading())
	{
		Response.Status = static_cast<EPackageStoreEntryStatus>(Status);
	}

	return Ar;
}

}}} // namesapce UE::PackageStore::Messaging

class FCookOnTheFlyPackageStore final
	: public IPackageStore
{
public:
	FCookOnTheFlyPackageStore(
		FIoDispatcher& InIoDispatcher,
		UE::Cook::ICookOnTheFlyServerConnection& InCookOnTheFlyServerConnection,
		TFunction<void()>&& InEntriesAddedCallback)
			: IoDispatcher(InIoDispatcher)
			, CookOnTheFlyServerConnection(InCookOnTheFlyServerConnection)
			, EntriesAddedCallback(InEntriesAddedCallback)
	{
		// Index zero is invalid
		PackageEntries.Add();
		CookOnTheFlyServerConnection.OnMessage().AddRaw(this, &FCookOnTheFlyPackageStore::OnCookOnTheFlyMessage);
	}

	~FCookOnTheFlyPackageStore()
	{
	}

private:
	virtual void Initialize() override
	{
		using namespace UE::Cook;
		using namespace UE::PackageStore::Messaging;

		FCookOnTheFlyRequest Request(ECookOnTheFlyMessage::GetCookedPackages);
		FCookOnTheFlyResponse Response = CookOnTheFlyServerConnection.SendRequest(Request).Get();

		if (Response.IsOk())
		{
			FGetCookedPackagesResponse GetCookedPackagesResponse = Response.GetBodyAs<FGetCookedPackagesResponse>();
			FPackageStoreData& PackageStoreData = GetCookedPackagesResponse.PackageStoreData;

			UE_LOG(LogCookOnTheFly, Log, TEXT("Got '%d' cooked and '%d' failed packages from server"),
				PackageStoreData.CookedPackages.Num(), PackageStoreData.FailedPackages.Num());

			AddPackages(MoveTemp(PackageStoreData.CookedPackages), MoveTemp(PackageStoreData.FailedPackages));

			LastServerActivtyTime = FPlatformTime::Seconds();
		}
		else
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to send '%s' request"), LexToString(Request.GetHeader().MessageType));
		}
	}

	virtual bool DoesPackageExist(FPackageId PackageId) override
	{
		FScopeLock _(&CriticalSection);
		FEntryInfo EntryInfo = PackageIdToEntryInfo.FindRef(PackageId);
		return EntryInfo.Status != EPackageStoreEntryStatus::Missing;
	}

	virtual FPackageStoreEntryHandle GetPackageEntryHandle(FPackageId PackageId, const FName& PackageName) override
	{
		using namespace UE::Cook;
		using namespace UE::PackageStore::Messaging;

		{
			FScopeLock _(&CriticalSection);
			FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindOrAdd(PackageId, FEntryInfo());

			if (EntryInfo.Status == EPackageStoreEntryStatus::Ok)
			{
				check(EntryInfo.EntryIndex != INDEX_NONE);
				return FPackageStoreEntryHandle::Create(EntryInfo.EntryIndex, EPackageStoreEntryStatus::Ok); 
			}
			else if (PackageName == NAME_None)
			{
				// When importing package(s) recursive the package name is NONE. Imported
				// package(s) are expected to be cooked and ready. This is handled by the COTF server.
				return FPackageStoreEntryHandle::Create(0, EPackageStoreEntryStatus::Missing); 
			}
			
			if (EntryInfo.Status == EPackageStoreEntryStatus::Missing)
			{
				return FPackageStoreEntryHandle::Create(0, EntryInfo.Status); 
			}

			if (EntryInfo.Status == EPackageStoreEntryStatus::Pending)
			{
				CheckActivity();
				return FPackageStoreEntryHandle::Create(0, EntryInfo.Status); 
			}

			// The package hasn't been requested, set the status to pending.
			EntryInfo.PackageName = PackageName;
			EntryInfo.Status = EPackageStoreEntryStatus::Pending;
		}

		LastClientActivtyTime = FPlatformTime::Seconds();
		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Requesting package '%s'"), *PackageName.ToString());

		FCookOnTheFlyRequest Request(ECookOnTheFlyMessage::CookPackage);
		Request.SetBodyTo(FCookPackageRequest { PackageName });
		FCookOnTheFlyResponse Response = CookOnTheFlyServerConnection.SendRequest(Request).Get();

		if (!Response.IsOk())
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to send '%s' request"), LexToString(Request.GetHeader().MessageType));
			return FPackageStoreEntryHandle::Create(0, EPackageStoreEntryStatus::Missing); 
		}

		FCookPackageResponse CookPackageResponse = Response.GetBodyAs<FCookPackageResponse>();

		{
			FScopeLock _(&CriticalSection);

			if (CookPackageResponse.Status == EPackageStoreEntryStatus::Missing)
			{
				FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindChecked(PackageId);
				EntryInfo.Status = EPackageStoreEntryStatus::Missing;
				return FPackageStoreEntryHandle::Create(0, EPackageStoreEntryStatus::Missing); 
			}
			else
			{
				return FPackageStoreEntryHandle::Create(0, EPackageStoreEntryStatus::Pending);
			}
		}
	}

	virtual FPackageStoreEntry GetPackageEntry(FPackageStoreEntryHandle Handle) override
	{
		check(Handle.IsValid());
		check(Handle.Status() == EPackageStoreEntryStatus::Ok);

		FScopeLock _(&CriticalSection);
		const int32 EntryIndex = static_cast<int32>(Handle.Value());
		const FPackageStoreEntryResource& Entry = PackageEntries[EntryIndex];

		return FPackageStoreEntry
		{
			Entry.ExportInfo,
			Entry.ImportedPackageIds
		};
	}

	virtual bool GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId)  override
	{
		return false;
	}

	void AddPackages(TArray<FPackageStoreEntryResource> Entries, TArray<FPackageId> FailedPackageIds)
	{
		FScopeLock _(&CriticalSection);
		
		for (FPackageId FailedPackageId : FailedPackageIds)
		{
			UE_LOG(LogCookOnTheFly, Verbose, TEXT("'0x%llX' [Failed]"), FailedPackageId.Value());
			FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindOrAdd(FailedPackageId, FEntryInfo());
			EntryInfo.Status = EPackageStoreEntryStatus::Missing;
			PackageStats.Failed++;
		}

		const int32 NumPackageEntries = PackageEntries.Num();

		for (FPackageStoreEntryResource& Entry : Entries)
		{
			const FPackageId PackageId = Entry.GetPackageId();
			FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindOrAdd(PackageId, FEntryInfo());

			if (EntryInfo.EntryIndex == INDEX_NONE)
			{
				EntryInfo.Status = EPackageStoreEntryStatus::Ok;
				EntryInfo.EntryIndex = PackageEntries.Add();
				PackageEntries[EntryInfo.EntryIndex] = MoveTemp(Entry);
				PackageStats.Cooked++;

				UE_LOG(LogCookOnTheFly, Verbose, TEXT("'%s' [OK] (Cooked/Failed='%d/%d')"),
					*PackageEntries[EntryInfo.EntryIndex].PackageName.ToString(), PackageStats.Cooked.Load(), PackageStats.Failed.Load());
			}
		}
	}

	void OnCookOnTheFlyMessage(const UE::Cook::FCookOnTheFlyMessage& Message)
	{
		using namespace UE::PackageStore::Messaging;

		switch (Message.GetHeader().MessageType)
		{
			case UE::Cook::ECookOnTheFlyMessage::PackagesCooked:
			{
				FPackagesCookedMessage PackagesCookedMessage = Message.GetBodyAs<FPackagesCookedMessage>();
				FPackageStoreData& PackageStoreData = PackagesCookedMessage.PackageStoreData;

				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Received '%s' message, Cooked='%d', Failed='%d', Server total='%d/%d' (Cooked/Failed)"),
					LexToString(Message.GetHeader().MessageType),
					PackageStoreData.CookedPackages.Num(),
					PackageStoreData.FailedPackages.Num(),
					PackageStoreData.TotalCookedPackages,
					PackageStoreData.TotalFailedPackages);

				AddPackages(MoveTemp(PackageStoreData.CookedPackages), MoveTemp(PackageStoreData.FailedPackages));

				UE_CLOG(PackageStoreData.TotalCookedPackages != PackageStats.Cooked.Load() || PackageStoreData.TotalFailedPackages != PackageStats.Failed.Load(),
					LogCookOnTheFly, Warning, TEXT("Client/Server package mismatch, Cooked='%d/%d', Failed='%d/%d' (Client/Server)"),
					PackageStats.Cooked.Load(), PackageStoreData.TotalCookedPackages, PackageStats.Failed.Load(), PackageStoreData.TotalFailedPackages);

				EntriesAddedCallback();

				break;
			}

			default:
				break;
		}

		LastServerActivtyTime = FPlatformTime::Seconds();
	}

	void CheckActivity()
	{
		const double TimeSinceLastClientActivity = FPlatformTime::Seconds() - LastClientActivtyTime;
		const double TimeSinceLastServerActivity = FPlatformTime::Seconds() - LastServerActivtyTime;
		const double TimeSinceLastWarning = FPlatformTime::Seconds() - LastWarningTime;

		if (TimeSinceLastClientActivity > MaxInactivityTime &&
			TimeSinceLastServerActivity > MaxInactivityTime &&
			TimeSinceLastWarning > TimeBetweenWarning)
		{
			LastWarningTime = FPlatformTime::Seconds();

			UE_LOG(LogCookOnTheFly, Log, TEXT("No server response in '%.2lf' seconds"), TimeSinceLastServerActivity);

			UE_LOG(LogCookOnTheFly, Log, TEXT("=== Pending Packages ==="));
			{
				FScopeLock _(&CriticalSection);
				for (const auto& KeyValue : PackageIdToEntryInfo)
				{
					const FEntryInfo& EntryInfo = KeyValue.Value;
					if (EntryInfo.Status == EPackageStoreEntryStatus::Pending)
					{
						UE_LOG(LogCookOnTheFly, Log, TEXT("%s"), *EntryInfo.PackageName.ToString());
					}
				}
			}
		}
	}

	struct FEntryInfo
	{
		FName PackageName;
		EPackageStoreEntryStatus Status = EPackageStoreEntryStatus::None;
		int32 EntryIndex = INDEX_NONE;
	};

	struct FPackageStats
	{
		TAtomic<uint32> Cooked {0};
		TAtomic<uint32> Failed {0};
	};

	FIoDispatcher& IoDispatcher;
	UE::Cook::ICookOnTheFlyServerConnection& CookOnTheFlyServerConnection;
	TFunction<void()> EntriesAddedCallback;
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

TUniquePtr<IPackageStore> MakeCookOnTheFlyPackageStore(
	FIoDispatcher& IoDispatcher,
	UE::Cook::ICookOnTheFlyServerConnection& CookOnTheFlyServerConnection,
	TFunction<void()>&& EntriesAddedCallback)
{
	return MakeUnique<FCookOnTheFlyPackageStore>(IoDispatcher, CookOnTheFlyServerConnection, MoveTemp(EntriesAddedCallback));
}

#endif // WITH_COTF
