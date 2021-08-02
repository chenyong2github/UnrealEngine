// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenStoreWriter.h"
#include "ZenStoreHttpClient.h"
#include "ZenFileSystemManifest.h"
#include "PackageStoreOptimizer.h"

#include "Async/Async.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/LargeMemoryWriter.h" 
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoDispatcher.h"
#include "Misc/App.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/ScopeRWLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogZenStoreWriter, Log, All);

using namespace UE;

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
		Request->HttpResult = HttpClient
			.AppendOp(MoveTemp(Request->OpEntry))
			.Next([StartTime = FPlatformTime::Seconds()](TIoStatusOr<uint64> Status)
			{
				return FHttpRequestResult
				{
					FPlatformTime::Seconds() - StartTime,
					Status.IsOk() ? Status.ValueOrDie() : 0,
					Status.IsOk()
				};
			});

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
		TFuture<FHttpRequestResult> HttpResult;
		FHttpRequestCompleted Callback;
		FHttpRequest* Next = nullptr;
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
					Request->Callback(Request->HttpResult.Get());

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

FZenStoreWriter::FZenStoreWriter(
	const FString& InOutputPath, 
	const FString& InMetadataDirectoryPath, 
	const ITargetPlatform* InTargetPlatform,
	bool IsCleanBuild)
	: TargetPlatform(*InTargetPlatform)
	, OutputPath(InOutputPath)
	, MetadataDirectoryPath(InMetadataDirectoryPath)
	, PackageStoreOptimizer(new FPackageStoreOptimizer())
	, CookMode(IPackageStoreWriter::FCookInfo::CookByTheBookMode)
{
	FString HostName = TEXT("localhost");
	uint16 Port = 1337;
	FString ProjectId = FApp::GetProjectName();
	FString OplogId = InTargetPlatform->PlatformName();

	HttpClient = MakeUnique<UE::FZenStoreHttpClient>(HostName, Port);

	FString RootDir = FPaths::RootDir();
	FString EngineDir = FPaths::EngineDir();
	FPaths::NormalizeDirectoryName(EngineDir);
	FString ProjectDir = FPaths::ProjectDir();
	FPaths::NormalizeDirectoryName(ProjectDir);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString AbsServerRoot = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*RootDir);
	FString AbsEngineDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*EngineDir);
	FString AbsProjectDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectDir);

	HttpClient->Initialize(	ProjectId, 
							OplogId, 
							AbsServerRoot, 
							AbsEngineDir, 
							AbsProjectDir, 
							IsCleanBuild);

	PackageStoreOptimizer->Initialize(InTargetPlatform);

	FPackageStoreManifest::FZenServerInfo& ZenServerInfo = PackageStoreManifest.EditZenServerInfo();
	ZenServerInfo.HostName = HostName;
	ZenServerInfo.Port = Port;
	ZenServerInfo.ProjectId = ProjectId;
	ZenServerInfo.OplogId = OplogId;

	ZenFileSystemManifest = MakeUnique<FZenFileSystemManifest>(TargetPlatform, OutputPath);
	
	HttpQueue = MakeUnique<FZenStoreHttpQueue>(*HttpClient);

	if (!IsCleanBuild)
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
					FCbObject OplogObj						= OplogEntry.AsObject();

					if (OplogObj["package"])
					{
						FCbObject PackageObj				= OplogObj["package"].AsObject();

						const FGuid PkgGuid					= PackageObj["guid"].AsUuid();
						const FIoHash PkgHash				= PackageObj["data"].AsHash();
						const int64	PkgDiskSize				= PackageObj["disksize"].AsUInt64();
						FPackageStoreEntryResource Entry	= FPackageStoreEntryResource::FromCbObject(OplogObj["packagestoreentry"].AsObject());
						const FName PackageName				= Entry.PackageName;

						const int32 Index					= PackageStoreEntries.Num();

						PackageStoreEntries.Add(MoveTemp(Entry));
						CookedPackagesInfo.Add(FCookedPackageInfo { PackageName, IoHashToMD5(PkgHash), PkgGuid, PkgDiskSize });
						PackageNameToIndex.Add(PackageName, Index);
					}
				}
			}

			return FIoStatus::Ok;
		});

		UE_LOG(LogZenStoreWriter, Display, TEXT("Fetching file manifest..."), *ProjectId, *OplogId);

		TIoStatusOr<FCbObject> FileStatus = HttpClient->GetFiles().Get();
		if (FileStatus .IsOk())
		{
			FCbObject FilesObj = FileStatus.ConsumeValueOrDie();
			if (FilesObj["files"])
			{
				for (FCbField& FileEntry : FilesObj["files"].AsArray())
				{
					FCbObject FileObj	= FileEntry.AsObject();
					FCbObjectId FileId	= FileObj["id"].AsObjectId();
					FString ServerPath	= FString(FileObj["serverpath"].AsString());
					FString ClientPath	= FString(FileObj["clientpath"].AsString());

					FIoChunkId FileChunkId;
					FileChunkId.Set(FileId.GetView());
					
					ZenFileSystemManifest->AddManifestEntry(FileChunkId, MoveTemp(ServerPath), MoveTemp(ClientPath));
				}
			}

			UE_LOG(LogZenStoreWriter, Display, TEXT("Fetched '%d' files(s) from oplog '%s/%s'"), ZenFileSystemManifest->NumEntries(), *ProjectId, *OplogId);
		}
		else
		{
			UE_LOG(LogZenStoreWriter, Warning, TEXT("Failed to fetch file(s) from oplog '%s/%s'"), *ProjectId, *OplogId);
		}

		if (FutureOplogStatus.Get().IsOk())
		{
			UE_LOG(LogZenStoreWriter, Display, TEXT("Fetched '%d' packges(s) from oplog '%s/%s'"), PackageStoreEntries.Num(), *ProjectId, *OplogId);
		}
		else
		{
			UE_LOG(LogZenStoreWriter, Warning, TEXT("Failed to fetch oplog '%s/%s'"), *ProjectId, *OplogId);
		}
	}
}

FZenStoreWriter::~FZenStoreWriter()
{
	FWriteScopeLock _(PackagesLock);

	if (PendingPackages.Num())
	{
		UE_LOG(LogZenStoreWriter, Warning, TEXT("Pending packages at shutdown!"));
	}
}

void FZenStoreWriter::WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());

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

void FZenStoreWriter::WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	// We do not modify the PendingPackages map, so take a shared lock
	FReadScopeLock _(PackagesLock);

	FPendingPackageState* ExistingState = PendingPackages.Find(Info.PackageName);

	checkf(ExistingState != nullptr, TEXT("WriteBulkdata called for package which is not pending: '%s'"), *Info.PackageName.ToString());

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

bool FZenStoreWriter::WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData)
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

	return true;
}

void FZenStoreWriter::WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	// Should not be called because IsLinkerAdditionalDataInSeparateArchive returned false
	checkNoEntry();
}

void FZenStoreWriter::BeginCook(const FCookInfo& Info)
{
	CookMode = Info.CookMode;

	if (Info.CookMode == IPackageStoreWriter::FCookInfo::CookOnTheFlyMode)
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

		TIoStatusOr<uint64> Status = HttpClient->AppendOp(Pkg).Get();
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

void FZenStoreWriter::CommitPackage(const FCommitPackageInfo& Info)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::CommitPackage);

	IPackageStoreWriter::FCommitEventArgs CommitEventArgs;

	CommitEventArgs.PlatformName	= FName(*TargetPlatform.PlatformName()),
	CommitEventArgs.PackageName		= Info.PackageName;
	CommitEventArgs.EntryIndex		= INDEX_NONE;
	
	FCbPackage Pkg;
	bool bIsValid = Info.bSucceeded;

	{
		FWriteScopeLock _(PackagesLock);

		FPendingPackageState* PackageState = PendingPackages.Find(Info.PackageName);

		checkf(PackageState != nullptr, TEXT("CommitPackage called for package which is not pending: '%s'"), *Info.PackageName.ToString());

		if (bIsValid)
		{
			check(PackageState->PackageData.IsValid);

			// Note that this is destructive - we yank out the buffer memory from the 
			// IoBuffer into the FSharedBuffer
			auto IoBufferToSharedBuffer = [](FIoBuffer& InBuffer) -> FSharedBuffer {
				InBuffer.EnsureOwned();
				const uint64 DataSize = InBuffer.DataSize();
				uint8* DataPtr = InBuffer.Release().ValueOrDie();
				return FSharedBuffer{ FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free) };
			};

			FPackageDataEntry& PkgData = PackageState->PackageData;
			
			const int64 PkgDiskSize = PkgData.Payload.DataSize();
			
			FCbAttachment PkgDataAttachment(IoBufferToSharedBuffer(PkgData.Payload));
			Pkg.AddAttachment(PkgDataAttachment);
			
			CommitEventArgs.EntryIndex = PackageStoreEntries.Num();
			PackageStoreEntries.Add(PkgData.PackageStoreEntry);
			CookedPackagesInfo.Add(FCookedPackageInfo { Info.PackageName, IoHashToMD5(PkgDataAttachment.GetHash()), Info.PackageGuid, PkgDiskSize });

			FCbWriter PackageObj;
			PackageObj.BeginObject();
			PackageObj << "key" << Info.PackageName.ToString();

			// NOTE: The package GUID and disk size are used for iterative cooks when comparing asset registry package data
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
					FCbAttachment BulkAttachment(IoBufferToSharedBuffer(Bulk.Payload));
					Pkg.AddAttachment(BulkAttachment);

					PackageObj.BeginObject();
					PackageObj << "id" << Bulk.ChunkId;
					PackageObj << "type" << LexToString(Bulk.Info.BulkdataType);
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
					FCbAttachment FileDataAttachment(IoBufferToSharedBuffer(File.Payload));
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

			PackageObj.EndObject();

			FCbObject Obj = PackageObj.Save().AsObject();

			Pkg.SetObject(Obj);
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
}
void FZenStoreWriter::GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&& Callback)
{
	FReadScopeLock _(PackagesLock);
	Callback(PackageStoreEntries);
}

void FZenStoreWriter::Flush()
{
	HttpQueue->Flush();
}

void FZenStoreWriter::GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages) 
{
	OutCookedPackages.Append(CookedPackagesInfo);
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
	TArray<FCookedPackageInfo> PreviousCookedPackageInfo = MoveTemp(CookedPackagesInfo);
	PackageNameToIndex.Empty(false);

	if (NumPackagesToKeep > 0)
	{
		PackageStoreEntries.Reserve(NumPackagesToKeep);
		CookedPackagesInfo.Reserve(NumPackagesToKeep);
		PackageNameToIndex.Reserve(NumPackagesToKeep);

		int32 EntryIndex = 0;
		for (int32 Idx : PackageIndicesToKeep)
		{
			const FName PackageName = PreviousCookedPackageInfo[Idx].PackageName;

			PackageStoreEntries.Add(MoveTemp(PreviousPackageStoreEntries[Idx]));
			CookedPackagesInfo.Add(MoveTemp(PreviousCookedPackageInfo[Idx]));
			PackageNameToIndex.Add(PackageName, EntryIndex++);
		}
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

		FCbAttachment ScriptAttachment(FSharedBuffer::MakeView(ScriptObjectsBuffer.Data(), ScriptObjectsBuffer.DataSize()));
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
				FContainerHeader Header = PackageStoreOptimizer->CreateContainerHeader(ContainerId, PackageStoreEntries);
				FLargeMemoryWriter HeaderAr(0, true);
				HeaderAr << Header;
				int64 DataSize = HeaderAr.TotalSize();
				HeaderBuffer = FIoBuffer(FIoBuffer::AssumeOwnership, HeaderAr.ReleaseOwnership(), DataSize);
			}

			FCbAttachment HeaderAttachment(FSharedBuffer::MakeView(HeaderBuffer.Data(), HeaderBuffer.DataSize()));
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
