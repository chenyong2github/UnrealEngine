// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Memory/CompositeBuffer.h"
#include "Misc/PackagePath.h"
#include "PackageStoreManifest.h"
#include "Serialization/PackageWriterToSharedBuffer.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class FAsyncIODelete;
class IPlugin;
class ITargetPlatform;
struct FPackageNameCache;

/** A CookedPackageWriter that saves cooked packages in separate .uasset,.uexp,.ubulk files in the Saved\Cooked\[Platform] directory. */
class FLooseCookedPackageWriter : public TPackageWriterToSharedBuffer<ICookedPackageWriter>
{
public:
	using Super = TPackageWriterToSharedBuffer<ICookedPackageWriter>;

	FLooseCookedPackageWriter(const FString& OutputPath, const FString& MetadataDirectoryPath, const ITargetPlatform* TargetPlatform,
		FAsyncIODelete& InAsyncIODelete, const FPackageNameCache& InPackageNameCache, const TArray<TSharedRef<IPlugin>>& InPluginsToRemap);
	~FLooseCookedPackageWriter();

	virtual FCookCapabilities GetCookCapabilities() const override
	{
		FCookCapabilities Result;
		Result.bDiffModeSupported = true;
		return Result;
	}

	virtual void BeginPackage(const FBeginPackageInfo& Info) override;
	virtual void AddToExportsSize(int32& ExportsSize) override;

	virtual FDateTime GetPreviousCookTime() const override;
	virtual void Initialize(const FCookInfo& Info) override;
	virtual void BeginCook() override;
	virtual void EndCook() override;
	virtual void Flush() override;
	virtual TUniquePtr<FAssetRegistryState> LoadPreviousAssetRegistry() override;
	virtual FCbObject GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey) override;
	virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) override;
	virtual void RemoveCookedPackages() override;
	virtual void MarkPackagesUpToDate(TArrayView<const FName> UpToDatePackages) override;
	virtual bool GetPreviousCookedBytes(FName PackageName, FPreviousCookedBytesData& OutData) override;
	virtual void CompleteExportsArchiveForDiff(FLargeMemoryWriter& ExportsArchive) override;
	virtual TFuture<FMD5Hash> CommitPackageInternal(const FCommitPackageInfo& Info) override;
	virtual void ResetPackage() override;

	static EPackageExtension BulkDataTypeToExtension(FBulkDataInfo::EType BulkDataType);
private:

	/** Buffers that are combined into the HeaderAndExports file (which is then split into .uasset + .uexp). */
	struct FExportBuffer
	{
		FSharedBuffer Buffer;
		TArray<FFileRegion> Regions;
	};

	/**
	 * The data needed to asynchronously write one of the files (.uasset, .uexp, .ubulk, and any additional),
	 * without reference back to other data on this writer.
	 */
	struct FWriteFileData
	{
		FString Filename;
		FCompositeBuffer Buffer;
		TArray<FFileRegion> Regions;
		bool bIsSidecar;

		void Write(FMD5& AccumulatedHash, EWriteOptions WriteOptions) const;
	};

	/** Stack data for the helper functions of CommitPackageInternal. */
	struct FCommitContext
	{
		const FCommitPackageInfo& Info;
		TArray<FExportBuffer> ExportsBuffers;
		TArray<FWriteFileData> OutputFiles;
	};

	/* Delete the sandbox directory (asynchronously) in preparation for a clean cook */
	void DeleteSandboxDirectory();
	/**
	* Searches the disk for all the cooked files in the sandbox path provided
	* Returns a map of the uncooked file path matches to the cooked file path for each package which exists
	*
	* @param UncookedpathToCookedPath out Map of the uncooked path matched with the cooked package which exists
	* @param SandboxRootDir root dir to search for cooked packages in
	*/
	void GetAllCookedFiles();

	FName ConvertCookedPathToUncookedPath(
		const FString& SandboxRootDir, const FString& RelativeRootDir,
		const FString& SandboxProjectDir, const FString& RelativeProjectDir,
		const FString& CookedPath, FString& OutUncookedPath) const;
	void RemoveCookedPackagesByUncookedFilename(const TArray<FName>& UncookedFileNamesToRemove);
	TFuture<FMD5Hash> AsyncSave(const FCommitPackageInfo& Info);

	void CollectForSavePackageData(FCommitContext& Context);
	void CollectForSaveBulkData(FCommitContext& Context);
	void CollectForSaveLinkerAdditionalDataRecords(FCommitContext& Context);
	void CollectForSaveAdditionalFileRecords(FCommitContext& Context);
	void CollectForSaveExportsFooter(FCommitContext& Context);
	void CollectForSaveExportsBuffers(FCommitContext& Context);
	TFuture<FMD5Hash> AsyncSaveOutputFiles(FCommitContext& Context);
	void UpdateManifest();

	TMap<FName, FName> UncookedPathToCookedPath;
	FString OutputPath;
	FString MetadataDirectoryPath;
	const ITargetPlatform& TargetPlatform;
	const FPackageNameCache& PackageNameCache;
	FPackageStoreManifest PackageStoreManifest;
	const TArray<TSharedRef<IPlugin>>& PluginsToRemap;
	FAsyncIODelete& AsyncIODelete;
	bool bIterateSharedBuild;
	bool bCompletedExportsArchiveForDiff;
};
