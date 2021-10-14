// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Serialization/PackageWriter.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "PackageStoreManifest.h"

class FAsyncIODelete;
class IPlugin;
class ITargetPlatform;
struct FPackageNameCache;

/** A CookedPackageWriter that saves cooked packages in separate .uasset,.uexp,.ubulk files in the Saved\Cooked\[Platform] directory. */
class FLooseCookedPackageWriter
	: public ICookedPackageWriter
{
public:
	FLooseCookedPackageWriter(const FString& OutputPath, const FString& MetadataDirectoryPath, const ITargetPlatform* TargetPlatform,
		FAsyncIODelete& InAsyncIODelete, const FPackageNameCache& InPackageNameCache, const TArray<TSharedRef<IPlugin>>& InPluginsToRemap);
	~FLooseCookedPackageWriter();

	FCookCapabilities GetCookCapabilities() const override
	{
		FCookCapabilities Result;
		Result.bDiffModeSupported = true;
		// TODO: Finish implementing this class so we can turn bSavePackageSupported on (and delete bSavePackageSupported as everything will have it set to true)
		Result.bSavePackageSupported = false;
		return Result;
	}

	void BeginPackage(const FBeginPackageInfo& Info) override;
	void CommitPackage(const FCommitPackageInfo& Info) override;
	void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override;
	void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override;

	FDateTime GetPreviousCookTime() const override;
	void BeginCook(const FCookInfo& Info) override;
	void EndCook() override;
	void Flush() override;
	TUniquePtr<FAssetRegistryState> LoadPreviousAssetRegistry() override;
	FCbObject GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey) override;
	void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) override;
	void RemoveCookedPackages() override;
	void MarkPackagesUpToDate(TArrayView<const FName> UpToDatePackages) override;
	bool GetPreviousCookedBytes(FName PackageName, const ITargetPlatform* InTargetPlatform,
		const TCHAR* SandboxFilename, FPreviousCookedBytesData& OutData) override;
	void SetCookOutputLocation(EOutputLocation location) override;

private:

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

	TMap<FName, FName> UncookedPathToCookedPath;
	FString OutputPath;
	FString MetadataDirectoryPath;
	const ITargetPlatform& TargetPlatform;
	const FPackageNameCache& PackageNameCache;
	FPackageStoreManifest PackageStoreManifest;
	const TArray<TSharedRef<IPlugin>>& PluginsToRemap;
	FAsyncIODelete& AsyncIODelete;
	bool bIterateSharedBuild;
};
