// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/OodleDataCompression.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "IO/IoDispatcher.h"
#include "IO/PackageStore.h"
#include "PackageStoreManifest.h"
#include "PackageStoreWriter.h"
#include "Serialization/AsyncLoading2.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/PackageWriter.h"

namespace UE {
	class FZenStoreHttpClient;
}

class FPackageStoreOptimizer;
class FZenFileSystemManifest;
class FCbPackage;
class FCbAttachment;
class FCbWriter;

/** 
 * A PackageStoreWriter that saves cooked packages for use by IoStore, and stores them in the Zen storage service.
 */
class FZenStoreWriter
	: public IPackageStoreWriter
{
public:
	IOSTOREUTILITIES_API FZenStoreWriter(	const FString& OutputPath, 
											const FString& MetadataDirectoryPath, 
											const ITargetPlatform* TargetPlatform);

	IOSTOREUTILITIES_API ~FZenStoreWriter();

	IOSTOREUTILITIES_API virtual void BeginPackage(const FBeginPackageInfo& Info) override;
	IOSTOREUTILITIES_API virtual TFuture<FMD5Hash> CommitPackage(FCommitPackageInfo&& Info) override;

	IOSTOREUTILITIES_API virtual void WritePackageData(const FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override;
	IOSTOREUTILITIES_API virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override;

	IOSTOREUTILITIES_API virtual void WriteBulkData(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void Initialize(const FCookInfo& Info) override;
	IOSTOREUTILITIES_API virtual void BeginCook() override;
	IOSTOREUTILITIES_API virtual void EndCook() override;

	IOSTOREUTILITIES_API virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>, TArrayView<const FOplogCookInfo>)>&& Callback) override;

	DECLARE_DERIVED_EVENT(FZenStoreWriter, IPackageStoreWriter::FCommitEvent, FCommitEvent);
	IOSTOREUTILITIES_API virtual FCommitEvent& OnCommit() override
	{
		return CommitEvent;
	}
	DECLARE_DERIVED_EVENT(FZenStoreWriter, IPackageStoreWriter::FMarkUpToDateEvent, FMarkUpToDateEvent);
	IOSTOREUTILITIES_API virtual FMarkUpToDateEvent& OnMarkUpToDate() override
	{
		return MarkUpToDateEvent;
	}


	virtual void Flush() override;

	IOSTOREUTILITIES_API void WriteIoStorePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const FPackageStoreEntryResource& PackageStoreEntry, const TArray<FFileRegion>& FileRegions);

	IOSTOREUTILITIES_API TUniquePtr<FAssetRegistryState> LoadPreviousAssetRegistry() override;
	IOSTOREUTILITIES_API virtual FCbObject GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey) override;
	IOSTOREUTILITIES_API virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) override;
	IOSTOREUTILITIES_API virtual void RemoveCookedPackages() override;
	IOSTOREUTILITIES_API virtual void MarkPackagesUpToDate(TArrayView<const FName> UpToDatePackages) override;

private:
	struct FBulkDataEntry
	{
		FIoBuffer Payload;
		FBulkDataInfo Info;
		FCbObjectId ChunkId;
		bool IsValid = false;
	};

	struct FPackageDataEntry
	{
		FIoBuffer Payload;
		FPackageInfo Info;
		FCbObjectId ChunkId;
		FPackageStoreEntryResource PackageStoreEntry;
		bool IsValid = false;
	};

	struct FFileDataEntry
	{
		FIoBuffer Payload;
		FAdditionalFileInfo Info;
		FString ZenManifestServerPath;
		FString ZenManifestClientPath;
	};

	struct FPendingPackageState
	{
		FName PackageName;
		FPackageDataEntry PackageData;
		TArray<FBulkDataEntry> BulkData;
		TArray<FFileDataEntry> FileData;
	};

	struct FZenStats
	{
		uint64 TotalBytes = 0;
		double TotalRequestTime = 0.0;
	};

	void CreateProjectMetaData(FCbPackage& Pkg, FCbWriter& PackageObj, bool bGenerateContainerHeader);
	void BroadcastCommit(IPackageStoreWriter::FCommitEventArgs& EventArgs);
	void BroadcastMarkUpToDate(IPackageStoreWriter::FMarkUpToDateEventArgs& EventArgs);
	TFuture<FMD5Hash> AsyncComputeCookedHash(const FPendingPackageState& PackageState);
	FCbAttachment CreateAttachment(FSharedBuffer Buffer);
	FCbAttachment CreateAttachment(FIoBuffer Buffer);

	FRWLock								PackagesLock;
	TMap<FName,FPendingPackageState>	PendingPackages;
	TUniquePtr<UE::FZenStoreHttpClient>	HttpClient;

	const ITargetPlatform&				TargetPlatform;
	FString								OutputPath;
	FString								MetadataDirectoryPath;
	FIoContainerId						ContainerId = FIoContainerId::FromName(TEXT("global"));

	FPackageStoreManifest				PackageStoreManifest;
	TUniquePtr<FPackageStoreOptimizer>	PackageStoreOptimizer;
	TArray<FPackageStoreEntryResource>	PackageStoreEntries;
	TArray<FOplogCookInfo>				CookedPackagesInfo;
	TMap<FName, int32>					PackageNameToIndex;

	TUniquePtr<FZenFileSystemManifest>	ZenFileSystemManifest;
	
	FCriticalSection					CommitEventCriticalSection;
	FCommitEvent						CommitEvent;
	FMarkUpToDateEvent					MarkUpToDateEvent;

	class FZenStoreHttpQueue;
	TUniquePtr<FZenStoreHttpQueue>		HttpQueue;
	
	ICookedPackageWriter::FCookInfo::ECookMode CookMode;

	FOodleDataCompression::ECompressor			Compressor;				
	FOodleDataCompression::ECompressionLevel	CompressionLevel;	

	FZenStats							ZenStats;

	bool								bInitialized;

	static void StaticInit();
	static bool IsReservedOplogKey(FUtf8StringView Key);
	static TArray<const UTF8CHAR*>		ReservedOplogKeys;
};
