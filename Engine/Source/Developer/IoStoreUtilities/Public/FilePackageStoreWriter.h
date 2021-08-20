// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/AsyncLoading2.h"
#include "IO/PackageStore.h"
#include "PackageStoreWriter.h"

class FIoStoreWriterContext;
class FIoStoreWriter;
class FPackageStoreOptimizer;
class FPackageStorePackage;
class FPackageStoreContainerHeaderEntry;

class FPackageStoreManifest
{
public:
	struct FFileInfo
	{
		FString FileName;
		FIoChunkId ChunkId;
	};

	struct FPackageInfo
	{
		FName PackageName;
		FIoChunkId PackageChunkId;
		TArray<FIoChunkId> BulkDataChunkIds;
	};
	
	struct FZenServerInfo
	{
		FString HostName;
		uint16 Port;
		FString ProjectId;
		FString OplogId;
	};

	IOSTOREUTILITIES_API FPackageStoreManifest() = default;
	IOSTOREUTILITIES_API ~FPackageStoreManifest() = default;

	IOSTOREUTILITIES_API void BeginPackage(FName PackageName);
	IOSTOREUTILITIES_API void AddPackageData(FName PackageName, const FString& FileName, const FIoChunkId& ChunkId);
	IOSTOREUTILITIES_API void AddBulkData(FName PackageName, const FString& FileName, const FIoChunkId& ChunkId);

	IOSTOREUTILITIES_API FIoStatus Save(const TCHAR* Filename) const;
	IOSTOREUTILITIES_API FIoStatus Load(const TCHAR* Filename);

	IOSTOREUTILITIES_API TArray<FFileInfo> GetFiles() const;
	IOSTOREUTILITIES_API TArray<FPackageInfo> GetPackages() const;

	IOSTOREUTILITIES_API FZenServerInfo& EditZenServerInfo();
	IOSTOREUTILITIES_API const FZenServerInfo* ReadZenServerInfo() const;

private:
	mutable FCriticalSection CriticalSection;
	TMap<FName, FPackageInfo> PackageInfoByNameMap;
	TMap<FIoChunkId, FString> FileNameByChunkIdMap;
	TUniquePtr<FZenServerInfo> ZenServerInfo;
};

/**
 * A PackageWriter that saves cooked packages for use by IoStore, and stores them in loose files.
 */
class FFilePackageStoreWriter
	: public IPackageStoreWriter
{
public:
	IOSTOREUTILITIES_API FFilePackageStoreWriter(const FString& OutputPath, const FString& MetadataDirectoryPath, const ITargetPlatform* TargetPlatform);
	IOSTOREUTILITIES_API ~FFilePackageStoreWriter();
	IOSTOREUTILITIES_API virtual void BeginPackage(const FBeginPackageInfo& Info) override;
	IOSTOREUTILITIES_API virtual void CommitPackage(const FCommitPackageInfo& Info) override;
	IOSTOREUTILITIES_API virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override
	{
		// Should not be called because bLinkerAdditionalDataInSeparateArchive is false
		checkNoEntry();
	}

	IOSTOREUTILITIES_API virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override { return false; }

	IOSTOREUTILITIES_API virtual void BeginCook(const FCookInfo& Info);
	IOSTOREUTILITIES_API virtual void EndCook();

	virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&& Callback) override
	{
		check(false);
	}

	DECLARE_DERIVED_EVENT(FFilePackageStoreWriter, IPackageStoreWriter::FCommitEvent, FCommitEvent);
	virtual FCommitEvent& OnCommit() override
	{
		check(false);
		return CommitEvent;
	}

	virtual void Flush() override
	{ }

	virtual void GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages)
	{ }

	virtual FCbObject GetTargetDomainDependencies(FName PackageName) override
	{ return FCbObject(); }

	virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove)
	{ }

private:
	TUniquePtr<FPackageStoreOptimizer> PackageStoreOptimizer;
	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext;
	TUniquePtr<FIoStoreWriter> IoStoreWriter;
	const ITargetPlatform& TargetPlatform;
	FString OutputPath;
	FString MetadataDirectoryPath;
	FPackageStoreManifest PackageStoreManifest;
	FIoContainerId ContainerId = FIoContainerId::FromName(TEXT("global"));
	TArray<FPackageStoreEntryResource> PackageStoreEntries;
	FCommitEvent CommitEvent;
};
