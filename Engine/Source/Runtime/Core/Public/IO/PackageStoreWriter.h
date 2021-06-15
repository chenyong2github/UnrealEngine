// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"

struct FPackageStoreEntryResource;

class IPackageStoreWriter
{
public:
	virtual ~IPackageStoreWriter() = default;

	struct FPackageBaseInfo
	{
		FName	PackageName;
	};

	/** Mark the beginning of a package store transaction for the specified package

		This must be called before any data is produced for a given package
	  */
	virtual void BeginPackage(const FPackageBaseInfo& Info) = 0;

	/** Finalize a package started with BeginPackage()
	  */
	virtual void CommitPackage(const FPackageBaseInfo& Info) = 0;

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

	virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) = 0;

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
