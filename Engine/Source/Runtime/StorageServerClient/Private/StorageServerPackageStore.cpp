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

EPackageStoreEntryStatus FStorageServerPackageStore::GetPackageStoreEntry(FPackageId PackageId, FPackageStoreEntry& OutPackageStoreEntry)
{
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

#endif