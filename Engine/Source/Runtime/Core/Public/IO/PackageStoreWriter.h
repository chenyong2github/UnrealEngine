// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringView.h"
#include "IO/IoDispatcher.h"
#include "Misc/SecureHash.h"
#include "Serialization/CompactBinary.h"

struct FPackageStoreEntryResource;

class IPackageStoreWriter
{
public:
	virtual ~IPackageStoreWriter() = default;


	// Properties of the PackageStoreWriter
	/** Whether this writer needs BulkDatas to be written after the Linker's archive has finalized its size.

		Some PackageStoreWriters need that behavior because they put the BulkDatas in a segment following
		the exports in a Composite archive.
	 */
	virtual bool IsAdditionalFilesNeedLinkerSize() const { return false; }
	/** Whether data stored in Linker.AdditionalDataToAppend should be serialized to a separate archive.
	
		If false, the data will be serialized to the end of the LinkerSave archive instead.
	*/
	virtual bool IsLinkerAdditionalDataInSeparateArchive() const { return false; }


	// Events the PackageStoreWriter receives
	struct FBeginPackageInfo
	{
		FName	PackageName;
	};

	/** Mark the beginning of a package store transaction for the specified package

		This must be called before any data is produced for a given package
	  */
	virtual void BeginPackage(const FBeginPackageInfo& Info) = 0;

	struct FCommitPackageInfo
	{
		FName PackageName;
		FGuid PackageGuid;
		FCbObject TargetDomainDependencies;
		bool bSucceeded = false;
	};

	/** Finalize a package started with BeginPackage()
	  */
	virtual void CommitPackage(const FCommitPackageInfo& Info) = 0;

	struct FPackageInfo
	{
		FName		PackageName;
		FString		LooseFilePath;
		uint64		HeaderSize;
		FIoChunkId	ChunkId = FIoChunkId::InvalidChunkId;
	};

	/** Write package data (exports and serialized header)

		This may only be called after a BeginPackage() call has been made
		to signal the start of a package store transaction
	  */
	virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) = 0;

	struct FBulkDataInfo
	{
		enum EType
		{
			Standard,
			Mmap,
			Optional
		};

		FName		PackageName;
		EType		BulkdataType = Standard;
		FString		LooseFilePath;
		FIoChunkId	ChunkId = FIoChunkId::InvalidChunkId;
	};

	/** Write bulk data for the current package
	  */
	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) = 0;

	struct FAdditionalFileInfo
	{
		FName	PackageName;
		FString	Filename;
		FIoChunkId ChunkId;
	};

	/** Write separate files written by UObjects during cooking via UObject::CookAdditionalFiles. */
	virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) = 0;

	struct FLinkerAdditionalDataInfo
	{
		FName PackageName;
	};
	/** Write separate data written by UObjects via FLinkerSave::AdditionalDataToAppend.

		This function will not be called unless IsLinkerAdditionalDataInSeparateArchive returned true.
		If that function is false, the data was inlined into the PackageData instead.
	*/
	virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) = 0;

	struct FCookInfo
	{
		enum ECookMode
		{
			CookByTheBookMode,
			CookOnTheFlyMode
		};

		ECookMode CookMode = CookByTheBookMode;
	};

	/** Signal the start of a cooking pass

		Package data may only be produced after BeginCook() has been called and
		before EndCook() is called
	  */
	virtual void BeginCook(const FCookInfo& Info) = 0;

	/** Signal the end of a cooking pass.
	  */
	virtual void EndCook() = 0;

	/**
	 * Returns all cooked package store entries.
	 */
	virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&&) = 0;

	/**
	 * Package commit event arguments
	 */
	struct FCommitEventArgs
	{
		FName PlatformName;
		FName PackageName;
		int32 EntryIndex = INDEX_NONE;
		TArrayView<const FPackageStoreEntryResource> Entries;
		TArray<FAdditionalFileInfo> AdditionalFiles;
	};

	/**
	 * Broadcasted after a package has been committed, i.e cooked.
	 */
	DECLARE_EVENT_OneParam(IPackageStoreWriter, FCommitEvent, const FCommitEventArgs&);
	virtual FCommitEvent& OnCommit() = 0;

	/**
	 * Flush any outstanding writes.
	 */
	virtual void Flush() = 0;

	struct FCookedPackageInfo
	{
		FName PackageName;
		FMD5Hash Hash;
		FGuid PackageGuid;
		int64 DiskSize = -1;
		FIoHash TargetDomainDependencies;
	};

	/**
	 * Returns a list of cooked package(s).
	 */
	virtual void GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages) = 0;

	/**
	 * Returns the TargetDomainDependencies that were previously commited for the given PackageName.
	 * Returns an empty object if not found.
	 */
	virtual FCbObject GetTargetDomainDependencies(FName PackageName) = 0;

	/**
	 * Remove cooked package(s) that has been modified since the last cook.
	 */
	virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) = 0;
};

static inline const ANSICHAR* LexToString(IPackageStoreWriter::FBulkDataInfo::EType Value)
{
	switch (Value)
	{
	case IPackageStoreWriter::FBulkDataInfo::Standard:
		return "Standard";
	case IPackageStoreWriter::FBulkDataInfo::Mmap:
		return "Mmap";
	case IPackageStoreWriter::FBulkDataInfo::Optional:
		return "Optional";
	}

	return "Unknown";
}
