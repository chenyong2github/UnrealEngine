// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageStoreWriter.h"
#include "PackageStoreOptimizer.h"
#include "Containers/Map.h"
#include "Serialization/LargeMemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPackageStoreWriter, Log, All);

FPackageStoreWriter::FPackageStoreWriter(const FString& InOutputPath, const ITargetPlatform* InTargetPlatform)
	: PackageStoreOptimizer(new FPackageStoreOptimizer())
	, IoStoreWriterContext(new FIoStoreWriterContext())
	, IoStoreWriter(new FIoStoreWriter(*(InOutputPath / TEXT("global"))))

{
	FIoStoreWriterSettings WriterSettings;
	WriterSettings.CompressionMethod = NAME_Zlib;
	WriterSettings.InitializePlatformSpecificSettings(InTargetPlatform);
	FIoStatus IoStatus = IoStoreWriterContext->Initialize(WriterSettings);
	check(IoStatus.IsOk());
	FIoContainerSettings ContainerSettings;
	ContainerSettings.ContainerId = ContainerId;
	ContainerSettings.ContainerFlags |= EIoContainerFlags::Compressed;
	ContainerSettings.ContainerFlags |= EIoContainerFlags::Indexed;
	IoStatus = IoStoreWriter->Initialize(*IoStoreWriterContext, ContainerSettings);
	check(IoStatus.IsOk());
	PackageStoreOptimizer->Initialize(InTargetPlatform);
	PackageStoreOptimizer->EnableIncrementalResolve();
}

FPackageStoreWriter::~FPackageStoreWriter()
{
}

void FPackageStoreWriter::Flush(bool bAllowMissingImports)
{
	PackageStoreOptimizer->Flush(bAllowMissingImports, [this](FPackageStorePackage* ResolvedPackage)
	{
		FPendingPackage* FindPendingPackage = PendingPackagesMap.Find(ResolvedPackage);
		check(FindPendingPackage);
		FIoBuffer PackageBuffer = PackageStoreOptimizer->CreatePackageBuffer(ResolvedPackage, FindPendingPackage->CookedExportsBuffer, &FindPendingPackage->FileRegions);
		FIoWriteOptions WriteOptions;
		WriteOptions.FileName = FindPendingPackage->FileName;
		FIoChunkId ChunkId = CreateIoChunkId(ResolvedPackage->GetId().Value(), 0, EIoChunkType::ExportBundleData);
		IoStoreWriter->Append(ChunkId, PackageBuffer, WriteOptions);
		PendingPackagesMap.Remove(ResolvedPackage);
		CompletedPackages.Add(PackageStoreOptimizer->CreateContainerHeaderEntry(ResolvedPackage));
		delete ResolvedPackage;
	});
}

void FPackageStoreWriter::WritePackage(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WritePackage);
	FIoBuffer HeaderBuffer = FIoBuffer(PackageData.Data(), Info.HeaderSize, PackageData);
	FPackageStorePackage* Package = PackageStoreOptimizer->CreatePackageFromCookedHeader(Info.PackageName, HeaderBuffer);

	FPendingPackage PendingPackage = {
		FIoBuffer(PackageData.Data() + Info.HeaderSize, PackageData.DataSize() - Info.HeaderSize, PackageData),
		Info.LooseFilePath,
		FileRegions
	};

	PendingPackagesMap.Emplace(Package, PendingPackage);
	PackageStoreOptimizer->BeginResolvePackage(Package);
	Flush(false);
}

void FPackageStoreWriter::WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WriteBulkdata);
	FIoWriteOptions WriteOptions;
	WriteOptions.FileName = Info.LooseFilePath;
	FPackageId PackageId = FPackageId::FromName(Info.PackageName);
	FIoChunkId ChunkId;
	if (Info.BulkdataType == FBulkDataInfo::Optional)
	{
		ChunkId = CreateIoChunkId(PackageId.Value(), 0, EIoChunkType::OptionalBulkData);
	}
	else if (Info.BulkdataType == FBulkDataInfo::Mmap)
	{
		WriteOptions.bForceUncompressed = true;
		WriteOptions.bIsMemoryMapped = true;
		ChunkId = CreateIoChunkId(PackageId.Value(), 0, EIoChunkType::MemoryMappedBulkData);
	}
	else
	{
		ChunkId = CreateIoChunkId(PackageId.Value(), 0, EIoChunkType::BulkData);
	}
	IoStoreWriter->Append(ChunkId, BulkData, WriteOptions);
}

void FPackageStoreWriter::Finalize()
{
	Flush(true);

	PackageStoreOptimizer->WriteScriptObjects(IoStoreWriter.Get());

	FContainerHeader Header = PackageStoreOptimizer->CreateContainerHeader(ContainerId, CompletedPackages);
	FLargeMemoryWriter HeaderAr(0, true);
	HeaderAr << Header;
	int64 DataSize = HeaderAr.TotalSize();
	FIoBuffer HeaderBuffer(FIoBuffer::AssumeOwnership, HeaderAr.ReleaseOwnership(), DataSize);
	FIoWriteOptions WriteOptions;
	WriteOptions.DebugName = TEXT("ContainerHeader");
	IoStoreWriter->Append(CreateIoChunkId(ContainerId.Value(), 0, EIoChunkType::ContainerHeader), HeaderBuffer, WriteOptions);

	TIoStatusOr<FIoStoreWriterResult> Result = IoStoreWriter->Flush();
	check(Result.IsOk());

	UE_LOG(LogPackageStoreWriter, Display, TEXT("Input:\t%d Packages"), PackageStoreOptimizer->GetTotalPackageCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Export bundles"), PackageStoreOptimizer->GetTotalExportBundleCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Export bundle entries"), PackageStoreOptimizer->GetTotalExportBundleEntryCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Export bundle arcs"), PackageStoreOptimizer->GetTotalExportBundleArcCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Public runtime script objects"), PackageStoreOptimizer->GetTotalScriptObjectCount());
}
