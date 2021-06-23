// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilePackageStoreWriter.h"
#include "PackageStoreOptimizer.h"
#include "Containers/Map.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Interfaces/ITargetPlatform.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogPackageStoreWriter, Log, All);

void WriteFileRegions(const TCHAR* Filename, const TArray<FFileRegion>& FileRegions)
{
	if (FileRegions.Num() > 0)
	{
		TArray<FFileRegion> FileRegionsCopy(FileRegions);
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(Filename));
		FFileRegion::SerializeFileRegions(*Ar, FileRegionsCopy);
	}
}

void FPackageStoreManifest::BeginPackage(FName PackageName)
{
	FScopeLock Lock(&CriticalSection);
	FPackageInfo& PackageInfo = PackageInfoByNameMap.FindOrAdd(PackageName);
	PackageInfo.PackageName = PackageName;
	if (PackageInfo.PackageChunkId.IsValid())
	{
		FileNameByChunkIdMap.Remove(PackageInfo.PackageChunkId);
		PackageInfo.PackageChunkId = FIoChunkId();
	}
	for (const FIoChunkId& BulkDataChunkId : PackageInfo.BulkDataChunkIds)
	{
		FileNameByChunkIdMap.Remove(BulkDataChunkId);
	}
	PackageInfo.BulkDataChunkIds.Empty();
}

void FPackageStoreManifest::AddPackageData(FName PackageName, const FString& FileName, const FIoChunkId& ChunkId)
{
	FScopeLock Lock(&CriticalSection);
	FPackageInfo* PackageInfo = PackageInfoByNameMap.Find(PackageName);
	check(PackageInfo);
	PackageInfo->PackageChunkId = ChunkId;
	if (!FileName.IsEmpty())
	{
		FileNameByChunkIdMap.Add(ChunkId, FileName);
	}
}

void FPackageStoreManifest::AddBulkData(FName PackageName, const FString& FileName, const FIoChunkId& ChunkId)
{
	FScopeLock Lock(&CriticalSection);
	FPackageInfo* PackageInfo = PackageInfoByNameMap.Find(PackageName);
	check(PackageInfo);
	PackageInfo->BulkDataChunkIds.Add(ChunkId);
	if (!FileName.IsEmpty())
	{
		FileNameByChunkIdMap.Add(ChunkId, FileName);
	}
}

FIoStatus FPackageStoreManifest::Save(const TCHAR* Filename) const
{
	FScopeLock Lock(&CriticalSection);
	TStringBuilder<64> ChunkIdStringBuilder;
	auto ChunkIdToString = [&ChunkIdStringBuilder](const FIoChunkId& ChunkId)
	{
		ChunkIdStringBuilder.Reset();
		ChunkIdStringBuilder << ChunkId;
		return *ChunkIdStringBuilder;
	};

	FString JsonTcharText;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonTcharText);
	Writer->WriteObjectStart();

	if (ZenServerInfo)
	{
		Writer->WriteObjectStart(TEXT("ZenServer"));
		Writer->WriteValue(TEXT("HostName"), ZenServerInfo->HostName);
		Writer->WriteValue(TEXT("Port"), ZenServerInfo->Port);
		Writer->WriteValue(TEXT("ProjectId"), ZenServerInfo->ProjectId);
		Writer->WriteValue(TEXT("OplogId"), ZenServerInfo->OplogId);
		Writer->WriteObjectEnd();
	}
	
	Writer->WriteArrayStart(TEXT("Files"));
	for (const auto& KV : FileNameByChunkIdMap)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("Path"), KV.Value);
		Writer->WriteValue(TEXT("ChunkId"), ChunkIdToString(KV.Key));
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("Packages"));
	for (const auto& KV : PackageInfoByNameMap)
	{
		const FPackageInfo& PackageInfo = KV.Value;
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("Name"), PackageInfo.PackageName.ToString());
		Writer->WriteValue(TEXT("PackageChunkId"), ChunkIdToString(PackageInfo.PackageChunkId));
		if (!PackageInfo.BulkDataChunkIds.IsEmpty())
		{
			Writer->WriteArrayStart(TEXT("BulkDataChunkIds"));
			for (const FIoChunkId& ChunkId : PackageInfo.BulkDataChunkIds)
			{
				Writer->WriteValue(ChunkIdToString(ChunkId));
			}
			Writer->WriteArrayEnd();
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	if (!FFileHelper::SaveStringToFile(JsonTcharText, Filename))
	{
		return FIoStatus(EIoErrorCode::FileOpenFailed);
	}

	return FIoStatus::Ok;
}

FIoStatus FPackageStoreManifest::Load(const TCHAR* Filename)
{
	FScopeLock Lock(&CriticalSection);
	PackageInfoByNameMap.Empty();
	FileNameByChunkIdMap.Empty();

	auto ChunkIdFromString = [](const FString& ChunkIdString)
	{
		FStringView ChunkIdStringView(*ChunkIdString, 24);
		uint8 Data[12];
		UE::String::HexToBytes(ChunkIdStringView, Data);
		FIoChunkId ChunkId;
		ChunkId.Set(Data, 12);
		return ChunkId;
	};

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, Filename))
	{
		return FIoStatus(EIoErrorCode::FileOpenFailed);
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return FIoStatus(EIoErrorCode::Unknown);
	}

	TSharedPtr<FJsonValue> ZenServerValue = JsonObject->Values.FindRef(TEXT("ZenServer"));
	if (ZenServerValue)
	{
		ZenServerInfo = MakeUnique<FZenServerInfo>();
		TSharedPtr<FJsonObject> ZenServerObject = ZenServerValue->AsObject();
		ZenServerInfo->HostName = ZenServerObject->Values.FindRef(TEXT("HostName"))->AsString();
		ZenServerInfo->Port = uint16(ZenServerObject->Values.FindRef(TEXT("Port"))->AsNumber());
		ZenServerInfo->ProjectId = ZenServerObject->Values.FindRef(TEXT("ProjectId"))->AsString();
		ZenServerInfo->OplogId = ZenServerObject->Values.FindRef(TEXT("OplogId"))->AsString();
	}

	TSharedPtr<FJsonValue> FilesArrayValue = JsonObject->Values.FindRef(TEXT("Files"));
	TArray<TSharedPtr<FJsonValue>> FilesArray = FilesArrayValue->AsArray();
	FileNameByChunkIdMap.Reserve(FilesArray.Num());
	for (const TSharedPtr<FJsonValue>& FileValue : FilesArray)
	{
		TSharedPtr<FJsonObject> FileObject = FileValue->AsObject();
		FIoChunkId ChunkId = ChunkIdFromString(FileObject->Values.FindRef(TEXT("ChunkId"))->AsString());
		FileNameByChunkIdMap.Add(ChunkId, FileObject->Values.FindRef(TEXT("Path"))->AsString());
	}

	TArray<TSharedPtr<FJsonValue>> PackagesArray = JsonObject->Values.FindRef(TEXT("Packages"))->AsArray();
	PackageInfoByNameMap.Reserve(PackagesArray.Num());
	for (const TSharedPtr<FJsonValue>& PackageValue : PackagesArray)
	{
		TSharedPtr<FJsonObject> PackageObject = PackageValue->AsObject();
		FName PackageName = FName(PackageObject->Values.FindRef(TEXT("Name"))->AsString());

		FPackageInfo& PackageInfo = PackageInfoByNameMap.FindOrAdd(PackageName);
		PackageInfo.PackageName = PackageName;
		PackageInfo.PackageChunkId = ChunkIdFromString(PackageObject->Values.FindRef(TEXT("PackageChunkId"))->AsString());

		TSharedPtr<FJsonValue> BulkDataChunkIdsValue = PackageObject->Values.FindRef(TEXT("BulkDataChunkIds"));
		if (BulkDataChunkIdsValue.IsValid())
		{
			TArray<TSharedPtr<FJsonValue>> BulkDataChunkIdsArray = BulkDataChunkIdsValue->AsArray();
			PackageInfo.BulkDataChunkIds.Reserve(BulkDataChunkIdsArray.Num());
			for (const TSharedPtr<FJsonValue>& BulkDataChunkIdValue : BulkDataChunkIdsArray)
			{
				PackageInfo.BulkDataChunkIds.Add(ChunkIdFromString(BulkDataChunkIdValue->AsString()));
			}
		}
	}

	return FIoStatus::Ok;
}

TArray<FPackageStoreManifest::FFileInfo> FPackageStoreManifest::GetFiles() const
{
	FScopeLock Lock(&CriticalSection);
	TArray<FFileInfo> Files;
	Files.Reserve(FileNameByChunkIdMap.Num());
	for (const auto& KV : FileNameByChunkIdMap)
	{
		Files.Add({ KV.Value, KV.Key });
	}
	return Files;
}

TArray<FPackageStoreManifest::FPackageInfo> FPackageStoreManifest::GetPackages() const
{
	FScopeLock Lock(&CriticalSection);
	TArray<FPackageInfo> Packages;
	PackageInfoByNameMap.GenerateValueArray(Packages);
	return Packages;
}

FPackageStoreManifest::FZenServerInfo& FPackageStoreManifest::EditZenServerInfo()
{
	FScopeLock Lock(&CriticalSection);
	if (!ZenServerInfo)
	{
		ZenServerInfo = MakeUnique<FZenServerInfo>();
	}
	return *ZenServerInfo;
}

const FPackageStoreManifest::FZenServerInfo* FPackageStoreManifest::ReadZenServerInfo() const
{
	FScopeLock Lock(&CriticalSection);
	return ZenServerInfo.Get();
}

FFilePackageStoreWriter::FFilePackageStoreWriter(const FString& InOutputPath, const FString& InMetadataDirectoryPath, const ITargetPlatform* InTargetPlatform)
	: PackageStoreOptimizer(new FPackageStoreOptimizer())
	, IoStoreWriterContext(new FIoStoreWriterContext())
	, IoStoreWriter(new FIoStoreWriter(*(InOutputPath / TEXT("global"))))
	, TargetPlatform(*InTargetPlatform)
	, OutputPath(InOutputPath)
	, MetadataDirectoryPath(InMetadataDirectoryPath)

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
}

FFilePackageStoreWriter::~FFilePackageStoreWriter()
{
}

void FFilePackageStoreWriter::WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WritePackageData);
	FIoBuffer CookedHeaderBuffer = FIoBuffer(PackageData.Data(), Info.HeaderSize, PackageData);
	FIoBuffer CookedExportsBuffer = FIoBuffer(PackageData.Data() + Info.HeaderSize, PackageData.DataSize() - Info.HeaderSize, PackageData);
	FPackageStorePackage* Package = PackageStoreOptimizer->CreatePackageFromCookedHeader(Info.PackageName, CookedHeaderBuffer);
	PackageStoreOptimizer->FinalizePackage(Package);
	TArray<FFileRegion> FileRegionsCopy(FileRegions);
	for (FFileRegion& Region : FileRegionsCopy)
	{
		// Adjust regions so they are relative to the start of the exports buffer
		Region.Offset -= Info.HeaderSize;
	}
	FIoBuffer PackageBuffer = PackageStoreOptimizer->CreatePackageBuffer(Package, CookedExportsBuffer, &FileRegionsCopy);
	FIoWriteOptions WriteOptions;
	WriteOptions.FileName = Info.LooseFilePath;
	IoStoreWriter->Append(Info.ChunkId, PackageBuffer, WriteOptions);
	PackageStoreManifest.AddPackageData(Info.PackageName, Info.LooseFilePath, Info.ChunkId);
	PackageStoreEntries.Add(PackageStoreOptimizer->CreatePackageStoreEntry(Package));
	for (FFileRegion& Region : FileRegionsCopy)
	{
		// Adjust regions once more so they are relative to the exports bundle buffer
		Region.Offset -= Package->GetHeaderSize();
	}
	WriteFileRegions(*FPaths::ChangeExtension(Info.LooseFilePath, FString(".uexp") + FFileRegion::RegionsFileExtension), FileRegionsCopy);
	delete Package;
}

void FFilePackageStoreWriter::WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WriteBulkdata);
	FIoWriteOptions WriteOptions;
	WriteOptions.FileName = Info.LooseFilePath;
	FPackageId PackageId = FPackageId::FromName(Info.PackageName);
	if (Info.BulkdataType == FBulkDataInfo::Mmap)
	{
		WriteOptions.bForceUncompressed = true;
		WriteOptions.bIsMemoryMapped = true;
	}
	IoStoreWriter->Append(Info.ChunkId, BulkData, WriteOptions);
	PackageStoreManifest.AddBulkData(Info.PackageName, Info.LooseFilePath, Info.ChunkId);
	WriteFileRegions(*(Info.LooseFilePath + FFileRegion::RegionsFileExtension), FileRegions);
}

void FFilePackageStoreWriter::BeginCook(const FCookInfo& Info)
{
	check(Info.CookMode == FCookInfo::CookByTheBookMode);
}

void FFilePackageStoreWriter::EndCook()
{
	FIoBuffer ScriptObjectsBuffer = PackageStoreOptimizer->CreateScriptObjectsBuffer();
	FIoWriteOptions WriteOptions;
	WriteOptions.DebugName = TEXT("ScriptObjects");
	IoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects), ScriptObjectsBuffer, WriteOptions);

	FContainerHeader Header = PackageStoreOptimizer->CreateContainerHeader(ContainerId, PackageStoreEntries);
	FLargeMemoryWriter HeaderAr(0, true);
	HeaderAr << Header;
	int64 DataSize = HeaderAr.TotalSize();
	FIoBuffer HeaderBuffer(FIoBuffer::AssumeOwnership, HeaderAr.ReleaseOwnership(), DataSize);
	WriteOptions.DebugName = TEXT("ContainerHeader");
	IoStoreWriter->Append(CreateIoChunkId(ContainerId.Value(), 0, EIoChunkType::ContainerHeader), HeaderBuffer, WriteOptions);

	TIoStatusOr<FIoStoreWriterResult> Result = IoStoreWriter->Flush();
	check(Result.IsOk());

	PackageStoreManifest.Save(*(MetadataDirectoryPath / TEXT("packagestore.manifest")));

	UE_LOG(LogPackageStoreWriter, Display, TEXT("Input:\t%d Packages"), PackageStoreOptimizer->GetTotalPackageCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Export bundles"), PackageStoreOptimizer->GetTotalExportBundleCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Export bundle entries"), PackageStoreOptimizer->GetTotalExportBundleEntryCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Internal export bundle arcs"), PackageStoreOptimizer->GetTotalInternalBundleArcsCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d External export bundle arcs"), PackageStoreOptimizer->GetTotalExternalBundleArcsCount());
	UE_LOG(LogPackageStoreWriter, Display, TEXT("Output:\t%d Public runtime script objects"), PackageStoreOptimizer->GetTotalScriptObjectCount());
}

void FFilePackageStoreWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	PackageStoreManifest.BeginPackage(Info.PackageName);
}

void FFilePackageStoreWriter::CommitPackage(const FCommitPackageInfo& Info)
{
}
