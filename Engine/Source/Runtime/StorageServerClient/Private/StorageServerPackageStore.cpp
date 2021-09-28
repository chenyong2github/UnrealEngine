// Copyright Epic Games, Inc. All Rights Reserved.

#include "StorageServerPackageStore.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoDispatcher.h"
#include "StorageServerConnection.h"

#if !UE_BUILD_SHIPPING

FStorageServerPackageStore::FStorageServerPackageStore(FStorageServerConnection& Connection)
{
	FIoChunkId HeaderChunkId = CreateIoChunkId(FIoContainerId::FromName(TEXT("global")).Value(), 0, EIoChunkType::ContainerHeader);
	Connection.ReadChunkRequest(HeaderChunkId, 0, uint64(-1), [this](FStorageServerResponse& ResponseStream)
	{
		if (ResponseStream.IsOk())
		{
			FIoContainerHeader ContainerHeader;
			ResponseStream << ContainerHeader;
			StoreEntriesData = MoveTemp(ContainerHeader.StoreEntries);
			StoreEntriesMap.Reserve(ContainerHeader.PackageCount);
			TArrayView<const FFilePackageStoreEntry> StoreEntries(reinterpret_cast<const FFilePackageStoreEntry*>(StoreEntriesData.GetData()), ContainerHeader.PackageCount);
			int32 Index = 0;
			for (const FFilePackageStoreEntry& StoreEntry : StoreEntries)
			{
				const FPackageId& PackageId = ContainerHeader.PackageIds[Index];
				check(PackageId.IsValid());
				StoreEntriesMap.FindOrAdd(PackageId, &StoreEntry);
				++Index;
			}
		}
	});
}

bool FStorageServerPackageStore::DoesPackageExist(FPackageId PackageId)
{
	return PackageId.IsValid() && StoreEntriesMap.Contains(PackageId);
}

FPackageStoreEntryHandle FStorageServerPackageStore::GetPackageEntryHandle(FPackageId PackageId, const FName& PackageName)
{
	const FFilePackageStoreEntry* FindEntry = StoreEntriesMap.FindRef(PackageId);
	const uint64 Handle = reinterpret_cast<uint64>(FindEntry);
	return FPackageStoreEntryHandle::Create(Handle, Handle ? EPackageStoreEntryStatus::Ok : EPackageStoreEntryStatus::Missing);
}

FPackageStoreEntry FStorageServerPackageStore::GetPackageEntry(FPackageStoreEntryHandle Handle)
{
	check(Handle.IsValid());
	const FFilePackageStoreEntry* Entry = reinterpret_cast<const FFilePackageStoreEntry*>(Handle.Value());
	check(Entry);
	return FPackageStoreEntry
	{
		FPackageStoreExportInfo
		{
			Entry->ExportCount,
			Entry->ExportBundleCount
		},
		MakeArrayView(Entry->ImportedPackages.Data(), Entry->ImportedPackages.Num()),
		MakeArrayView(Entry->ShaderMapHashes.Data(), Entry->ShaderMapHashes.Num())
	};
}

#endif