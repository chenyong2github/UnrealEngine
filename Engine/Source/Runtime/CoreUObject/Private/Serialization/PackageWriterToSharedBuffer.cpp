// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/PackageWriterToSharedBuffer.h"

#include "Serialization/LargeMemoryWriter.h"

TUniquePtr<FLargeMemoryWriter> IPackageWriter::CreateLinkerArchive(FName PackageName, UObject* Asset)
{
	// The LargeMemoryWriter does not need to be persistent; the LinkerSave wraps it and reports Persistent=true
	bool bIsPersistent = false; 
	return TUniquePtr<FLargeMemoryWriter>(new FLargeMemoryWriter(0, bIsPersistent, *PackageName.ToString()));
}

static FSharedBuffer IoBufferToSharedBuffer(const FIoBuffer& InBuffer)
{
	InBuffer.EnsureOwned();
	const uint64 DataSize = InBuffer.DataSize();
	FIoBuffer MutableBuffer(InBuffer);
	uint8* DataPtr = MutableBuffer.Release().ValueOrDie();
	return FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free);
};

void FPackageWriterRecords::BeginPackage(const IPackageWriter::FBeginPackageInfo& Info)
{
	checkf(!Begin.IsSet(),
		TEXT("IPackageWriter->BeginPackage must not be called twice without calling CommitPackage."));
	Begin.Emplace(Info);
}

void FPackageWriterRecords::WritePackageData(const IPackageWriter::FPackageInfo& Info,
	FLargeMemoryWriter& ExportsArchive, const TArray<FFileRegion>& FileRegions)
{
	ValidatePackageName(Info.PackageName);
	int64 DataSize= ExportsArchive.TotalSize();
	checkf(DataSize > 0, TEXT("IPackageWriter->WritePackageData must not be called with an empty ExportsArchive"));
	checkf(static_cast<uint64>(DataSize) >= Info.HeaderSize,
		TEXT("IPackageWriter->WritePackageData must not be called with HeaderSize > ExportsArchive.TotalSize"));
	FSharedBuffer Buffer = FSharedBuffer::TakeOwnership(ExportsArchive.ReleaseOwnership(), DataSize,
		FMemory::Free);
	Package = FPackage{ Info, MoveTemp(Buffer), FileRegions };
}

void FPackageWriterRecords::WriteBulkData(const IPackageWriter::FBulkDataInfo& Info, const FIoBuffer& BulkData,
	const TArray<FFileRegion>& FileRegions)
{
	ValidatePackageName(Info.PackageName);
	BulkDatas.Add(FBulkData{ Info, IoBufferToSharedBuffer(BulkData), FileRegions });
}

void FPackageWriterRecords::WriteAdditionalFile(const IPackageWriter::FAdditionalFileInfo& Info,
	const FIoBuffer& FileData)
{
	ValidatePackageName(Info.PackageName);
	AdditionalFiles.Add(FAdditionalFile{ Info, IoBufferToSharedBuffer(FileData) });
}

void FPackageWriterRecords::WriteLinkerAdditionalData(const IPackageWriter::FLinkerAdditionalDataInfo& Info,
	const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	ValidatePackageName(Info.PackageName);
	LinkerAdditionalDatas.Add(
		FLinkerAdditionalData{ Info, IoBufferToSharedBuffer(Data), FileRegions });
}

void FPackageWriterRecords::ResetPackage()
{
	Begin.Reset();
	Package.Reset();
	BulkDatas.Reset();
	AdditionalFiles.Reset();
	LinkerAdditionalDatas.Reset();
}

void FPackageWriterRecords::ValidatePackageName(FName InPackageName)
{
	checkf(Begin.IsSet(),
		TEXT("IPackageWriter->BeginPackage must be called before any other functions on IPackageWriter"));
	checkf(Begin->PackageName == InPackageName, 
		TEXT("IPackageWriter must receive the same PackageName in all calls between Begin and Commit."));
}

void FPackageWriterRecords::ValidateCommit(const IPackageWriter::FCommitPackageInfo& Info)
{
	ValidatePackageName(Info.PackageName);
	checkf(Info.bSucceeded == false || Package.IsSet(),
		TEXT("IPackageWriter->WritePackageData must be called before Commit if the Package save was successful."));
	bool HasBulkDataType[IPackageWriter::FBulkDataInfo::NumTypes]{};
	for (FBulkData& Record : BulkDatas)
	{
		checkf(!HasBulkDataType[(int32)Record.Info.BulkDataType],
			TEXT("IPackageWriter->WriteBulkData must not be called with more than one BulkData of the same type."));
		HasBulkDataType[(int32)Record.Info.BulkDataType] = true;
	}
}