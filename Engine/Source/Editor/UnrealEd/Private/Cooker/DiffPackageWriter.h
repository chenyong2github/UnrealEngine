// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/ArchiveStackTrace.h"
#include "Serialization/PackageWriter.h"
#include "AssetRegistry/AssetRegistryState.h"

/** A CookedPackageWriter that diffs the output from the current cook with the file that was saved in the previous cook. */
class FDiffPackageWriter : public ICookedPackageWriter
{
public:
	FDiffPackageWriter(TUniquePtr<ICookedPackageWriter>&& InInner);

	// IPackageWriter
	virtual FCapabilities GetCapabilities() const override
	{
		FCapabilities Result = Inner->GetCapabilities();
		Result.bIgnoreHeaderDiffs = bIgnoreHeaderDiffs;
		return Result;
	}
	virtual void BeginPackage(const FBeginPackageInfo& Info) override;
	virtual TFuture<FMD5Hash> CommitPackage(FCommitPackageInfo&& Info) override;
	virtual void WritePackageData(const FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive, const TArray<FFileRegion>& FileRegions) override;
	virtual void WriteBulkData(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override
	{
		Inner->WriteBulkData(Info, BulkData, FileRegions);
	}
	virtual void WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override
	{
		Inner->WriteAdditionalFile(Info, FileData);
	}
	virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override
	{
		Inner->WriteLinkerAdditionalData(Info, Data, FileRegions);
	}
	virtual void AddToExportsSize(int64& ExportsSize) override
	{
		Inner->AddToExportsSize(ExportsSize);
	}
	virtual TUniquePtr<FLargeMemoryWriter> CreateLinkerArchive(FName PackageName, UObject* Asset) override;
	virtual bool IsPreSaveCompleted() const override
	{
		return bDiffCallstack;
	}

	// ICookedPackageWriter
	virtual FCookCapabilities GetCookCapabilities() const override
	{
		FCookCapabilities Result = Inner->GetCookCapabilities();
		Result.bDiffModeSupported = false; // DiffPackageWriter can not be an inner of another DiffPackageWriter
		return Result;
	}
	virtual FDateTime GetPreviousCookTime() const
	{
		return Inner->GetPreviousCookTime();
	}
	virtual void Initialize(const FCookInfo& Info) override
	{
		Inner->Initialize(Info);
	}
	virtual void BeginCook() override
	{
		Inner->BeginCook();
	}
	virtual void EndCook() override
	{
		Inner->EndCook();
	}
	virtual void Flush() override
	{
		Inner->Flush();
	}
	virtual TUniquePtr<FAssetRegistryState> LoadPreviousAssetRegistry() override
	{
		return Inner->LoadPreviousAssetRegistry();
	}
	virtual FCbObject GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey) override
	{
		return Inner->GetOplogAttachment(PackageName, AttachmentKey);
	}
	virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) override
	{
		Inner->RemoveCookedPackages(PackageNamesToRemove);
	}
	virtual void RemoveCookedPackages() override
	{
		Inner->RemoveCookedPackages();
	}
	virtual void MarkPackagesUpToDate(TArrayView<const FName> UpToDatePackages) override
	{
		Inner->MarkPackagesUpToDate(UpToDatePackages);
	}

	// FDiffPackageWriter
	/** Return whether a difference was found in the first save */
	bool IsDifferenceFound() const
	{
		return bIsDifferent;
	}

	/** Prepare the Inner PackageWriter for a second save, and set settings for callstack diffing. */
	void BeginDiffCallstack();

private:
	FArchiveDiffMap DiffMap;
	FBeginPackageInfo BeginInfo;
	TUniquePtr<ICookedPackageWriter> Inner;
	int32 MaxDiffsToLog = 5;
	bool bSaveForDiff = false;
	bool bIgnoreHeaderDiffs = false;
	bool bIsDifferent = false;
	bool bDiffCallstack = false;
};

