// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenStoreWriter.h"
#include "ZenStoreHttpClient.h"
#include "ZenFileSystemManifest.h"
#include "PackageStoreOptimizer.h"

#include "Algo/BinarySearch.h"
#include "Algo/IsSorted.h"
#include "Algo/Sort.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "Async/Async.h"
#include "Compression/CompressedBuffer.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/LargeMemoryWriter.h" 
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoDispatcher.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/StringBuilder.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogZenStoreWriter, Log, All);

using namespace UE;

// Note that this is destructive - we yank out the buffer memory from the 
// IoBuffer into the FSharedBuffer
FSharedBuffer IoBufferToSharedBuffer(FIoBuffer& InBuffer)
{
	InBuffer.EnsureOwned();
	const uint64 DataSize = InBuffer.DataSize();
	uint8* DataPtr = InBuffer.Release().ValueOrDie();
	return FSharedBuffer{ FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free) };
};

FCbObjectId ToObjectId(const FIoChunkId& ChunkId)
{
	return FCbObjectId(MakeMemoryView(ChunkId.GetData(), ChunkId.GetSize()));
}

FMD5Hash IoHashToMD5(const FIoHash& IoHash)
{
	const FIoHash::ByteArray& Bytes = IoHash.GetBytes();
	
	FMD5 MD5Gen;
	MD5Gen.Update(Bytes, sizeof(FIoHash::ByteArray));
	
	FMD5Hash Hash;
	Hash.Set(MD5Gen);

	return Hash;
}

class FZenStoreWriter::FZenStoreHttpQueue
{
public:

	struct FHttpRequestResult
	{
		double DurationInSeconds = 0.0;
		uint64 TotalBytes = 0;
		bool bOk = false;
	};

	using FHttpRequestCompleted = TFunction<void(FHttpRequestResult)>;

	FZenStoreHttpQueue(UE::FZenStoreHttpClient& InHttpClient)
		: HttpClient(InHttpClient)
		, WakeUpEvent(FPlatformProcess::GetSynchEventFromPool())
		, QueueEmptyEvent(FPlatformProcess::GetSynchEventFromPool(true))
	{
		BackgroundThread = AsyncThread([this]()
		{ 
			BackgroundThreadEntry();
		});
	}

	~FZenStoreHttpQueue()
	{
		Shutdown();
		FPlatformProcess::ReturnSynchEventToPool(WakeUpEvent);
		FPlatformProcess::ReturnSynchEventToPool(QueueEmptyEvent);
	}

	void Enqueue(FCbPackage OpEntry, FHttpRequestCompleted&& Callback)
	{
		FHttpRequest* Request = Alloc();

		Request->OpEntry	= MoveTemp(OpEntry);
		Request->Callback	= MoveTemp(Callback);			
		Request->StartTime	= FPlatformTime::Seconds();

		{
			FScopeLock _(&QueueCriticalSection);

			if (!LastPendingHttpRequest)
			{
				check(!NextPendingHttpRequest);
				NextPendingHttpRequest = LastPendingHttpRequest = Request;
			}
			else
			{
				LastPendingHttpRequest->Next = Request;
				LastPendingHttpRequest = Request;
			}
		}

		WakeUpEvent->Trigger();
	}

	void Flush()
	{
		QueueEmptyEvent->Reset();
		WakeUpEvent->Trigger();
		QueueEmptyEvent->Wait();
	}

private:

	struct FHttpRequest
	{
		FCbPackage OpEntry;
		FHttpRequestCompleted Callback;
		FHttpRequest* Next = nullptr;
		double StartTime = 0;
	};

	FHttpRequest* Alloc()
	{
		FScopeLock _(&RequestsCriticalSection);

		FHttpRequest* Request = nullptr;

		if (FirstFreeHttpRequest)
		{
			Request = FirstFreeHttpRequest;
			FirstFreeHttpRequest = Request->Next;
			Request->Next = nullptr;
		}
		else
		{
			const int32 Index = HttpRequests.Add();
			Request = &HttpRequests[Index];
		}

		check(!Request->Next);

		return Request;
	}

	void Free(FHttpRequest* Request)
	{
		check(!Request->Next);

		FScopeLock _(&RequestsCriticalSection);

		Request->Next = FirstFreeHttpRequest;
		FirstFreeHttpRequest = Request;
	}

	void BackgroundThreadEntry()
	{
		while (!bStop.Load())
		{
			FHttpRequest* PendingRequests = nullptr;
			{
				FScopeLock _(&QueueCriticalSection);
				PendingRequests = NextPendingHttpRequest;
				NextPendingHttpRequest = LastPendingHttpRequest = nullptr;
			}

			if (PendingRequests)
			{
				FHttpRequest* Request = PendingRequests;
				while (Request)
				{
					TIoStatusOr<uint64> Status = HttpClient.AppendOp(MoveTemp(Request->OpEntry));
			
					FHttpRequestResult HttpResult
					{
						FPlatformTime::Seconds() - Request->StartTime,
						Status.IsOk() ? Status.ValueOrDie() : 0,
						Status.IsOk()
					};

					Request->Callback(HttpResult);

					FHttpRequest* FreeRequest = Request;
					Request = Request->Next;
					FreeRequest->Next = nullptr;
					Free(FreeRequest);
				}
			}
			else if (!bStop)
			{
				QueueEmptyEvent->Trigger();
				WakeUpEvent->Wait();
			}
		}
	}

	void Shutdown()
	{
		if (!bStop)
		{
			bStop = true;
			WakeUpEvent->Trigger();
			BackgroundThread.Wait();
		}
	}

	UE::FZenStoreHttpClient& HttpClient;
	FCriticalSection QueueCriticalSection;
	FEvent* WakeUpEvent;
	FEvent* QueueEmptyEvent;
	TFuture<void> BackgroundThread;
	TAtomic<bool> bStop {false};

	FCriticalSection RequestsCriticalSection;
	TChunkedArray<FHttpRequest> HttpRequests;
	FHttpRequest* NextPendingHttpRequest = nullptr;
	FHttpRequest* LastPendingHttpRequest = nullptr;
	FHttpRequest* FirstFreeHttpRequest = nullptr;
};

TArray<const UTF8CHAR*> FZenStoreWriter::ReservedOplogKeys;

void FZenStoreWriter::StaticInit()
{
	if (ReservedOplogKeys.Num() > 0)
	{
		return;
	}

	ReservedOplogKeys.Append({ UTF8TEXT("files"), UTF8TEXT("key"), UTF8TEXT("package"), UTF8TEXT("packagestoreentry") });
	Algo::Sort(ReservedOplogKeys, [](const UTF8CHAR* A, const UTF8CHAR* B)
		{
			return FUtf8StringView(A).Compare(FUtf8StringView(B), ESearchCase::IgnoreCase) < 0;
		});;
}

FZenStoreWriter::FZenStoreWriter(
	const FString& InOutputPath, 
	const FString& InMetadataDirectoryPath, 
	const ITargetPlatform* InTargetPlatform
)
	: TargetPlatform(*InTargetPlatform)
	, OutputPath(InOutputPath)
	, MetadataDirectoryPath(InMetadataDirectoryPath)
	, PackageStoreManifest(InOutputPath)
	, PackageStoreOptimizer(new FPackageStoreOptimizer())
	, CookMode(ICookedPackageWriter::FCookInfo::CookByTheBookMode)
	, bInitialized(false)
{
	StaticInit();

	FString ProjectId = FApp::GetProjectName();
	FString OplogId = InTargetPlatform->PlatformName();

	HttpClient = MakeUnique<UE::FZenStoreHttpClient>();

	FString RootDir = FPaths::RootDir();
	FString EngineDir = FPaths::EngineDir();
	FPaths::NormalizeDirectoryName(EngineDir);
	FString ProjectDir = FPaths::ProjectDir();
	FPaths::NormalizeDirectoryName(ProjectDir);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString AbsServerRoot = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*RootDir);
	FString AbsEngineDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*EngineDir);
	FString AbsProjectDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectDir);

	HttpClient->TryCreateProject(ProjectId, OplogId, AbsServerRoot, AbsEngineDir, AbsProjectDir);

	PackageStoreOptimizer->Initialize();

	FPackageStoreManifest::FZenServerInfo& ZenServerInfo = PackageStoreManifest.EditZenServerInfo();

#if UE_WITH_ZEN
	const UE::Zen::FZenServiceInstance& ZenServiceInstance = HttpClient->GetZenServiceInstance();
	ZenServerInfo.bAutoLaunch = ZenServiceInstance.IsAutoLaunch();
	ZenServerInfo.AutoLaunchExecutablePath = ZenServiceInstance.GetAutoLaunchExecutablePath();
	ZenServerInfo.AutoLaunchArguments = ZenServiceInstance.GetAutoLaunchArguments();
#endif

	ZenServerInfo.HostName = HttpClient->GetHostName();
	ZenServerInfo.Port = HttpClient->GetPort();
	ZenServerInfo.ProjectId = ProjectId;
	ZenServerInfo.OplogId = OplogId;

	ZenFileSystemManifest = MakeUnique<FZenFileSystemManifest>(TargetPlatform, OutputPath);
	
	HttpQueue = MakeUnique<FZenStoreHttpQueue>(*HttpClient);
	
	Compressor = FOodleDataCompression::ECompressor::Mermaid;
	CompressionLevel = FOodleDataCompression::ECompressionLevel::VeryFast;
}

FZenStoreWriter::~FZenStoreWriter()
{
	FWriteScopeLock _(PackagesLock);

	if (PendingPackages.Num())
	{
		UE_LOG(LogZenStoreWriter, Warning, TEXT("Pending packages at shutdown!"));
	}
}

void FZenStoreWriter::WritePackageData(const FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());
	FIoBuffer PackageData(FIoBuffer::AssumeOwnership, ExportsArchive.ReleaseOwnership(), ExportsArchive.TotalSize());

	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::WritePackageData);

	FIoBuffer CookedHeaderBuffer = FIoBuffer(PackageData.Data(), Info.HeaderSize, PackageData);
	FIoBuffer CookedExportsBuffer = FIoBuffer(PackageData.Data() + Info.HeaderSize, PackageData.DataSize() - Info.HeaderSize, PackageData);
	TUniquePtr<FPackageStorePackage> Package{PackageStoreOptimizer->CreatePackageFromCookedHeader(Info.PackageName, CookedHeaderBuffer)};
	PackageStoreOptimizer->FinalizePackage(Package.Get());
	TArray<FFileRegion> FileRegionsCopy(FileRegions);
	for (FFileRegion& Region : FileRegionsCopy)
	{
		// Adjust regions so they are relative to the start of the exports buffer
		Region.Offset -= Info.HeaderSize;
	}
	FIoBuffer PackageBuffer = PackageStoreOptimizer->CreatePackageBuffer(Package.Get(), CookedExportsBuffer, &FileRegionsCopy);
	FIoChunkId ChunkId = CreateIoChunkId(Package->GetId().Value(), 0, EIoChunkType::ExportBundleData);
	PackageStoreManifest.AddPackageData(Info.PackageName, Info.LooseFilePath, Info.ChunkId);
	for (FFileRegion& Region : FileRegionsCopy)
	{
		// Adjust regions once more so they are relative to the exports bundle buffer
		Region.Offset -= Package->GetHeaderSize();
	}
	//WriteFileRegions(*FPaths::ChangeExtension(Info.LooseFilePath, FString(".uexp") + FFileRegion::RegionsFileExtension), FileRegionsCopy);

	// Commit to Zen build store

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	// We do not modify the PendingPackages map, so take a shared lock
	FReadScopeLock _(PackagesLock);

	FPendingPackageState* ExistingState = PendingPackages.Find(Info.PackageName);

	checkf(ExistingState != nullptr, TEXT("WritePackageData called for package which is not pending: '%s'"), *Info.PackageName.ToString());

	FPackageDataEntry& Entry = ExistingState->PackageData;

	Entry.Payload			= PackageBuffer;
	Entry.Info				= Info;
	Entry.ChunkId			= ChunkOid;
	Entry.PackageStoreEntry = PackageStoreOptimizer->CreatePackageStoreEntry(Package.Get());
	Entry.IsValid			= true;
}

void FZenStoreWriter::WriteIoStorePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const FPackageStoreEntryResource& PackageStoreEntry, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());

	TRACE_CPUPROFILER_EVENT_SCOPE(WriteIoStorePackageData);

	PackageStoreManifest.AddPackageData(Info.PackageName, Info.LooseFilePath, Info.ChunkId);
	//WriteFileRegions(*FPaths::ChangeExtension(Info.LooseFilePath, FString(".uexp") + FFileRegion::RegionsFileExtension), FileRegionsCopy);

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	// We do not modify the PendingPackages map, so take a shared lock
	FReadScopeLock _(PackagesLock);

	FPendingPackageState* ExistingState = PendingPackages.Find(Info.PackageName);

	checkf(ExistingState != nullptr, TEXT("WritePackageData called for package which is not pending: '%s'"), *Info.PackageName.ToString());

	FPackageDataEntry& Entry = ExistingState->PackageData;

	Entry.Payload = PackageData;
	Entry.Info = Info;
	Entry.ChunkId = ChunkOid;
	Entry.PackageStoreEntry = PackageStoreEntry;
	Entry.IsValid = true;
}

void FZenStoreWriter::WriteBulkData(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	// We do not modify the PendingPackages map, so take a shared lock
	FReadScopeLock _(PackagesLock);

	FPendingPackageState* ExistingState = PendingPackages.Find(Info.PackageName);

	checkf(ExistingState != nullptr, TEXT("WriteBulkData called for package which is not pending: '%s'"), *Info.PackageName.ToString());

	BulkData.MakeOwned();

	FBulkDataEntry BulkEntry;

	BulkEntry.Payload	= BulkData;
	BulkEntry.Info		= Info;
	BulkEntry.ChunkId	= ChunkOid;
	BulkEntry.IsValid	= true;

	ExistingState->BulkData.Emplace(BulkEntry);

	PackageStoreManifest.AddBulkData(Info.PackageName, Info.LooseFilePath, Info.ChunkId);

	//	WriteFileRegions(*(Info.LooseFilePath + FFileRegion::RegionsFileExtension), FileRegions);
}

void FZenStoreWriter::WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData)
{
	const FZenFileSystemManifest::FManifestEntry& ManifestEntry = ZenFileSystemManifest->CreateManifestEntry(Info.Filename);

	FFileDataEntry FileEntry;

	FileEntry.Payload				= FIoBuffer(FIoBuffer::Clone, FileData.Data(), FileData.DataSize());
	FileEntry.Info					= Info;
	FileEntry.Info.ChunkId			= ManifestEntry.FileChunkId;
	FileEntry.ZenManifestServerPath = ManifestEntry.ServerPath;
	FileEntry.ZenManifestClientPath = ManifestEntry.ClientPath;

	// We do not modify the PendingPackages map, so take a shared lock
	FReadScopeLock _(PackagesLock);

	FPendingPackageState* ExistingState = PendingPackages.Find(Info.PackageName);

	checkf(ExistingState != nullptr, TEXT("WriteAdditionalFile called for package which is not pending: '%s'"), *Info.PackageName.ToString());

	ExistingState->FileData.Add(MoveTemp(FileEntry));
}

void FZenStoreWriter::WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	// LinkerAdditionalData is not yet implemented in this writer; it is only used for VirtualizedBulkData which is not used in cooked content
	checkNoEntry();
}

void FZenStoreWriter::Initialize(const FCookInfo& Info)
{
	CookMode = Info.CookMode;

	if (!bInitialized)
	{
		if (Info.bFullBuild)
		{
			UE_LOG(LogZenStoreWriter, Display, TEXT("Deleting %s..."), *OutputPath);
			const bool bRequireExists = false;
			const bool bTree = true;
			IFileManager::Get().DeleteDirectory(*OutputPath, bRequireExists, bTree);
		}

		FString ProjectId = FApp::GetProjectName();
		FString OplogId = TargetPlatform.PlatformName();
		bool bOplogEstablished = HttpClient->TryCreateOplog(ProjectId, OplogId, Info.bFullBuild);
		UE_CLOG(!bOplogEstablished, LogZenStoreWriter, Fatal, TEXT("Failed to establish oplog on the ZenServer"));

		if (!Info.bFullBuild)
		{
			UE_LOG(LogZenStoreWriter, Display, TEXT("Fetching oplog..."), *ProjectId, *OplogId);

			TFuture<FIoStatus> FutureOplogStatus = HttpClient->GetOplog().Next([this](TIoStatusOr<FCbObject> OplogStatus)
				{
					if (!OplogStatus.IsOk())
					{
						return OplogStatus.Status();
					}

					FCbObject Oplog = OplogStatus.ConsumeValueOrDie();

					if (Oplog["entries"])
					{
						for (FCbField& OplogEntry : Oplog["entries"].AsArray())
						{
							FCbObject OplogObj = OplogEntry.AsObject();

							if (OplogObj["package"])
							{
								FCbObject PackageObj = OplogObj["package"].AsObject();

								const FGuid PkgGuid = PackageObj["guid"].AsUuid();
								const FIoHash PkgHash = PackageObj["data"].AsHash();
								const int64	PkgDiskSize = PackageObj["disksize"].AsUInt64();
								FPackageStoreEntryResource Entry = FPackageStoreEntryResource::FromCbObject(OplogObj["packagestoreentry"].AsObject());
								const FName PackageName = Entry.PackageName;

								const int32 Index = PackageStoreEntries.Num();

								PackageStoreEntries.Add(MoveTemp(Entry));
								FOplogCookInfo& CookInfo = CookedPackagesInfo.Add_GetRef(
									FOplogCookInfo{
										FCookedPackageInfo {PackageName, IoHashToMD5(PkgHash), PkgGuid, PkgDiskSize }
									});
								PackageNameToIndex.Add(PackageName, Index);

								for (FCbFieldView Field : OplogObj)
								{
									FUtf8StringView FieldName = Field.GetName();
									if (IsReservedOplogKey(FieldName))
									{
										continue;
									}
									if (Field.IsHash())
									{
										const UTF8CHAR* AttachmentId = UE::FZenStoreHttpClient::FindOrAddAttachmentId(FieldName);
										CookInfo.Attachments.Add({ AttachmentId, Field.AsHash() });
									}
								}
								CookInfo.Attachments.Shrink();
								check(Algo::IsSorted(CookInfo.Attachments,
									[](const FOplogCookInfo::FAttachment& A, const FOplogCookInfo::FAttachment& B)
									{
										return FUtf8StringView(A.Key).Compare(FUtf8StringView(B.Key), ESearchCase::IgnoreCase) < 0;
									}));
							}
						}
					}

					return FIoStatus::Ok;
				});

			UE_LOG(LogZenStoreWriter, Display, TEXT("Fetching file manifest..."), *ProjectId, *OplogId);

			TIoStatusOr<FCbObject> FileStatus = HttpClient->GetFiles().Get();
			if (FileStatus.IsOk())
			{
				FCbObject FilesObj = FileStatus.ConsumeValueOrDie();
				for (FCbField& FileEntry : FilesObj["files"])
				{
					FCbObject FileObj = FileEntry.AsObject();
					FCbObjectId FileId = FileObj["id"].AsObjectId();
					FString ServerPath = FString(FileObj["serverpath"].AsString());
					FString ClientPath = FString(FileObj["clientpath"].AsString());

					FIoChunkId FileChunkId;
					FileChunkId.Set(FileId.GetView());

					ZenFileSystemManifest->AddManifestEntry(FileChunkId, MoveTemp(ServerPath), MoveTemp(ClientPath));
				}

				UE_LOG(LogZenStoreWriter, Display, TEXT("Fetched '%d' files(s) from oplog '%s/%s'"), ZenFileSystemManifest->NumEntries(), *ProjectId, *OplogId);
			}
			else
			{
				UE_LOG(LogZenStoreWriter, Warning, TEXT("Failed to fetch file(s) from oplog '%s/%s'"), *ProjectId, *OplogId);
			}

			if (FutureOplogStatus.Get().IsOk())
			{
				UE_LOG(LogZenStoreWriter, Display, TEXT("Fetched '%d' packages(s) from oplog '%s/%s'"), PackageStoreEntries.Num(), *ProjectId, *OplogId);
			}
			else
			{
				UE_LOG(LogZenStoreWriter, Warning, TEXT("Failed to fetch oplog '%s/%s'"), *ProjectId, *OplogId);
			}
		}
		bInitialized = true;
	}
	else
	{
		if (Info.bFullBuild)
		{
			RemoveCookedPackages();
		}
	}
}

void FZenStoreWriter::BeginCook()
{
	if (CookMode == ICookedPackageWriter::FCookInfo::CookOnTheFlyMode)
	{
		FCbPackage Pkg;
		FCbWriter PackageObj;
		
		PackageObj.BeginObject();
		PackageObj << "key" << "CookOnTheFly";

		const bool bGenerateContainerHeader = false;
		CreateProjectMetaData(Pkg, PackageObj, bGenerateContainerHeader);

		PackageObj.EndObject();
		FCbObject Obj = PackageObj.Save().AsObject();

		Pkg.SetObject(Obj);

		TIoStatusOr<uint64> Status = HttpClient->AppendOp(Pkg);
		UE_CLOG(!Status.IsOk(), LogZenStoreWriter, Fatal, TEXT("Failed to append OpLog"));
	}
}

void FZenStoreWriter::EndCook()
{
	HttpQueue->Flush();

	FCbPackage Pkg;
	FCbWriter PackageObj;
	
	PackageObj.BeginObject();
	PackageObj << "key" << "EndCook";

	const bool bGenerateContainerHeader = true;
	CreateProjectMetaData(Pkg, PackageObj, bGenerateContainerHeader);

	PackageObj.EndObject();
	FCbObject Obj = PackageObj.Save().AsObject();

	Pkg.SetObject(Obj);

	TIoStatusOr<uint64> Status = HttpClient->EndBuildPass(Pkg);
	UE_CLOG(!Status.IsOk(), LogZenStoreWriter, Fatal, TEXT("Failed to append OpLog and end the build pass"));

	PackageStoreManifest.Save(*(MetadataDirectoryPath / TEXT("packagestore.manifest")));

	UE_LOG(LogZenStoreWriter, Display, TEXT("Input:\t%d Packages"), PackageStoreOptimizer->GetTotalPackageCount());
	UE_LOG(LogZenStoreWriter, Display, TEXT("Output:\t%d Export bundles"), PackageStoreOptimizer->GetTotalExportBundleCount());
	UE_LOG(LogZenStoreWriter, Display, TEXT("Output:\t%d Export bundle entries"), PackageStoreOptimizer->GetTotalExportBundleEntryCount());
	UE_LOG(LogZenStoreWriter, Display, TEXT("Output:\t%d Internal export bundle arcs"), PackageStoreOptimizer->GetTotalInternalBundleArcsCount());
	UE_LOG(LogZenStoreWriter, Display, TEXT("Output:\t%d External export bundle arcs"), PackageStoreOptimizer->GetTotalExternalBundleArcsCount());
	UE_LOG(LogZenStoreWriter, Display, TEXT("Output:\t%d Public runtime script objects"), PackageStoreOptimizer->GetTotalScriptObjectCount());
	
	UE_LOG(LogZenStoreWriter, Display, TEXT("%.2llfMiB uploaded to Zen storage in %.2llfmin (Speed=%.2llfMiB/s)"),
		double(ZenStats.TotalBytes) / 1024.0 / 1024.0,
		ZenStats.TotalRequestTime / 60.0,
		(double(ZenStats.TotalBytes) / 1024.0 / 1024.0) / ZenStats.TotalRequestTime);
}

void FZenStoreWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	FWriteScopeLock _(PackagesLock);

	const FPendingPackageState* ExistingState = PendingPackages.Find(Info.PackageName);

	checkf(ExistingState == nullptr, TEXT("BeginPackage called for package which is already pending: '%s'"), *Info.PackageName.ToString());

	FPendingPackageState PackageState;
	PackageState.PackageName = Info.PackageName;

	PendingPackages.Add(Info.PackageName, PackageState);

	PackageStoreManifest.BeginPackage(Info.PackageName);
}

bool FZenStoreWriter::IsReservedOplogKey(FUtf8StringView Key)
{
	int32 Index = Algo::LowerBound(ReservedOplogKeys, Key,
		[](const UTF8CHAR* Existing, FUtf8StringView Key)
		{
			return FUtf8StringView(Existing).Compare(Key, ESearchCase::IgnoreCase) < 0;
		});
	return Index != ReservedOplogKeys.Num() &&
		FUtf8StringView(ReservedOplogKeys[Index]).Equals(Key, ESearchCase::IgnoreCase);
}

TFuture<FMD5Hash> FZenStoreWriter::CommitPackage(FCommitPackageInfo&& Info)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::CommitPackage);

	IPackageStoreWriter::FCommitEventArgs CommitEventArgs;

	CommitEventArgs.PlatformName	= FName(*TargetPlatform.PlatformName());
	CommitEventArgs.PackageName		= Info.PackageName;
	CommitEventArgs.EntryIndex		= INDEX_NONE;
	
	FCbPackage Pkg;
	bool bIsValid = Info.bSucceeded;
	TFuture<FMD5Hash> CookedHash;

	{
		FWriteScopeLock _(PackagesLock);

		FPendingPackageState* PackageState = PendingPackages.Find(Info.PackageName);

		checkf(PackageState != nullptr, TEXT("CommitPackage called for package which is not pending: '%s'"), *Info.PackageName.ToString());

		if (bIsValid && EnumHasAnyFlags(Info.WriteOptions, EWriteOptions::Write))
		{
			checkf(EnumHasAllFlags(Info.WriteOptions, EWriteOptions::Write), TEXT("Partial EWriteOptions::Write options are not yet implemented."));
			checkf(!EnumHasAnyFlags(Info.WriteOptions, EWriteOptions::SaveForDiff), TEXT("-diffonly -savefordiff is not yet implemented."));
			check(PackageState->PackageData.IsValid);

			FPackageDataEntry& PkgData = PackageState->PackageData;

			const int64 PkgDiskSize = PkgData.Payload.DataSize();

			FCbAttachment PkgDataAttachment = CreateAttachment(PkgData.Payload);
			Pkg.AddAttachment(PkgDataAttachment);

			CommitEventArgs.EntryIndex = PackageNameToIndex.FindOrAdd(Info.PackageName, PackageStoreEntries.Num());
			if (CommitEventArgs.EntryIndex == PackageStoreEntries.Num())
			{
				PackageStoreEntries.Emplace();
				CookedPackagesInfo.Emplace();
			}

			PackageStoreEntries[CommitEventArgs.EntryIndex] = PkgData.PackageStoreEntry;
			FOplogCookInfo& CookInfo = CookedPackagesInfo[CommitEventArgs.EntryIndex];
			CookInfo = FOplogCookInfo{
					FCookedPackageInfo { Info.PackageName, IoHashToMD5(PkgDataAttachment.GetHash()), Info.PackageGuid, PkgDiskSize } };
			CookInfo.bUpToDate = true;

			int32 NumAttachments = Info.Attachments.Num();
			TArray<FCbAttachment, TInlineAllocator<2>> CbAttachments;
			if (NumAttachments)
			{
				TArray<const FCommitAttachmentInfo*, TInlineAllocator<2>> SortedAttachments;
				SortedAttachments.Reserve(NumAttachments);
				for (const FCommitAttachmentInfo& AttachmentInfo : Info.Attachments)
				{
					SortedAttachments.Add(&AttachmentInfo);
				}
				SortedAttachments.Sort([](const FCommitAttachmentInfo& A, const FCommitAttachmentInfo& B)
					{
						return A.Key.Compare(B.Key, ESearchCase::IgnoreCase) < 0;
					});
				CbAttachments.Reserve(NumAttachments);
				CookInfo.Attachments.Reserve(NumAttachments);
				for (const FCommitAttachmentInfo* AttachmentInfo : SortedAttachments)
				{
					check(!IsReservedOplogKey(AttachmentInfo->Key));
					const FCbAttachment& CbAttachment = CbAttachments.Add_GetRef(CreateAttachment(AttachmentInfo->Value.GetBuffer().ToShared()));
					Pkg.AddAttachment(CbAttachment);
					CookInfo.Attachments.Add(FOplogCookInfo::FAttachment{
						UE::FZenStoreHttpClient::FindOrAddAttachmentId(AttachmentInfo->Key), CbAttachment.GetHash() });
				}
			}

			FCbWriter PackageObj;
			PackageObj.BeginObject();
			FString PackageNameKey = Info.PackageName.ToString();
			PackageNameKey.ToLowerInline();
			PackageObj << "key" << PackageNameKey;

			// NOTE: The package GUID and disk size are used for legacy iterative cooks when comparing asset registry package data
			PackageObj.BeginObject("package");
			PackageObj << "id" << PkgData.ChunkId;
			PackageObj << "guid" << Info.PackageGuid;
			PackageObj << "data" << PkgDataAttachment;
			PackageObj << "disksize" << PkgDiskSize;
			PackageObj.EndObject();

			PackageObj << "packagestoreentry" << PkgData.PackageStoreEntry;
			
			if (PackageState->BulkData.Num())
			{
				PackageObj.BeginArray("bulkdata");

				for (FBulkDataEntry& Bulk : PackageState->BulkData)
				{
					FCbAttachment BulkAttachment = CreateAttachment(Bulk.Payload);
					Pkg.AddAttachment(BulkAttachment);

					PackageObj.BeginObject();
					PackageObj << "id" << Bulk.ChunkId;
					PackageObj << "type" << LexToString(Bulk.Info.BulkDataType);
					PackageObj << "data" << BulkAttachment;
					PackageObj.EndObject();
				}

				PackageObj.EndArray();
			}

			if (PackageState->FileData.Num())
			{
				PackageObj.BeginArray("files");

				for (FFileDataEntry& File : PackageState->FileData)
				{
					FCbAttachment FileDataAttachment = CreateAttachment(File.Payload);
					Pkg.AddAttachment(FileDataAttachment);

					PackageObj.BeginObject();
					PackageObj << "id" << ToObjectId(File.Info.ChunkId);
					PackageObj << "data" << FileDataAttachment;
					PackageObj << "serverpath" << File.ZenManifestServerPath;
					PackageObj << "clientpath" << File.ZenManifestClientPath;
					PackageObj.EndObject();

					CommitEventArgs.AdditionalFiles.Add(FAdditionalFileInfo
					{ 
						Info.PackageName,
						File.ZenManifestClientPath,
						File.Info.ChunkId
					});
				}

				PackageObj.EndArray();
			}

			for (int32 Index = 0; Index < NumAttachments; ++Index)
			{
				FCbAttachment& CbAttachment = CbAttachments[Index];
				FOplogCookInfo::FAttachment& CookInfoAttachment = CookInfo.Attachments[Index];
				PackageObj << CookInfoAttachment.Key << CbAttachment;
			}

			PackageObj.EndObject();

			FCbObject Obj = PackageObj.Save().AsObject();

			Pkg.SetObject(Obj);
		}

		if (bIsValid && EnumHasAnyFlags(Info.WriteOptions, EWriteOptions::ComputeHash))
		{
			CookedHash = AsyncComputeCookedHash(*PackageState);
		}
	}

	if (bIsValid)
	{
		HttpQueue->Enqueue(Pkg, [this, EventArgs = MoveTemp(CommitEventArgs)](FZenStoreHttpQueue::FHttpRequestResult HttpResult) mutable
		{
			ZenStats.TotalBytes += HttpResult.TotalBytes;
			ZenStats.TotalRequestTime += HttpResult.DurationInSeconds;
			BroadcastCommit(EventArgs);
		});
	}
	else
	{
		BroadcastCommit(CommitEventArgs);
	}

	{
		FWriteScopeLock _(PackagesLock);
		PendingPackages.Remove(Info.PackageName);
	}

	return CookedHash;
}

TFuture<FMD5Hash> FZenStoreWriter::AsyncComputeCookedHash(const FPendingPackageState& PackageState)
{
	check(PackageState.PackageData.IsValid);

	TArray<FIoBuffer> Payloads;
	Payloads.Reserve(PackageState.BulkData.Num() + PackageState.FileData.Num() + 1);
	for (const FBulkDataEntry& BulkData : PackageState.BulkData)
	{
		const FIoBuffer& Payload = BulkData.Payload;
		Payloads.Add(FIoBuffer(Payload.Data(), Payload.DataSize(), Payload));
	}

	for (const FFileDataEntry& FileData : PackageState.FileData)
	{
		const FIoBuffer& Payload = FileData.Payload;
		Payloads.Add(FIoBuffer(Payload.Data(), Payload.DataSize(), Payload));
	}
	{
		const FIoBuffer& Payload = PackageState.PackageData.Payload;
		Payloads.Add(FIoBuffer(Payload.Data(), Payload.DataSize(), Payload));
	}

	UE::SavePackageUtilities::IncrementOutstandingAsyncWrites();
	return Async(EAsyncExecution::TaskGraph, [Payloads = MoveTemp(Payloads)]() mutable
	{
		FMD5 AccumulatedHash;

		for (FIoBuffer& Payload : Payloads)
		{
			// Moving the Payload to a local variable so that after the update its possibly heavyweight data
			// is cleaned up instead of waiting to clean them all up at the end.
			FIoBuffer LocalPayload = MoveTemp(Payload);
			AccumulatedHash.Update(LocalPayload.Data(), LocalPayload.DataSize());
		}

		UE::SavePackageUtilities::DecrementOutstandingAsyncWrites();
		FMD5Hash OutputHash;
		OutputHash.Set(AccumulatedHash);
		return OutputHash;
	});
}

void FZenStoreWriter::GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>, TArrayView<const FOplogCookInfo>)>&& Callback)
{
	FReadScopeLock _(PackagesLock);
	Callback(PackageStoreEntries, CookedPackagesInfo);
}

void FZenStoreWriter::Flush()
{
	HttpQueue->Flush();
}

TUniquePtr<FAssetRegistryState> FZenStoreWriter::LoadPreviousAssetRegistry() 
{
	// Load the previous asset registry to return to CookOnTheFlyServer, and set the packages enumerated in both *this and
	// the returned asset registry to the intersection of the oplog and the previous asset registry;
	// to report a package as already cooked we have to have the information from both sources.
	FString PreviousAssetRegistryFile = FPaths::Combine(MetadataDirectoryPath, GetDevelopmentAssetRegistryFilename());
	FArrayReader SerializedAssetData;
	if (!IFileManager::Get().FileExists(*PreviousAssetRegistryFile) ||
		!FFileHelper::LoadFileToArray(SerializedAssetData, *PreviousAssetRegistryFile))
	{
		RemoveCookedPackages();
		return TUniquePtr<FAssetRegistryState>();
	}

	TUniquePtr<FAssetRegistryState> PreviousState = MakeUnique<FAssetRegistryState>();
	PreviousState->Load(SerializedAssetData);

	TSet<FName> RemoveSet;
	const TMap<FName, const FAssetPackageData*>& PreviousStatePackages = PreviousState->GetAssetPackageDataMap(); 
	for (const TPair<FName, const FAssetPackageData*>& Pair : PreviousStatePackages)
	{
		FName PackageName = Pair.Key;
		if (!PackageNameToIndex.Find(PackageName))
		{
			RemoveSet.Add(PackageName);
		}
	}
	if (RemoveSet.Num())
	{
		PreviousState->PruneAssetData(TSet<FName>(), RemoveSet, FAssetRegistrySerializationOptions());
	}

	TArray<FName> RemoveArray;
	for (TPair<FName, int32>& Pair : PackageNameToIndex)
	{
		FName PackageName = Pair.Key;
		if (!PreviousStatePackages.Find(PackageName))
		{
			RemoveArray.Add(PackageName);
		}
	}
	if (RemoveArray.Num())
	{
		RemoveCookedPackages(RemoveArray);
	}

	return PreviousState;
}

FCbObject FZenStoreWriter::GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey)
{
	const int32* Idx = PackageNameToIndex.Find(PackageName);
	if (!Idx)
	{
		return FCbObject();
	}

	const UTF8CHAR* AttachmentId = UE::FZenStoreHttpClient::FindAttachmentId(AttachmentKey);
	if (!AttachmentId)
	{
		return FCbObject();
	}
	FUtf8StringView AttachmentIdView(AttachmentId);

	const FOplogCookInfo& CookInfo = CookedPackagesInfo[*Idx];
	int32 AttachmentIndex = Algo::LowerBound(CookInfo.Attachments, AttachmentIdView,
		[](const FOplogCookInfo::FAttachment& Existing, FUtf8StringView AttachmentIdView)
		{
			return FUtf8StringView(Existing.Key).Compare(AttachmentIdView, ESearchCase::IgnoreCase) < 0;
		});
	if (AttachmentIndex == CookInfo.Attachments.Num())
	{
		return FCbObject();
	}
	const FOplogCookInfo::FAttachment& Existing = CookInfo.Attachments[AttachmentIndex];
	if (!FUtf8StringView(Existing.Key).Equals(AttachmentIdView, ESearchCase::IgnoreCase))
	{
		return FCbObject();
	}
	TIoStatusOr<FIoBuffer> BufferResult = HttpClient->ReadOpLogAttachment(WriteToString<48>(Existing.Hash));
	if (!BufferResult.IsOk())
	{
		return FCbObject();
	}
	FIoBuffer Buffer = BufferResult.ValueOrDie();
	if (Buffer.DataSize() == 0)
	{
		return FCbObject();
	}

	FSharedBuffer SharedBuffer = IoBufferToSharedBuffer(Buffer);
	return FCbObject(SharedBuffer);
}

void FZenStoreWriter::RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove)
{
	TSet<int32> PackageIndicesToKeep;
	for (int32 Idx = 0, Num = PackageStoreEntries.Num(); Idx < Num; ++Idx)
	{
		PackageIndicesToKeep.Add(Idx);
	}
	
	for (const FName& PackageName : PackageNamesToRemove)
	{
		if (const int32* Idx = PackageNameToIndex.Find(PackageName))
		{
			PackageIndicesToKeep.Remove(*Idx);
		}
	}

	const int32 NumPackagesToKeep = PackageIndicesToKeep.Num();
	
	TArray<FPackageStoreEntryResource> PreviousPackageStoreEntries = MoveTemp(PackageStoreEntries);
	TArray<FOplogCookInfo> PreviousCookedPackageInfo = MoveTemp(CookedPackagesInfo);
	PackageNameToIndex.Empty();

	if (NumPackagesToKeep > 0)
	{
		PackageStoreEntries.Reserve(NumPackagesToKeep);
		CookedPackagesInfo.Reserve(NumPackagesToKeep);
		PackageNameToIndex.Reserve(NumPackagesToKeep);

		int32 EntryIndex = 0;
		for (int32 Idx : PackageIndicesToKeep)
		{
			const FName PackageName = PreviousCookedPackageInfo[Idx].CookInfo.PackageName;

			PackageStoreEntries.Add(MoveTemp(PreviousPackageStoreEntries[Idx]));
			CookedPackagesInfo.Add(MoveTemp(PreviousCookedPackageInfo[Idx]));
			PackageNameToIndex.Add(PackageName, EntryIndex++);
		}
	}
}

void FZenStoreWriter::RemoveCookedPackages()
{
	PackageStoreEntries.Empty();
	CookedPackagesInfo.Empty();
	PackageNameToIndex.Empty();
}

void FZenStoreWriter::MarkPackagesUpToDate(TArrayView<const FName> UpToDatePackages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::MarkPackagesUpToDate);

	IPackageStoreWriter::FMarkUpToDateEventArgs MarkUpToDateEventArgs;

	MarkUpToDateEventArgs.PackageIndexes.Reserve(UpToDatePackages.Num());

	{
		FWriteScopeLock _(PackagesLock);
		for (FName PackageName : UpToDatePackages)
		{
			int32* Index = PackageNameToIndex.Find(PackageName);
			if (!Index)
			{
				if (!FPackageName::IsScriptPackage(WriteToString<128>(PackageName)))
				{
					UE_LOG(LogZenStoreWriter, Warning, TEXT("MarkPackagesUpToDate called with package %s that is not in the oplog."),
						*PackageName.ToString());
				}
				continue;
			}

			MarkUpToDateEventArgs.PackageIndexes.Add(*Index);
			CookedPackagesInfo[*Index].bUpToDate = true;
		}
	}
	if (MarkUpToDateEventArgs.PackageIndexes.Num())
	{
		BroadcastMarkUpToDate(MarkUpToDateEventArgs);
	}
}

void FZenStoreWriter::CreateProjectMetaData(FCbPackage& Pkg, FCbWriter& PackageObj, bool bGenerateContainerHeader)
{
	// File Manifest
	{
		// Only append new file entries to the Oplog
		
		int32 NumEntries = ZenFileSystemManifest->NumEntries();
		const int32 NumNewEntries = ZenFileSystemManifest->Generate();

		if (NumNewEntries > 0)
		{
			TArrayView<FZenFileSystemManifest::FManifestEntry const> Entries = ZenFileSystemManifest->ManifestEntries();
			TArrayView<FZenFileSystemManifest::FManifestEntry const> NewEntries = Entries.Slice(NumEntries, NumNewEntries);
			
			PackageObj.BeginArray("files");

			for (const FZenFileSystemManifest::FManifestEntry& NewEntry : NewEntries)
			{
				FCbObjectId FileOid = ToObjectId(NewEntry.FileChunkId);

				PackageObj.BeginObject();
				PackageObj << "id" << FileOid;
				PackageObj << "data" << FIoHash::Zero;
				PackageObj << "serverpath" << NewEntry.ServerPath;
				PackageObj << "clientpath" << NewEntry.ClientPath;
				PackageObj.EndObject();
			}

			PackageObj.EndArray();
		}

		FString ManifestPath = FPaths::Combine(MetadataDirectoryPath, TEXT("zenfs.manifest"));
		UE_LOG(LogZenStoreWriter, Display, TEXT("Saving Zen filesystem manifest '%s'"), *ManifestPath);
		ZenFileSystemManifest->Save(*ManifestPath);
	}

	// Metadata section
	{
		PackageObj.BeginArray("meta");

		// Summarize Script Objects
		FIoBuffer ScriptObjectsBuffer = PackageStoreOptimizer->CreateScriptObjectsBuffer();
		FCbObjectId ScriptOid = ToObjectId(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects));

		FCbAttachment ScriptAttachment = CreateAttachment(ScriptObjectsBuffer); 
		Pkg.AddAttachment(ScriptAttachment);

		PackageObj.BeginObject();
		PackageObj << "id" << ScriptOid;
		PackageObj << "name" << "ScriptObjects";
		PackageObj << "data" << ScriptAttachment;
		PackageObj.EndObject();

		// Generate Container Header
		if (bGenerateContainerHeader)
		{
			FCbObjectId HeaderOid = ToObjectId(CreateIoChunkId(ContainerId.Value(), 0, EIoChunkType::ContainerHeader));
			FIoBuffer HeaderBuffer;

			{
				FIoContainerHeader Header = PackageStoreOptimizer->CreateContainerHeader(ContainerId, PackageStoreEntries);
				FLargeMemoryWriter HeaderAr(0, true);
				HeaderAr << Header;
				int64 DataSize = HeaderAr.TotalSize();
				HeaderBuffer = FIoBuffer(FIoBuffer::AssumeOwnership, HeaderAr.ReleaseOwnership(), DataSize);
			}

			FCbAttachment HeaderAttachment = CreateAttachment(HeaderBuffer);
			Pkg.AddAttachment(HeaderAttachment);
			
			PackageObj.BeginObject();
			PackageObj << "id" << HeaderOid;
			PackageObj << "name" << "ContainerHeader";
			PackageObj << "data" << HeaderAttachment;
			PackageObj.EndObject();
		}

		PackageObj.EndArray();	// End of Meta array
	}
}

void FZenStoreWriter::BroadcastCommit(IPackageStoreWriter::FCommitEventArgs& EventArgs)
{
	FScopeLock CommitEventLock(&CommitEventCriticalSection);
	
	if (CommitEvent.IsBound())
	{
		FReadScopeLock EntriesLock(PackagesLock);
		EventArgs.Entries = PackageStoreEntries;
		CommitEvent.Broadcast(EventArgs);
	}
}

void FZenStoreWriter::BroadcastMarkUpToDate(IPackageStoreWriter::FMarkUpToDateEventArgs& EventArgs)
{
	FScopeLock CommitEventLock(&CommitEventCriticalSection);

	if (MarkUpToDateEvent.IsBound())
	{
		FReadScopeLock EntriesLock(PackagesLock);
		EventArgs.PlatformName = FName(*TargetPlatform.PlatformName());
		EventArgs.Entries = PackageStoreEntries;
		EventArgs.CookInfos = CookedPackagesInfo;
		MarkUpToDateEvent.Broadcast(EventArgs);
	}
}

FCbAttachment FZenStoreWriter::CreateAttachment(FSharedBuffer AttachmentData)
{
	check(AttachmentData.GetSize() > 0);
	FCompressedBuffer CompressedBuffer = FCompressedBuffer::Compress(AttachmentData, Compressor, CompressionLevel);
	check(!CompressedBuffer.IsNull());
	return FCbAttachment(CompressedBuffer);
}

FCbAttachment FZenStoreWriter::CreateAttachment(FIoBuffer AttachmentData)
{
	return CreateAttachment(IoBufferToSharedBuffer(AttachmentData));
}
