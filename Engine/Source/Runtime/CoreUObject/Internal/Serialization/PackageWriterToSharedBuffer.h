// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Memory/SharedBuffer.h"
#include "Misc/Optional.h"
#include "Serialization/PackageWriter.h"

class FPackageWriterRecords
{
public:
	COREUOBJECT_API void BeginPackage(const IPackageWriter::FBeginPackageInfo& Info);
	COREUOBJECT_API void WritePackageData(const IPackageWriter::FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive,
		const TArray<FFileRegion>& FileRegions);
	COREUOBJECT_API void WriteBulkData(const IPackageWriter::FBulkDataInfo& Info, const FIoBuffer& BulkData,
		const TArray<FFileRegion>& FileRegions);
	COREUOBJECT_API void WriteAdditionalFile(const IPackageWriter::FAdditionalFileInfo& Info,
		const FIoBuffer& FileData);
	COREUOBJECT_API void WriteLinkerAdditionalData(const IPackageWriter::FLinkerAdditionalDataInfo& Info,
		const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions);

	struct FPackage
	{
		IPackageWriter::FPackageInfo Info;
		FSharedBuffer Buffer;
		TArray<FFileRegion> Regions;
	};
	struct FBulkData
	{
		IPackageWriter::FBulkDataInfo Info;
		FSharedBuffer Buffer;
		TArray<FFileRegion> Regions;
	};
	struct FAdditionalFile
	{
		IPackageWriter::FAdditionalFileInfo Info;
		FSharedBuffer Buffer;
	};
	struct FLinkerAdditionalData
	{
		IPackageWriter::FLinkerAdditionalDataInfo Info;
		FSharedBuffer Buffer;
		TArray<FFileRegion> Regions;
	};

	/** Called at the end of Commit to clear all records and prepare for the next BeginPackage */
	COREUOBJECT_API void ResetPackage();
	/** Verify that the PackageName coming into a Write function matches the PackageName from BeginPackage */
	COREUOBJECT_API void ValidatePackageName(FName InPackageName);
	/** Verify records from all Write functions are valid, and the required ones are present */
	COREUOBJECT_API void ValidateCommit(const IPackageWriter::FCommitPackageInfo& Info);

	/** Always valid during CommitPackageInternal */
	TOptional<IPackageWriter::FBeginPackageInfo> Begin;
	/** Always valid during CommitPackageInternal if Info.bSucceeded */
	TOptional<FPackage> Package;
	TArray<FBulkData> BulkDatas;
	TArray<FAdditionalFile> AdditionalFiles;
	TArray<FLinkerAdditionalData>  LinkerAdditionalDatas;
};

/**
 * A base class for IPackageWriter subclasses that writes to records that are read in CommitPackage.
 * To avoid diamond inheritance, this class specifies the interface it is implementing (either IPackageWriter
 * or ICookedPackageWriter) by template. Subclasses should derive from one of
 *     TPackageWriterToSharedBuffer<IPackageWriter>
 *     TPackageWriterToSharedBuffer<ICookedPackageWriter>.
 */
template <typename BaseInterface>
class TPackageWriterToSharedBuffer : public BaseInterface
{
public:
	using FPackageRecord = FPackageWriterRecords::FPackage;
	using FBulkDataRecord = FPackageWriterRecords::FBulkData;
	using FAdditionalFileRecord = FPackageWriterRecords::FAdditionalFile;
	using FLinkerAdditionalDataRecord = FPackageWriterRecords::FLinkerAdditionalData;

	virtual void BeginPackage(const IPackageWriter::FBeginPackageInfo& Info) override
	{
		Records.BeginPackage(Info);
	}
	virtual void WritePackageData(const IPackageWriter::FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive,
		const TArray<FFileRegion>& FileRegions) override
	{
		Records.WritePackageData(Info, ExportsArchive, FileRegions);
	}
	virtual void WriteBulkData(const IPackageWriter::FBulkDataInfo& Info, const FIoBuffer& BulkData,
		const TArray<FFileRegion>& FileRegions) override
	{
		Records.WriteBulkData(Info, BulkData, FileRegions);
	}
	virtual void WriteAdditionalFile(const IPackageWriter::FAdditionalFileInfo& Info,
		const FIoBuffer& FileData) override
	{
		Records.WriteAdditionalFile(Info, FileData);
	}
	virtual void WriteLinkerAdditionalData(const IPackageWriter::FLinkerAdditionalDataInfo& Info,
		const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override
	{
		Records.WriteLinkerAdditionalData(Info, Data, FileRegions);
	}
	virtual TFuture<FMD5Hash> CommitPackage(IPackageWriter::FCommitPackageInfo&& Info) override
	{
		ValidateCommit(Info);
		TFuture<FMD5Hash> CookedHash = CommitPackageInternal(Info);
		ResetPackage();
		return CookedHash;
	}

protected:
	virtual TFuture<FMD5Hash> CommitPackageInternal(const IPackageWriter::FCommitPackageInfo& Info) = 0;

protected:
	/** Called at the end of Commit to clear all records and prepare for the next BeginPackage */
	virtual void ResetPackage()
	{
		Records.ResetPackage();
	}
	/** Verify that the PackageName coming into a Write function matches the PackageName from BeginPackage */
	void ValidatePackageName(FName InPackageName)
	{
		Records.ValidatePackageName(InPackageName);
	}
	/** Verify records from all Write functions are valid, and the required ones are present */
	void ValidateCommit(const IPackageWriter::FCommitPackageInfo& Info)
	{
		Records.ValidateCommit(Info);
	}

	FPackageWriterRecords Records;
};

