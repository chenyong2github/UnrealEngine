// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureStreaming.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#include "SparseVolumeTexture/SparseVolumeTextureSceneProxy.h"
#include "SparseVolumeTexture/SparseVolumeTextureData.h"
#include "RenderingThread.h"

#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/BulkDataReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureStreaming, Log, All);

int32 GSVTNumPrefetchFrames = 1;
static FAutoConsoleVariableRef CVarSVTStreamingPrefetchCount(
	TEXT("r.SparseVolumeTexture.Streaming.NumPrefetchFrames"), 
	GSVTNumPrefetchFrames,
	TEXT("Number of frames to prefetch when streaming animated SparseVolumeTexture frames."),
	ECVF_Scalability);

static int32 SVTFrameAndLevelToChunkIndex(int32 FrameIndex, int32 MipLevel, int32 NumFrames)
{
	return MipLevel * NumFrames + FrameIndex;
}

static int32 SVTChunkIndexToFrame(int32 ChunkIndex, int32 NumFrames)
{
	return ChunkIndex % NumFrames;
}

static int32 SVTChunkIndexToMipLevel(int32 ChunkIndex, int32 NumFrames)
{
	return ChunkIndex / NumFrames;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FLoadedSparseVolumeTextureChunk::~FLoadedSparseVolumeTextureChunk()
{
	checkf(Proxy.load() == nullptr, TEXT("Render proxy ptr not null (%p), ChunkIndex: %i"), Proxy.load(), ChunkIndex);
}

void FLoadedSparseVolumeTextureChunk::CleanUpIORequest()
{
	if (IORequest)
	{
		IORequest->WaitCompletion();
		delete IORequest;
		IORequest = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

FStreamingSparseVolumeTextureData::FStreamingSparseVolumeTextureData()
{
	ResetRequestedChunks();
}

FStreamingSparseVolumeTextureData::~FStreamingSparseVolumeTextureData()
{
}

bool FStreamingSparseVolumeTextureData::Initialize(UStreamableSparseVolumeTexture* InSparseVolumeTexture, FSparseVolumeTextureStreamingManager* InStreamingManager)
{
	check(InSparseVolumeTexture);

	if (InSparseVolumeTexture->GetFrames().IsEmpty())
	{
		UE_LOG(LogSparseVolumeTextureStreaming, Error, TEXT("Failed to initialize streaming SparseVolumeTexture due to lack of SVT or serialized stream frames. '%s'"), *InSparseVolumeTexture->GetFullName());
		return false;
	}

	SparseVolumeTexture = InSparseVolumeTexture;
	StreamingManager = InStreamingManager;

	// Always get the first frame of data so we can play immediately
	check(LoadedChunks.IsEmpty());
	check(LoadedChunkIndices.IsEmpty());
	check(!InSparseVolumeTexture->GetFrames().IsEmpty()); // Must hold at least the first frame

	AddNewLoadedChunk(0, InSparseVolumeTexture->GetFrames()[0].SparseVolumeTextureSceneProxy);
	LoadedChunkIndices.Add(0);

	return true;
}

void FStreamingSparseVolumeTextureData::FreeResources()
{
	// Make sure there are no pending requests in flight.
	for (int32 Pass = 0; Pass < 3; Pass++)
	{
		BlockTillAllRequestsFinished();
		if (!UpdateStreamingStatus())
		{
			break;
		}
		check(Pass < 2); // we should be done after two passes. Pass 0 will start anything we need and pass 1 will complete those requests
	}

	for (FLoadedSparseVolumeTextureChunk& LoadedChunk : LoadedChunks)
	{
		FreeLoadedChunk(LoadedChunk);
	}
}

bool FStreamingSparseVolumeTextureData::UpdateStreamingStatus()
{
	if (!SparseVolumeTexture)
	{
		return false;
	}

	// Handle failed chunks first
	for (int32 LoadedChunkIdx = 0; LoadedChunkIdx < LoadedChunks.Num(); ++LoadedChunkIdx)
	{
		FLoadedSparseVolumeTextureChunk& LoadedChunk = LoadedChunks[LoadedChunkIdx];
		if (LoadFailedChunkIndices.Contains(LoadedChunk.ChunkIndex))
		{
			// Mark as not loaded
			LoadedChunkIndices.Remove(LoadedChunk.ChunkIndex);

			// Remove this chunk
			FreeLoadedChunk(LoadedChunk);

			FScopeLock LoadedChunksLock(&LoadedChunksCriticalSection);
			LoadedChunks.RemoveAtSwap(LoadedChunkIdx, 1, false);
		}
	}

	LoadFailedChunkIndices.Reset();

	bool bHasPendingRequestInFlight = false;
	TArray<int32> IndicesToLoad;
	TArray<int32> IndicesToFree;

	if (HasPendingRequests(IndicesToLoad, IndicesToFree))
	{
		for (FLoadedSparseVolumeTextureChunk& LoadedChunk : LoadedChunks)
		{
			if (LoadedChunk.IORequest)
			{
				const bool bRequestFinished = LoadedChunk.IORequest->PollCompletion();
				bHasPendingRequestInFlight |= !bRequestFinished;
				if (bRequestFinished)
				{
					LoadedChunk.CleanUpIORequest();
				}
			}
		}

		LoadedChunkIndices = RequestedChunkIndices;

		BeginPendingRequests(IndicesToLoad, IndicesToFree);
	}

	ResetRequestedChunks();

	// Print out the currently resident frames to the screen
#if 0
	if (GEngine)
	{
#if WITH_EDITORONLY_DATA
		FString Message = TEXT("Referenced DDC SVT Frames: ");
#else
		FString Message = TEXT("Streaming SVT Frames in Memory: ");
#endif
		TArray<int32> InMemoryChunkIndices;
		for (FLoadedSparseVolumeTextureChunk& LoadedChunk : LoadedChunks)
		{
			if (LoadedChunk.bOwnsProxy)
			{
				InMemoryChunkIndices.Add(LoadedChunk.ChunkIndex);
			}
		}
		
		if (!InMemoryChunkIndices.IsEmpty())
		{
			const int32 NumFrames = SparseVolumeTexture->GetNumFrames();
			InMemoryChunkIndices.Sort();
			for (int32 ChunkIndex : InMemoryChunkIndices)
			{
				Message += FString::Format(TEXT("F:{0} M:{1}, "), { SVTChunkIndexToFrame(ChunkIndex, NumFrames), SVTChunkIndexToMipLevel(ChunkIndex, NumFrames) });
			}
			GEngine->AddOnScreenDebugMessage(-1, 0.1f, FColor::Yellow, Message);
		}
	}
#endif

	return bHasPendingRequestInFlight;
}

bool FStreamingSparseVolumeTextureData::HasPendingRequests(TArray<int32>& IndicesToLoad, TArray<int32>& IndicesToFree) const
{
	IndicesToLoad.Reset();
	IndicesToFree.Reset();

	// Find indices that aren't loaded
	for (int32 RequestedIndex : RequestedChunkIndices)
	{
		if (!LoadedChunkIndices.Contains(RequestedIndex))
		{
			IndicesToLoad.AddUnique(RequestedIndex);
		}
	}

	// Find indices that aren't needed anymore
	for (int32 LoadedIndex : LoadedChunkIndices)
	{
		if (!RequestedChunkIndices.Contains(LoadedIndex))
		{
			IndicesToFree.AddUnique(LoadedIndex);
		}
	}

	return IndicesToLoad.Num() > 0 || IndicesToFree.Num() > 0;
}

void FStreamingSparseVolumeTextureData::BeginPendingRequests(const TArray<int32>& IndicesToLoad, const TArray<int32>& IndicesToFree)
{
	// Mark chunks for removal in case they can be reused
	{
		for (int32 IndexToFree : IndicesToFree)
		{
			for (int32 LoadedChunkIdx = 0; LoadedChunkIdx < LoadedChunks.Num(); ++LoadedChunkIdx)
			{
				check(IndexToFree != 0);
				if (LoadedChunks[LoadedChunkIdx].ChunkIndex == IndexToFree)
				{
					FreeLoadedChunk(LoadedChunks[LoadedChunkIdx]);

					FScopeLock LoadedChunksLock(&LoadedChunksCriticalSection);
					LoadedChunks.RemoveAtSwap(LoadedChunkIdx, 1, false);
					break;
				}
			}
		}
	}

	// Set off all IO Requests
	const int32 NumFrames = SparseVolumeTexture->GetNumFrames();
	check(NumFrames > 0);
	const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_CriticalPath; //Set to Crit temporarily as emergency speculative fix for streaming issue
	TArrayView<const FSparseVolumeTextureFrame> SVTFrames = SparseVolumeTexture->GetFrames();
	for (int32 IndexToLoad : IndicesToLoad)
	{
		const int32 FrameToLoad = SVTChunkIndexToFrame(IndexToLoad, NumFrames) % NumFrames;
		const FSparseVolumeTextureFrame& Frame = SVTFrames[FrameToLoad];
		FSparseVolumeTextureSceneProxy* ExistingProxy = Frame.SparseVolumeTextureSceneProxy;
		FLoadedSparseVolumeTextureChunk& ChunkStorage = AddNewLoadedChunk(IndexToLoad, ExistingProxy);

		if (!ExistingProxy)
		{
			UE_CLOG(ChunkStorage.Proxy.load() != nullptr, LogSparseVolumeTextureStreaming, Fatal, TEXT("Existing render proxy for streaming SparseVolumeTexture frame."));
			UE_CLOG(ChunkStorage.IORequest, LogSparseVolumeTextureStreaming, Fatal, TEXT("Streaming SparseVolumeTexture frame already has IORequest."));

			const int64 ChunkSize = Frame.RuntimeStreamedInData.GetBulkDataSize();
			ChunkStorage.RequestStart = FPlatformTime::Seconds();
			UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("SparseVolumeTexture streaming request started %s Frame:%i At:%.3f\n"), *SparseVolumeTexture->GetName(), IndexToLoad, ChunkStorage.RequestStart);
			FBulkDataIORequestCallBack AsyncFileCallBack = [this, IndexToLoad, ChunkSize](bool bWasCancelled, IBulkDataIORequest* Req)
			{
				StreamingManager->OnAsyncFileCallback(this, IndexToLoad, ChunkSize, Req, bWasCancelled);
			};

			UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Loading streaming SparseVolumeTexture %s Frame:%i Offset:%i Size:%i File:%s\n"),
				*SparseVolumeTexture->GetName(), IndexToLoad, Frame.RuntimeStreamedInData.GetBulkDataOffsetInFile(), Frame.RuntimeStreamedInData.GetBulkDataSize(), *Frame.RuntimeStreamedInData.GetDebugName());
			ChunkStorage.IORequest = Frame.RuntimeStreamedInData.CreateStreamingRequest(AsyncIOPriority, &AsyncFileCallBack, nullptr);
			if (!ChunkStorage.IORequest)
			{
				UE_LOG(LogSparseVolumeTextureStreaming, Error, TEXT("SparseVolumeTexture streaming read request failed."));
			}
		}
	}
}

bool FStreamingSparseVolumeTextureData::BlockTillAllRequestsFinished(float TimeLimit)
{
	QUICK_SCOPE_CYCLE_COUNTER(FStreamingSparseVolumeTextureData_BlockTillAllRequestsFinished);
	if (TimeLimit == 0.0f)
	{
		for (FLoadedSparseVolumeTextureChunk& LoadedChunk : LoadedChunks)
		{
			LoadedChunk.CleanUpIORequest();
		}
	}
	else
	{
		double EndTime = FPlatformTime::Seconds() + TimeLimit;
		for (FLoadedSparseVolumeTextureChunk& LoadedChunk : LoadedChunks)
		{
			if (LoadedChunk.IORequest)
			{
				float ThisTimeLimit = EndTime - FPlatformTime::Seconds();
				if (ThisTimeLimit < 0.001f || // one ms is the granularity of the platform event system
					!LoadedChunk.IORequest->WaitCompletion(ThisTimeLimit))
				{
					return false;
				}

				LoadedChunk.CleanUpIORequest();
			}
		}
	}
	return true;
}

void FStreamingSparseVolumeTextureData::GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const
{
	FScopeLock LoadedChunksLock(&LoadedChunksCriticalSection);
	for (const FLoadedSparseVolumeTextureChunk& LoadedChunk : LoadedChunks)
	{
		const FSparseVolumeTextureSceneProxy* Proxy = LoadedChunk.Proxy.load();
		if (LoadedChunk.bOwnsProxy && Proxy)
		{
			Proxy->GetMemorySize(SizeCPU, SizeGPU);
		}
	}
}

FLoadedSparseVolumeTextureChunk& FStreamingSparseVolumeTextureData::AddNewLoadedChunk(int32 ChunkIndex, FSparseVolumeTextureSceneProxy* ExistingProxy)
{
	FScopeLock LoadedChunksLock(&LoadedChunksCriticalSection);
	const int32 NewIndex = LoadedChunks.AddDefaulted();

	LoadedChunks[NewIndex].Proxy = ExistingProxy;
	LoadedChunks[NewIndex].ChunkIndex = ChunkIndex;
	LoadedChunks[NewIndex].bOwnsProxy = false;
	return LoadedChunks[NewIndex];
}

void FStreamingSparseVolumeTextureData::FreeLoadedChunk(FLoadedSparseVolumeTextureChunk& LoadedChunk)
{
	if (LoadedChunk.IORequest)
	{
		LoadedChunk.IORequest->Cancel();
		LoadedChunk.IORequest->WaitCompletion();
		delete LoadedChunk.IORequest;
		LoadedChunk.IORequest = nullptr;
	}

	if (LoadedChunk.bOwnsProxy)
	{
		FSparseVolumeTextureSceneProxy* Proxy = LoadedChunk.Proxy.load();
		ENQUEUE_RENDER_COMMAND(FStreamingSparseVolumeTextureData_DeleteSVTProxy)(
			[Proxy](FRHICommandListImmediate& RHICmdList)
			{
				Proxy->ReleaseResource();
				delete Proxy; 
			});
	}

	LoadedChunk.Proxy = nullptr;
	LoadedChunk.bOwnsProxy = false;
	LoadedChunk.ChunkIndex = INDEX_NONE;
}

void FStreamingSparseVolumeTextureData::ResetRequestedChunks()
{
	RequestedChunkIndices.Reset();
	RequestedChunkIndices.Add(0); //Always want first frame
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureStreamingManager::FSparseVolumeTextureStreamingManager()
{
}

FSparseVolumeTextureStreamingManager::~FSparseVolumeTextureStreamingManager()
{
	checkf(StreamingSparseVolumeTextures.IsEmpty(), TEXT("FSparseVolumeTextureStreamingManager still has %i streaming SparseVolumeTextures registered!"), StreamingSparseVolumeTextures.Num());
}

void FSparseVolumeTextureStreamingManager::UpdateResourceStreaming(float DeltaTime, bool bProcessEverything)
{
	FScopeLock Lock(&CriticalSection);
	for (auto& StreamingSVTPair : StreamingSparseVolumeTextures)
	{
		StreamingSVTPair.Value->UpdateStreamingStatus();
	}
}

int32 FSparseVolumeTextureStreamingManager::BlockTillAllRequestsFinished(float TimeLimit, bool bLogResults)
{
	FScopeLock Lock(&CriticalSection);

	QUICK_SCOPE_CYCLE_COUNTER(FSparseVolumeTextureStreamingManager_BlockTillAllRequestsFinished);
	int32 Result = 0;

	if (TimeLimit == 0.0f)
	{
		for (auto& StreamingSVTPair : StreamingSparseVolumeTextures)
		{
			StreamingSVTPair.Value->BlockTillAllRequestsFinished();
		}
	}
	else
	{
		double EndTime = FPlatformTime::Seconds() + TimeLimit;
		for (auto& StreamingSVTPair : StreamingSparseVolumeTextures)
		{
			float ThisTimeLimit = EndTime - FPlatformTime::Seconds();
			if (ThisTimeLimit < .001f || // one ms is the granularity of the platform event system
				!StreamingSVTPair.Value->BlockTillAllRequestsFinished(ThisTimeLimit))
			{
				Result = 1; // we don't report the actual number, just 1 for any number of outstanding requests
				break;
			}
		}
	}

	return Result;
}

void FSparseVolumeTextureStreamingManager::AddSparseVolumeTexture(UStreamableSparseVolumeTexture* SparseVolumeTexture)
{
	FScopeLock Lock(&CriticalSection);
	if (StreamingSparseVolumeTextures.FindRef(SparseVolumeTexture) == nullptr)
	{
		FStreamingSparseVolumeTextureData* NewStreamingData = new FStreamingSparseVolumeTextureData();
		if (NewStreamingData->Initialize(SparseVolumeTexture, this))
		{
			StreamingSparseVolumeTextures.Add(SparseVolumeTexture, NewStreamingData);
		}
		else
		{
			delete NewStreamingData;
		}
	}
}

bool FSparseVolumeTextureStreamingManager::RemoveSparseVolumeTexture(UStreamableSparseVolumeTexture* SparseVolumeTexture)
{
	FScopeLock Lock(&CriticalSection);
	FStreamingSparseVolumeTextureData* StreamingData = StreamingSparseVolumeTextures.FindRef(SparseVolumeTexture);
	if (StreamingData)
	{
		StreamingSparseVolumeTextures.Remove(SparseVolumeTexture);

		// Free resources. This blocks pending IO requests
		StreamingData->FreeResources();
		delete StreamingData;
		return true;
	}
	return false;
}

void FSparseVolumeTextureStreamingManager::GetMemorySizeForSparseVolumeTexture(const UStreamableSparseVolumeTexture* SparseVolumeTexture, SIZE_T* SizeCPU, SIZE_T* SizeGPU) const
{
	FScopeLock Lock(&CriticalSection);
	FStreamingSparseVolumeTextureData* StreamingData = StreamingSparseVolumeTextures.FindRef(SparseVolumeTexture);
	if (StreamingData)
	{
		StreamingData->GetMemorySize(SizeCPU, SizeGPU);
	}
}

const FSparseVolumeTextureSceneProxy* FSparseVolumeTextureStreamingManager::GetSparseVolumeTextureSceneProxy(const UStreamableSparseVolumeTexture* SparseVolumeTexture, int32 FrameIndex, int32 MipLevel, bool bTrackAsRequested)
{
	MipLevel = 0; // Currently streaming all mips at once
	FScopeLock Lock(&CriticalSection);

	FStreamingSparseVolumeTextureData* StreamingData = StreamingSparseVolumeTextures.FindRef(SparseVolumeTexture);
	if (StreamingData)
	{
		const int32 NumFrames = SparseVolumeTexture->GetNumFrames();
		const int32 ChunkIndex = SVTFrameAndLevelToChunkIndex(FrameIndex, MipLevel, NumFrames);
		if (bTrackAsRequested)
		{
			StreamingData->RequestedChunkIndices.AddUnique(ChunkIndex);

			// Prefetch next frames
			const int32 NumPrefetchFrames = FMath::Clamp(GSVTNumPrefetchFrames, 0, NumFrames - 1);
			for (int32 i = 0; i < NumPrefetchFrames; ++i)
			{
				StreamingData->RequestedChunkIndices.AddUnique(SVTFrameAndLevelToChunkIndex((FrameIndex + 1 + i) % NumFrames, MipLevel, NumFrames));
			}
		}

		if (StreamingData->LoadedChunkIndices.Contains(ChunkIndex))
		{
			if (const FLoadedSparseVolumeTextureChunk* Frame = Algo::FindBy(StreamingData->LoadedChunks, ChunkIndex, &FLoadedSparseVolumeTextureChunk::ChunkIndex))
			{
				if (Frame->Proxy.load() == nullptr)
				{
					const double RequestTimer = Frame->RequestStart < 0.0 ? Frame->RequestStart : FPlatformTime::Seconds() - Frame->RequestStart;
					UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("No render proxy for loaded frame: %i, SVT: %s Request timer : %.3f"), FrameIndex, *SparseVolumeTexture->GetFullName(), RequestTimer);
				}
				return Frame->Proxy.load();
			}
			else
			{
				UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Unable to find requested frame: %i, SVT: %s - Is in LoadedChunkIndices however"), FrameIndex, *SparseVolumeTexture->GetFullName());
			}
		}
		else
		{
			UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Requested previously unknown frame: %i, SVT: %s"), FrameIndex, *SparseVolumeTexture->GetFullName());
		}
	}
	else
	{
		UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Tried to get frame for SVT that is not registered with the streaming manager SVT: %s"), *SparseVolumeTexture->GetFullName());
	}

	return nullptr;
}

void FSparseVolumeTextureStreamingManager::OnAsyncFileCallback(FStreamingSparseVolumeTextureData* StreamingSVTData, int32 ChunkIndex, int64 ReadSize, IBulkDataIORequest* ReadRequest, bool bWasCancelled)
{
	// Check to see if we successfully managed to load anything
	uint8* Mem = ReadRequest->GetReadResults();

	FScopeLock Lock(&StreamingSVTData->LoadedChunksCriticalSection);

	const int32 LoadedChunkIdx = StreamingSVTData->LoadedChunks.IndexOfByPredicate([ChunkIndex](const FLoadedSparseVolumeTextureChunk& Frame) { return Frame.ChunkIndex == ChunkIndex; });
	check(LoadedChunkIdx != INDEX_NONE);
	FLoadedSparseVolumeTextureChunk& ChunkStorage = StreamingSVTData->LoadedChunks[LoadedChunkIdx];

	const double CurrentTime = FPlatformTime::Seconds();
	const double RequestDuration = CurrentTime - ChunkStorage.RequestStart;

	if (Mem)
	{
		checkf(ChunkStorage.Proxy.load() == nullptr, TEXT("Chunk storage already has data. (0x%p) ChunkIndex:%i LoadedChunkIdx:%i"), ChunkStorage.Proxy.load(), ChunkIndex, LoadedChunkIdx);

		TArrayView<const uint8> MemView(Mem, ReadSize);
		FMemoryReaderView Reader(MemView);

		FSparseVolumeTextureSceneProxy* NewProxy = new FSparseVolumeTextureSceneProxy();
		FSparseVolumeTextureData TextureData;
		TextureData.Serialize(Reader);
		NewProxy->GetRuntimeData().Create(TextureData);
		BeginInitResource(NewProxy);

		ChunkStorage.Proxy = NewProxy;
		ChunkStorage.bOwnsProxy = true;
		ChunkStorage.RequestStart = -2.0; //Signify we have finished loading

		UE_LOG(LogSparseVolumeTextureStreaming, Log, TEXT("Request Finished %.2f\n SparseVolumeTexture Frame Streamed %.4f\n"), CurrentTime, RequestDuration);

		FMemory::Free(Mem);
	}
	else
	{
		const TCHAR* WasCancelledText = bWasCancelled ? TEXT("Yes") : TEXT("No");
		UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Streaming SparseVolumeTexture failed to load chunk: %i Load Duration:%.3f, SVT:%s WasCancelled: %s\n"), ChunkIndex, RequestDuration, *StreamingSVTData->SparseVolumeTexture->GetName(), WasCancelledText);
		
		StreamingSVTData->LoadFailedChunkIndices.AddUnique(ChunkIndex);
	}
}
