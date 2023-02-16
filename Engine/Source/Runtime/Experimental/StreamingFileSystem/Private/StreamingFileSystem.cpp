// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamingFileSystem.h"

#include "VirtualFileCache.h"

#include "Interfaces/IBuildPatchServicesModule.h"

#include "InstallBundleManagerInterface.h"
#include "InstallBundleSourceInterface.h"

#include "Modules/ModuleManager.h"

#include "Async/Async.h"
#include "IO/PackageId.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "HAL/ThreadSafeBool.h"

DECLARE_LOG_CATEGORY_EXTERN(LogStreamingFileSystem, Log, All);
DEFINE_LOG_CATEGORY(LogStreamingFileSystem);

int32 GVFCBlockFileSizeMB = 512;
static FAutoConsoleVariableRef CVarVFCBlockFileSizeMB(
	TEXT("VFC.BlockFileSize"),
	GVFCBlockFileSizeMB,
	TEXT("Virtual File Cache block file size on disk (in megabytes).")
);

int32 GVFCMemoryCacheSizeMB = 16;
static FAutoConsoleVariableRef CVarVFCMemoryCacheSizeMB(
	TEXT("VFC.MemoryCacheSize"),
	GVFCMemoryCacheSizeMB,
	TEXT("Virtual File Cache memory cache size for caching writes to avoid reading from disk (in megabytes).")
);

class FStreamingFileSystem : public IStreamingFileSystem
{
public:

	FStreamingFileSystem(TSharedPtr<IVirtualFileCache> InVFS) : VFS(InVFS) {}
	virtual ~FStreamingFileSystem() {}

	/* IIoDispatcherBackend Begin */ 
	virtual void Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) override;
	virtual void Shutdown() override {}
	virtual bool Resolve(FIoRequestImpl* Request) override;
	virtual void CancelIoRequest(FIoRequestImpl* Request) override;
	virtual void UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	virtual bool DoesChunkExist(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const override;
	virtual FIoRequestImpl* GetCompletedRequests() override;
	virtual TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override;
	/* IIoDispatcherBackend End */ 

private:

	using FRequestId = uint32;

	void FinishInit(EInstallBundleManagerInitResult Result, TSharedPtr<IInstallBundleManager> BundleManager);
	void EnginePreExit();

	void OnInstallComplete(FBuildPatchStreamResult Result, FRequestId RequestId);
	void CopyDataToRequest(FIoRequestImpl* Request, const TArray<uint8>& Data);
	void ResolveRequestsInQueue(FIoChunkId CompletedChunk, const TArray<uint8>& Data);
	void CompleteRequest(FIoRequestImpl* Request, bool bFailed = false);

	bool BlockingReadFromVFS(FIoChunkId ChunkId, TArray<uint8>& Result, FString& Error);
	void DownloadRequest(FIoRequestImpl* Request);
	void UpdatePriorityRequest(FIoRequestImpl* Request);
	void FinishRequest(FBuildPatchStreamResult Result, FIoRequestImpl* Request);
	void RunThread();

	TSharedPtr<IVirtualFileCache> VFS;

	IBuildPatchServicesModule* BuildPatchServicesModule;
	IBuildManifestPtr BuildManifest;
	IBuildInstallStreamerPtr ContentStreamer;
	FDelegateHandle FinishInitDelegate;

	TMap<const FIoChunkId, FString> ChunkMap;

	FCriticalSection CompletedLock;
	FIoRequestImpl* CompletedRequest = nullptr;
	FIoRequestImpl* CompletedRequestsTail = nullptr;

	FWakeUpIoDispatcherThreadDelegate WakeUpDispatcherThreadDelegate;
	FIoSignatureErrorDelegate SignatureErrorDelegate;

	TArray<FString> DistributionCDNPaths;

	TFuture<void> Thread;
	FThreadSafeBool bIsShuttingDown;
	bool bInitialized = false;
	FEvent* ThreadTrigger;

	// Only accessible by our thread
	FRequestId NextRequestId = 1;
	FCriticalSection RequestLock;
	static constexpr uint32 MAX_DOWNLOADS = 2;
	TArray<FIoRequestImpl*> ProcessingRequests;
	TMap<FRequestId, FIoRequestImpl*> DownloadingRequests;

	// Accessible by IoDispatcher thread
	TArray<FIoRequestImpl*> Requests;
	TQueue<FIoRequestImpl*> CancelledRequests;

	// Accessible by Main or async thread
	TQueue<FRequestId> DownloadedRequests;
};

void FStreamingFileSystem::FinishInit(EInstallBundleManagerInitResult Result, TSharedPtr<IInstallBundleManager> BundleManager)
{
	BundleManager->InitCompleteDelegate.Remove(FinishInitDelegate);

	if (Result != EInstallBundleManagerInitResult::OK)
	{
		UE_LOG(LogStreamingFileSystem, Warning, TEXT("Failed to initialize InstallBundleManager"));
		return;
	}

	const TSharedPtr<IInstallBundleSource> BundleSource = BundleManager->GetBundleSource(EInstallBundleSourceType::BuildPatchServices);
	if (!BundleSource.IsValid())
	{
		UE_LOG(LogStreamingFileSystem, Warning, TEXT("Failed to initialize BuildPatchServices bundle source"));
		return;
	}
	
	DistributionCDNPaths = BundleSource->GetDistributionCDNPaths();
	BuildManifest = BundleSource->GetBuildManifest();
	if (!BuildManifest.IsValid())
	{
		UE_LOG(LogStreamingFileSystem, Warning, TEXT("Failed to initialize build manifest from bundle source"));
		return;
	}

	TArray<FString> ManifestFiles = BuildManifest->GetBuildFileList();
	for (const FString& ManifestFile : ManifestFiles)
	{
		const FString StartText = FApp::GetProjectName() / FString(TEXT("Content"));
		if (ManifestFile.StartsWith(StartText))
		{
			FString NewFile = ManifestFile.Replace(*StartText, TEXT("/Game"));
			const FString EndText = TEXT(".uptnl");
			if (NewFile.EndsWith(EndText))
			{
				NewFile.ReplaceInline(*EndText, TEXT(""));

				FPackageId PackageId = FPackageId::FromName(FName(NewFile));
				const FIoChunkId OptionalChunkId = CreateIoChunkId(PackageId.Value(), 0, EIoChunkType::OptionalBulkData);

				ChunkMap.Add(OptionalChunkId, ManifestFile);
			}
		}
	}

	BuildPatchServices::FBuildInstallStreamerConfiguration StreamerConfig;
	StreamerConfig.Manifest = BuildManifest;
	StreamerConfig.CloudDirectories = DistributionCDNPaths;
	StreamerConfig.bMainThreadDelegates = false;
	ContentStreamer = BuildPatchServicesModule->CreateBuildInstallStreamer(MoveTemp(StreamerConfig));

	bInitialized = true;
	bIsShuttingDown = false;
	ThreadTrigger = FPlatformProcess::GetSynchEventFromPool(false);
	Thread = Async(EAsyncExecution::ThreadIfForkSafe, [this]()
		{
			RunThread();
		});
}

void FStreamingFileSystem::EnginePreExit()
{
	bIsShuttingDown = true;
	if (ThreadTrigger)
	{
		ThreadTrigger->Trigger();
	}
}

void FStreamingFileSystem::Initialize(TSharedRef<const FIoDispatcherBackendContext> Context)
{
	WakeUpDispatcherThreadDelegate = Context->WakeUpDispatcherThreadDelegate;
	SignatureErrorDelegate = Context->SignatureErrorDelegate;
	//Context->bIsMultiThreaded

	FVirtualFileCacheSettings Settings;
	//uint64 BlockSize = VFS_DEFAULT_BLOCK_SIZE;
	Settings.BlockFileSize = GVFCBlockFileSizeMB * 1024ull * 1024ull;
	Settings.RecentWriteLRUSize = GVFCMemoryCacheSizeMB * 1024ull * 1024ull;
	//uint64 NumBlockFiles = 1;
	//FString OverrideDefaultDirectory;
	VFS->Initialize(Settings);

	UE_LOG(LogStreamingFileSystem, Log, TEXT("Initializing StreamingFileSystem with %llu MB disk cache and %llu MB memory cache"), Settings.BlockFileSize / (1024 * 1024), Settings.RecentWriteLRUSize / (1024 * 1024));

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FStreamingFileSystem::EnginePreExit);

	BuildPatchServicesModule = &FModuleManager::LoadModuleChecked<IBuildPatchServicesModule>(TEXT("BuildPatchServices"));

	TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
	if (BundleManager->GetInitState() != EInstallBundleManagerInitState::Succeeded)
	{
		FinishInitDelegate = BundleManager->InitCompleteDelegate.AddRaw(this, &FStreamingFileSystem::FinishInit, BundleManager);
	}
}

bool FStreamingFileSystem::BlockingReadFromVFS(FIoChunkId ChunkId, TArray<uint8>& Result, FString& ReadError)
{
	FSHAHash Hash = {};
	const FString* FileManifest = ChunkMap.Find(ChunkId);
	if (FileManifest)
	{
		if (!BuildManifest->GetFileHash(*FileManifest, Hash))
		{
			// TODO: Report error
			ReadError = FString::Printf(TEXT("Hash not found in build manifest for %s"), **FileManifest);
			return false;
		}
	}
	else
	{
		ReadError = FString::Printf(TEXT("Hash not found in chunk map for %s"), **FileManifest);
		return false;
	}

	if (VFS->DoesChunkExist(Hash))
	{
		TFuture<TArray<uint8>> ExistingDataFuture = VFS->ReadData(Hash);
		ExistingDataFuture.Wait();
		TArray<uint8> ExistingData = ExistingDataFuture.Get();
		if (ExistingData.Num() == 0)
		{
			// TODO: Report error
			ReadError = FString::Printf(TEXT("Empty data found for hash %s %s"), *Hash.ToString(), **FileManifest);
			return false;
		}
		else
		{
			Result = MoveTemp(ExistingData);
			return true;
		}
	}

	ReadError = FString::Printf(TEXT("Chunk not found for hash %s"), *Hash.ToString());
	return false;
}

void FStreamingFileSystem::FinishRequest(FBuildPatchStreamResult Result, FIoRequestImpl* Request)
{
	//bool bInstallSuccess = Installer->CompletedSuccessfully();
	//Installer->HasError();
	//Installer->GetErrorType();


	//if (Result.ErrorType == EBuildPatchInstallError::ApplicationClosing)
	//{
	//	Request->SetFailed();
	//	CompleteRequest(Request, true);
	//}

	FString* Filename = ChunkMap.Find(Request->ChunkId);
	if (Filename)
	{
		UE_LOG(LogStreamingFileSystem, Verbose, TEXT("Completing request %s"), **Filename);
	}

	TArray<uint8> ExistingData;
	FString ReadError;
	if (BlockingReadFromVFS(Request->ChunkId, ExistingData, ReadError))
	{
		check(ExistingData.Num() > 0);

		ResolveRequestsInQueue(Request->ChunkId, ExistingData);
		CopyDataToRequest(Request, ExistingData);
	}
	else
	{
		UE_LOG(LogStreamingFileSystem, Error, TEXT("FinishRequest read error: %s"), *ReadError);

		Request->SetFailed();
		CompleteRequest(Request, true);
	}
}

void FStreamingFileSystem::CopyDataToRequest(FIoRequestImpl* Request, const TArray<uint8>& Data)
{
	uint64 RequestOffset = Request->Options.GetOffset();
	uint64 RequestSize = Request->Options.GetSize();
	check(RequestOffset + RequestSize <= Data.Num());
	check(Request->Options.GetTargetVa());

	Request->CreateBuffer(RequestSize);
	FIoBuffer& Buffer = Request->GetBuffer();
	check(Buffer.GetSize() == RequestSize);

	FMemory::Memcpy(Buffer.GetData(), Data.GetData() + RequestOffset, RequestSize);

	CompleteRequest(Request);
}

void FStreamingFileSystem::ResolveRequestsInQueue(FIoChunkId CompletedChunk, const TArray<uint8>& Data)
{
	// The queue is temporary while we wait for FORT-485014
	FScopeLock Lock(&RequestLock);
	TArray<FIoRequestImpl*> RemoveList;
	for (FIoRequestImpl* Element : Requests)
	{
		if (Element->ChunkId == CompletedChunk)
		{
			RemoveList.Add(Element);
			CopyDataToRequest(Element, Data);
		}
	}

	for (FIoRequestImpl* Element : RemoveList)
	{
		uint32 NumRemoved = Requests.Remove(Element);
		check(NumRemoved == 1);
	}
}

void FStreamingFileSystem::CompleteRequest(FIoRequestImpl* Request, bool bFailed /* = false*/)
{
	check((Request->IsCancelled() || bFailed) ^ Request->HasBuffer());
	check(!Request->NextRequest);
	FScopeLock Lock(&CompletedLock);
	if (!CompletedRequest)
	{
		CompletedRequest = Request;
		CompletedRequestsTail = Request;
	}
	else
	{
		CompletedRequestsTail->NextRequest = Request;
		CompletedRequestsTail = Request;
	}
}

void FStreamingFileSystem::UpdatePriorityRequest(FIoRequestImpl* Request)
{
	// TODO
}

void FStreamingFileSystem::DownloadRequest(FIoRequestImpl* Request)
{	
	FString* Filename = ChunkMap.Find(Request->ChunkId);
	if (Filename)
	{
		UE_LOG(LogStreamingFileSystem, Verbose, TEXT("Resolving Request %s"), **Filename);

		FRequestId RequestId = NextRequestId++;
		DownloadingRequests.Add(RequestId, Request);

		FBuildPatchStreamCompleteDelegate BuildPatchStreamCompleteDelegate = FBuildPatchStreamCompleteDelegate::CreateRaw(this, &FStreamingFileSystem::OnInstallComplete, RequestId);
		ContentStreamer->QueueFilesByName({ *Filename }, MoveTemp(BuildPatchStreamCompleteDelegate));
	}
	else
	{
		checkNoEntry();
	}
}

void FStreamingFileSystem::RunThread()
{
	while (!bIsShuttingDown)
	{
		// Complete downloaded requests
		FRequestId RequestId = 0;
		bool bFinishedRequest = false;
		while (DownloadedRequests.Dequeue(RequestId))
		{
			FIoRequestImpl** DownloadedRequest = DownloadingRequests.Find(RequestId);
			if (DownloadedRequest)
			{
				FinishRequest(FBuildPatchStreamResult{}, *DownloadedRequest);
				uint32 NumRemoved = DownloadingRequests.Remove(RequestId);
				check(NumRemoved == 1);

				NumRemoved = ProcessingRequests.Remove(*DownloadedRequest);
				check(NumRemoved == 1);
				bFinishedRequest = true;
			}
		}

		// Cancel downloading requests
		bool bCancelledRequest = false;
		FIoRequestImpl* CancelledRequest = nullptr;
		while (CancelledRequests.Dequeue(CancelledRequest))
		{
			uint32 NumRemoved = ProcessingRequests.Remove(CancelledRequest);
			check(NumRemoved == 0 || NumRemoved == 1);
			if (NumRemoved == 1)
			{
				CompleteRequest(CancelledRequest);
				bCancelledRequest = true;

				const FRequestId* Key = DownloadingRequests.FindKey(CancelledRequest);
				if (ensure(Key))
				{
					NumRemoved = DownloadingRequests.Remove(*Key);
					check(NumRemoved == 1);
				}
			}
		}

		if (bFinishedRequest || bCancelledRequest)
		{
			WakeUpDispatcherThreadDelegate.Execute();
		}

		// Start the next request
		FIoRequestImpl* NextRequest = nullptr;
		if (ProcessingRequests.Num() < MAX_DOWNLOADS)
		{
			FScopeLock Lock(&RequestLock);
			for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); RequestIndex++)
			{
				FIoRequestImpl* CurrentRequest = Requests[RequestIndex];

				bool bAlreadyProcessing = false;
				for (const FIoRequestImpl* ProcessingRequest : ProcessingRequests)
				{
					if (ProcessingRequest->ChunkId == CurrentRequest->ChunkId)
					{
						// We are already processing a request for the same file (though likely a different offset)
						bAlreadyProcessing = true;
						break;
					}
				}

				if (!bAlreadyProcessing)
				{
					NextRequest = Requests[RequestIndex];
					Requests.RemoveAt(RequestIndex);
					break;
				}
			}
		}

		if (NextRequest)
		{
			TArray<uint8> ExistingData;
			FString ReadError;
			// TODO: Handle possible error if not found in manifest (should be impossible)
			if (BlockingReadFromVFS(NextRequest->ChunkId, ExistingData, ReadError))
			{
				check(ExistingData.Num() > 0);

				CopyDataToRequest(NextRequest, ExistingData);

				WakeUpDispatcherThreadDelegate.Execute();
			}
			else
			{
				ProcessingRequests.Add(NextRequest);
				DownloadRequest(NextRequest);
			}
		}
		else
		{
			ThreadTrigger->Wait();
		}
	}

	// Shutting down
	// 
	// Cancel pending requests because the BuildPatchInstaller has shut down
	for (FIoRequestImpl* Request : ProcessingRequests)
	{
		Request->SetFailed();
		CompleteRequest(Request, true);
	}
	ProcessingRequests.Empty();

	FScopeLock Lock(&RequestLock);
	for (FIoRequestImpl* Request : Requests)
	{
		check(!Request->BackendData);
		Request->SetFailed();
		CompleteRequest(Request, true);
	}
	Requests.Empty();
}

void FStreamingFileSystem::OnInstallComplete(FBuildPatchStreamResult Result, FRequestId RequestId)
{
	DownloadedRequests.Enqueue(RequestId);
	ThreadTrigger->Trigger();
}

// Begin IIoDispatcherBackend API

bool FStreamingFileSystem::Resolve(FIoRequestImpl* Request)
{
	const FString* FileManifest = ChunkMap.Find(Request->ChunkId);
	if (FileManifest)
	{
		{
			FScopeLock Lock(&RequestLock);
			Requests.Add(Request);
		}
		ThreadTrigger->Trigger();
		return true;
	}
	else
	{
		return false;
	}
}

void FStreamingFileSystem::CancelIoRequest(FIoRequestImpl* Request)
{
	bool bCompleteRequest = false;
	{
		FScopeLock Lock(&RequestLock);
		int32 NumRemoved = Requests.Remove(Request);
		bCompleteRequest = NumRemoved == 1;
		check(NumRemoved == 0 || NumRemoved == 1);
	}

	if (bCompleteRequest)
	{
		CompleteRequest(Request);

		WakeUpDispatcherThreadDelegate.Execute();
	}
	else
	{
		CancelledRequests.Enqueue(Request);
	}

	ThreadTrigger->Trigger();
}

void FStreamingFileSystem::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
	// TODO
}

bool FStreamingFileSystem::DoesChunkExist(const FIoChunkId& Id) const
{
	// TODO: How do we handle requests that come in before we have loaded the manifest?
	//check(bInitialized);
	return !!ChunkMap.Find(Id);
}

TIoStatusOr<uint64> FStreamingFileSystem::GetSizeForChunk(const FIoChunkId& Id) const
{
	check(bInitialized);
	const FString* Filename = ChunkMap.Find(Id);
	if (Filename)
	{
		return BuildManifest->GetFileSize(*Filename);
	}

	return FIoStatus(EIoErrorCode::NotFound);
}

FIoRequestImpl* FStreamingFileSystem::GetCompletedRequests()
{
	FIoRequestImpl* Result = nullptr;

	FScopeLock Lock(&CompletedLock);
	Result = CompletedRequest;
	CompletedRequest = nullptr;
	CompletedRequestsTail = nullptr;

	return Result;
}

TIoStatusOr<FIoMappedRegion> FStreamingFileSystem::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return FIoStatus(EIoErrorCode::Unknown, TEXT("Memory mapped streaming is not supported"));
}

// End IIoDispatcherBackend API

TSharedRef<IStreamingFileSystem> IStreamingFileSystem::CreateStreamingFileSystem()
{
	return MakeShared<FStreamingFileSystem>(IVirtualFileCache::CreateVirtualFileCache());
}
