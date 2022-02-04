// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFlyPackageStore.h"

#if WITH_COTF

#include "HAL/PlatformTime.h"
#include "Containers/ChunkedArray.h"
#include "Serialization/MemoryReader.h"
#include "CookOnTheFlyMessages.h"

FCookOnTheFlyPackageStore::FCookOnTheFlyPackageStore(UE::Cook::ICookOnTheFlyServerConnection& InCookOnTheFlyServerConnection)
	: CookOnTheFlyServerConnection(InCookOnTheFlyServerConnection)
{
	// Index zero is invalid
	PackageEntries.Add();
	CookOnTheFlyServerConnection.OnMessage().AddRaw(this, &FCookOnTheFlyPackageStore::OnCookOnTheFlyMessage);

	using namespace UE::Cook;
	using namespace UE::ZenCookOnTheFly::Messaging;

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

bool FCookOnTheFlyPackageStore::DoesPackageExist(FPackageId PackageId)
{
	FScopeLock _(&CriticalSection);
	FEntryInfo EntryInfo = PackageIdToEntryInfo.FindRef(PackageId);
	return EntryInfo.Status != EPackageStoreEntryStatus::Missing;
}

EPackageStoreEntryStatus FCookOnTheFlyPackageStore::GetPackageStoreEntry(FPackageId PackageId, FPackageStoreEntry& OutPackageStoreEntry)
{
	using namespace UE::Cook;
	using namespace UE::ZenCookOnTheFly::Messaging;

	{
		FScopeLock _(&CriticalSection);
		FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindOrAdd(PackageId, FEntryInfo());

		if (EntryInfo.Status == EPackageStoreEntryStatus::Ok)
		{
			check(EntryInfo.EntryIndex != INDEX_NONE);
			return CreatePackageStoreEntry(EntryInfo, OutPackageStoreEntry);
		}
			
		if (EntryInfo.Status == EPackageStoreEntryStatus::Missing)
		{
			return EPackageStoreEntryStatus::Missing;
		}

		if (EntryInfo.Status == EPackageStoreEntryStatus::Pending)
		{
			CheckActivity();
			return EPackageStoreEntryStatus::Pending;
		}

		// The package hasn't been requested, set the status to pending.
		EntryInfo.Status = EPackageStoreEntryStatus::Pending;
	}

	LastClientActivtyTime = FPlatformTime::Seconds();
	UE_LOG(LogCookOnTheFly, Verbose, TEXT("Requesting package 0x%llX"), PackageId.ValueForDebugging());

	FCookOnTheFlyRequest Request(ECookOnTheFlyMessage::CookPackage);
	Request.SetBodyTo(FCookPackageRequest { PackageId });
	FCookOnTheFlyResponse Response = CookOnTheFlyServerConnection.SendRequest(Request).Get();

	if (!Response.IsOk())
	{
		UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to send '%s' request"), LexToString(Request.GetHeader().MessageType));
		FScopeLock _(&CriticalSection);
		FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindChecked(PackageId);
		EntryInfo.Status = EPackageStoreEntryStatus::Missing;
		return EPackageStoreEntryStatus::Missing;
	}

	FCookPackageResponse CookPackageResponse = Response.GetBodyAs<FCookPackageResponse>();

	{
		FScopeLock _(&CriticalSection);

		FEntryInfo& EntryInfo = PackageIdToEntryInfo.FindChecked(PackageId);
		if (CookPackageResponse.Status == EPackageStoreEntryStatus::Missing)
		{
			EntryInfo.Status = EPackageStoreEntryStatus::Missing;
			return EPackageStoreEntryStatus::Missing;
		}
		else
		{
			return CreatePackageStoreEntry(EntryInfo, OutPackageStoreEntry);
		}
	}
}

EPackageStoreEntryStatus FCookOnTheFlyPackageStore::CreatePackageStoreEntry(const FEntryInfo& EntryInfo, FPackageStoreEntry& OutPackageStoreEntry)
{
	if (EntryInfo.Status == EPackageStoreEntryStatus::Ok)
	{
		const FPackageStoreEntryResource& Entry = PackageEntries[EntryInfo.EntryIndex];
		OutPackageStoreEntry.ExportInfo = Entry.ExportInfo;
		OutPackageStoreEntry.ImportedPackageIds = Entry.ImportedPackageIds;
		return EPackageStoreEntryStatus::Ok;
	}
	else
	{
		return EntryInfo.Status;
	}
}

void FCookOnTheFlyPackageStore::AddPackages(TArray<FPackageStoreEntryResource> Entries, TArray<FPackageId> FailedPackageIds)
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

void FCookOnTheFlyPackageStore::OnCookOnTheFlyMessage(const UE::Cook::FCookOnTheFlyMessage& Message)
{
	using namespace UE::ZenCookOnTheFly::Messaging;

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

			PendingEntriesAdded.Broadcast();

			break;
		}

		default:
			break;
	}

	LastServerActivtyTime = FPlatformTime::Seconds();
}

void FCookOnTheFlyPackageStore::CheckActivity()
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
					UE_LOG(LogCookOnTheFly, Log, TEXT("0x%llX"), KeyValue.Key.ValueForDebugging());
				}
			}
		}
	}
}

#endif // WITH_COTF
