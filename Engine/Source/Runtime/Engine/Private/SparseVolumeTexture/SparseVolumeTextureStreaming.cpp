// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureStreaming.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
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

////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadedSparseVolumeTextureFrame::CleanUpIORequest()
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
	ResetRequestedFrames();
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
	check(LoadedFrames.IsEmpty());
	check(LoadedFrameIndices.IsEmpty());
	check(!InSparseVolumeTexture->GetFrames().IsEmpty()); // Must hold at least the first frame

	AddNewLoadedFrame(0, InSparseVolumeTexture->GetFrames()[0].SparseVolumeTextureSceneProxy);
	LoadedFrameIndices.Add(0);

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

	for (FLoadedSparseVolumeTextureFrame& LoadedFrame : LoadedFrames)
	{
		FreeLoadedFrame(LoadedFrame);
	}
}

bool FStreamingSparseVolumeTextureData::UpdateStreamingStatus()
{
	if (!SparseVolumeTexture)
	{
		return false;
	}

	// Handle failed frames first
	for (int32 LoadedFrameIdx = 0; LoadedFrameIdx < LoadedFrames.Num(); ++LoadedFrameIdx)
	{
		FLoadedSparseVolumeTextureFrame& LoadedFrame = LoadedFrames[LoadedFrameIdx];
		if (LoadFailedFrameIndices.Contains(LoadedFrame.FrameIndex))
		{
			// Mark as not loaded
			LoadedFrameIndices.Remove(LoadedFrame.FrameIndex);

			// Remove this frame
			FreeLoadedFrame(LoadedFrame);

			FScopeLock LoadedFramesLock(&LoadedFramesCriticalSection);
			LoadedFrames.RemoveAtSwap(LoadedFrameIdx, 1, false);
		}
	}

	LoadFailedFrameIndices.Reset();

	bool bHasPendingRequestInFlight = false;
	TArray<int32> IndicesToLoad;
	TArray<int32> IndicesToFree;

	if (HasPendingRequests(IndicesToLoad, IndicesToFree))
	{
		for (FLoadedSparseVolumeTextureFrame& LoadedFrame : LoadedFrames)
		{
			if (LoadedFrame.IORequest)
			{
				const bool bRequestFinished = LoadedFrame.IORequest->PollCompletion();
				bHasPendingRequestInFlight |= !bRequestFinished;
				if (bRequestFinished)
				{
					LoadedFrame.CleanUpIORequest();
				}
			}
		}

		LoadedFrameIndices = RequestedFrameIndices;

		BeginPendingRequests(IndicesToLoad, IndicesToFree);
	}

	ResetRequestedFrames();

	// Print out the currently resident frames to the screen
#if 0
	if (GEngine)
	{
#if WITH_EDITORONLY_DATA
		FString Message = TEXT("Referenced DDC SVT Frames: ");
#else
		FString Message = TEXT("Streaming SVT Frames in Memory: ");
#endif
		TArray<int32> FramesInMemoryIndices;
		for (FLoadedSparseVolumeTextureFrame& LoadedFrame : LoadedFrames)
		{
			if (LoadedFrame.bOwnsProxy)
			{
				FramesInMemoryIndices.Add(LoadedFrame.FrameIndex);
			}
		}
		
		if (!FramesInMemoryIndices.IsEmpty())
		{
			FramesInMemoryIndices.Sort();
			for (int32 FrameIndex : FramesInMemoryIndices)
			{
				Message += FString::Format(TEXT("{0}, "), { FrameIndex });
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
	for (int32 RequestedIndex : RequestedFrameIndices)
	{
		if (!LoadedFrameIndices.Contains(RequestedIndex))
		{
			IndicesToLoad.AddUnique(RequestedIndex);
		}
	}

	// Find indices that aren't needed anymore
	for (int32 LoadedIndex : LoadedFrameIndices)
	{
		if (!RequestedFrameIndices.Contains(LoadedIndex))
		{
			IndicesToFree.AddUnique(LoadedIndex);
		}
	}

	return IndicesToLoad.Num() > 0 || IndicesToFree.Num() > 0;
}

void FStreamingSparseVolumeTextureData::BeginPendingRequests(const TArray<int32>& IndicesToLoad, const TArray<int32>& IndicesToFree)
{
	// Mark frames for removal in case they can be reused
	{
		for (int32 IndexToFree : IndicesToFree)
		{
			for (int32 LoadedFrameIdx = 0; LoadedFrameIdx < LoadedFrames.Num(); ++LoadedFrameIdx)
			{
				check(IndexToFree != 0);
				if (LoadedFrames[LoadedFrameIdx].FrameIndex == IndexToFree)
				{
					FreeLoadedFrame(LoadedFrames[LoadedFrameIdx]);

					FScopeLock LoadedFramesLock(&LoadedFramesCriticalSection);
					LoadedFrames.RemoveAtSwap(LoadedFrameIdx, 1, false);
					break;
				}
			}
		}
	}

	// Set off all IO Requests

	const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_CriticalPath; //Set to Crit temporarily as emergency speculative fix for streaming issue
	TArrayView<const FSparseVolumeTextureFrame> SVTFrames = SparseVolumeTexture->GetFrames();
	for (int32 IndexToLoad : IndicesToLoad)
	{
		const FSparseVolumeTextureFrame& Frame = SVTFrames[IndexToLoad];
		FSparseVolumeTextureSceneProxy* ExistingProxy = Frame.SparseVolumeTextureSceneProxy;
		FLoadedSparseVolumeTextureFrame& FrameStorage = AddNewLoadedFrame(IndexToLoad, ExistingProxy);

		if (!ExistingProxy)
		{
			UE_CLOG(FrameStorage.Proxy.load() != nullptr, LogSparseVolumeTextureStreaming, Fatal, TEXT("Existing render proxy for streaming SparseVolumeTexture frame."));
			UE_CLOG(FrameStorage.IORequest, LogSparseVolumeTextureStreaming, Fatal, TEXT("Streaming SparseVolumeTexture frame already has IORequest."));

			int64 FrameSize = Frame.RuntimeStreamedInData.GetBulkDataSize();
			FrameStorage.RequestStart = FPlatformTime::Seconds();
			UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("SparseVolumeTexture streaming request started %s Frame:%i At:%.3f\n"), *SparseVolumeTexture->GetName(), IndexToLoad, FrameStorage.RequestStart);
			FBulkDataIORequestCallBack AsyncFileCallBack = [this, IndexToLoad, FrameSize](bool bWasCancelled, IBulkDataIORequest* Req)
			{
				StreamingManager->OnAsyncFileCallback(this, IndexToLoad, FrameSize, Req, bWasCancelled);
			};

			UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Loading streaming SparseVolumeTexture %s Frame:%i Offset:%i Size:%i File:%s\n"),
				*SparseVolumeTexture->GetName(), IndexToLoad, Frame.RuntimeStreamedInData.GetBulkDataOffsetInFile(), Frame.RuntimeStreamedInData.GetBulkDataSize(), *Frame.RuntimeStreamedInData.GetDebugName());
			FrameStorage.IORequest = Frame.RuntimeStreamedInData.CreateStreamingRequest(AsyncIOPriority, &AsyncFileCallBack, nullptr);
			if (!FrameStorage.IORequest)
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
		for (FLoadedSparseVolumeTextureFrame& LoadedFrame : LoadedFrames)
		{
			LoadedFrame.CleanUpIORequest();
		}
	}
	else
	{
		double EndTime = FPlatformTime::Seconds() + TimeLimit;
		for (FLoadedSparseVolumeTextureFrame& LoadedFrame : LoadedFrames)
		{
			if (LoadedFrame.IORequest)
			{
				float ThisTimeLimit = EndTime - FPlatformTime::Seconds();
				if (ThisTimeLimit < 0.001f || // one ms is the granularity of the platform event system
					!LoadedFrame.IORequest->WaitCompletion(ThisTimeLimit))
				{
					return false;
				}

				LoadedFrame.CleanUpIORequest();
			}
		}
	}
	return true;
}

void FStreamingSparseVolumeTextureData::GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const
{
	FScopeLock LoadedFramesLock(&LoadedFramesCriticalSection);
	for (const FLoadedSparseVolumeTextureFrame& LoadedFrame : LoadedFrames)
	{
		const FSparseVolumeTextureSceneProxy* Proxy = LoadedFrame.Proxy.load();
		if (LoadedFrame.bOwnsProxy && Proxy)
		{
			Proxy->GetMemorySize(SizeCPU, SizeGPU);
		}
	}
}

FLoadedSparseVolumeTextureFrame& FStreamingSparseVolumeTextureData::AddNewLoadedFrame(int32 FrameIndex, FSparseVolumeTextureSceneProxy* ExistingProxy)
{
	FScopeLock LoadedFramesLock(&LoadedFramesCriticalSection);
	const int32 NewIndex = LoadedFrames.AddDefaulted();

	LoadedFrames[NewIndex].Proxy = ExistingProxy;
	LoadedFrames[NewIndex].FrameIndex = FrameIndex;
	LoadedFrames[NewIndex].bOwnsProxy = false;
	return LoadedFrames[NewIndex];
}

void FStreamingSparseVolumeTextureData::FreeLoadedFrame(FLoadedSparseVolumeTextureFrame& LoadedFrame)
{
	if (LoadedFrame.IORequest)
	{
		LoadedFrame.IORequest->Cancel();
		LoadedFrame.IORequest->WaitCompletion();
		delete LoadedFrame.IORequest;
		LoadedFrame.IORequest = nullptr;
	}

	if (LoadedFrame.bOwnsProxy)
	{
		FSparseVolumeTextureSceneProxy* Proxy = LoadedFrame.Proxy.load();
		ENQUEUE_RENDER_COMMAND(FStreamingSparseVolumeTextureData_DeleteSVTProxy)(
			[Proxy](FRHICommandListImmediate& RHICmdList)
			{
				Proxy->ReleaseResource();
				delete Proxy; 
			});
	}

	LoadedFrame.Proxy = nullptr;
	LoadedFrame.bOwnsProxy = false;
	LoadedFrame.FrameIndex = INDEX_NONE;
}

void FStreamingSparseVolumeTextureData::ResetRequestedFrames()
{
	RequestedFrameIndices.Reset();
	RequestedFrameIndices.Add(0); //Always want first frame
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

const FSparseVolumeTextureSceneProxy* FSparseVolumeTextureStreamingManager::GetSparseVolumeTextureSceneProxy(const UStreamableSparseVolumeTexture* SparseVolumeTexture, int32 FrameIndex, bool bTrackAsRequested)
{
	FScopeLock Lock(&CriticalSection);

	FStreamingSparseVolumeTextureData* StreamingData = StreamingSparseVolumeTextures.FindRef(SparseVolumeTexture);
	if (StreamingData)
	{
		if (bTrackAsRequested)
		{
			StreamingData->RequestedFrameIndices.AddUnique(FrameIndex);

			// Prefetch next frames
			const int32 NumFrames = SparseVolumeTexture->GetFrames().Num();
			const int32 NumPrefetchFrames = FMath::Clamp(GSVTNumPrefetchFrames, 0, NumFrames - 1);
			for (int32 i = 0; i < NumPrefetchFrames; ++i)
			{
				StreamingData->RequestedFrameIndices.AddUnique((FrameIndex + 1 + i) % NumFrames);
			}
		}

		if (StreamingData->LoadedFrameIndices.Contains(FrameIndex))
		{
			if (const FLoadedSparseVolumeTextureFrame* Frame = Algo::FindBy(StreamingData->LoadedFrames, FrameIndex, &FLoadedSparseVolumeTextureFrame::FrameIndex))
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
				UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Unable to find requested frame: %i, SVT: %s - Is in LoadedFrameIndices however"), FrameIndex, *SparseVolumeTexture->GetFullName());
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

void FSparseVolumeTextureStreamingManager::OnAsyncFileCallback(FStreamingSparseVolumeTextureData* StreamingSVTData, int32 FrameIndex, int64 ReadSize, IBulkDataIORequest* ReadRequest, bool bWasCancelled)
{
	// Check to see if we successfully managed to load anything
	uint8* Mem = ReadRequest->GetReadResults();

	FScopeLock Lock(&StreamingSVTData->LoadedFramesCriticalSection);

	const int32 LoadedFrameIdx = StreamingSVTData->LoadedFrames.IndexOfByPredicate([FrameIndex](const FLoadedSparseVolumeTextureFrame& Frame) { return Frame.FrameIndex == FrameIndex; });
	check(LoadedFrameIdx != INDEX_NONE);
	FLoadedSparseVolumeTextureFrame& FrameStorage = StreamingSVTData->LoadedFrames[LoadedFrameIdx];

	const double CurrentTime = FPlatformTime::Seconds();
	const double RequestDuration = CurrentTime - FrameStorage.RequestStart;

	if (Mem)
	{
		checkf(FrameStorage.Proxy.load() == nullptr, TEXT("Frame storage already has data. (0x%p) FrameIndex:%i LoadedFrameIdx:%i"), FrameStorage.Proxy.load(), FrameIndex, LoadedFrameIdx);

		TArrayView<const uint8> MemView(Mem, ReadSize);
		FMemoryReaderView Reader(MemView);

		FSparseVolumeTextureSceneProxy* NewProxy = new FSparseVolumeTextureSceneProxy();
		NewProxy->GetRuntimeData().Serialize(Reader);
		BeginInitResource(NewProxy);

		FrameStorage.Proxy = NewProxy;
		FrameStorage.bOwnsProxy = true;
		FrameStorage.RequestStart = -2.0; //Signify we have finished loading

		UE_LOG(LogSparseVolumeTextureStreaming, Log, TEXT("Request Finished %.2f\n SparseVolumeTexture Frame Streamed %.4f\n"), CurrentTime, RequestDuration);

		FMemory::Free(Mem);
	}
	else
	{
		const TCHAR* WasCancelledText = bWasCancelled ? TEXT("Yes") : TEXT("No");
		UE_LOG(LogSparseVolumeTextureStreaming, Warning, TEXT("Streaming SparseVolumeTexture failed to load frame: %i Load Duration:%.3f, SVT:%s WasCancelled: %s\n"), FrameIndex, RequestDuration, *StreamingSVTData->SparseVolumeTexture->GetName(), WasCancelledText);
		
		StreamingSVTData->LoadFailedFrameIndices.AddUnique(FrameIndex);
	}
}
