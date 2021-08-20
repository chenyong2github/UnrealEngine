// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/AsyncLoading2.h"
#include "IO/IoDispatcher.h"
#include "Containers/Map.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/PackageWriter.h"
#include "IO/PackageStore.h"
#include "FilePackageStoreWriter.h"

namespace UE {
	class FZenStoreHttpClient;
}

class FPackageStoreOptimizer;
class FZenFileSystemManifest;
class FCbPackage;
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
	IOSTOREUTILITIES_API virtual void CommitPackage(const FCommitPackageInfo& Info) override;

	IOSTOREUTILITIES_API virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override;
	IOSTOREUTILITIES_API virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void BeginCook(const FCookInfo& Info) override;
	IOSTOREUTILITIES_API virtual void EndCook() override;

	IOSTOREUTILITIES_API virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&& Callback) override;

	DECLARE_DERIVED_EVENT(FZenStoreWriter, IPackageStoreWriter::FCommitEvent, FCommitEvent);
	IOSTOREUTILITIES_API virtual FCommitEvent& OnCommit() override
	{
		return CommitEvent;
	}

	virtual void Flush() override;

	IOSTOREUTILITIES_API void WriteIoStorePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const FPackageStoreEntryResource& PackageStoreEntry, const TArray<FFileRegion>& FileRegions);

	IOSTOREUTILITIES_API virtual void GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages) override;
	IOSTOREUTILITIES_API virtual FCbObject GetTargetDomainDependencies(FName PackageName) override;
	IOSTOREUTILITIES_API virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) override;
	IOSTOREUTILITIES_API virtual void RemoveCookedPackages() override;

private:
	void CreateProjectMetaData(FCbPackage& Pkg, FCbWriter& PackageObj, bool bGenerateContainerHeader);
	void BroadcastCommit(IPackageStoreWriter::FCommitEventArgs& EventArgs);

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
	TArray<FCookedPackageInfo>			CookedPackagesInfo;
	TMap<FName, int32>					PackageNameToIndex;

	TUniquePtr<FZenFileSystemManifest>	ZenFileSystemManifest;
	
	FCriticalSection					CommitEventCriticalSection;
	FCommitEvent						CommitEvent;

	class FZenStoreHttpQueue;
	TUniquePtr<FZenStoreHttpQueue>		HttpQueue;
	
	ICookedPackageWriter::FCookInfo::ECookMode CookMode;

	FZenStats							ZenStats;

	bool								bInitialized;
};
