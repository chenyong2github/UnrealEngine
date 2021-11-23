// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheUSDStream.h"
#include "Async/Async.h"
#include "GeometryCacheMeshData.h"
#include "Misc/ScopeLock.h"

static int32 kUsdReadConcurrency = 10;

struct FGeometryCacheUsdStreamReadRequest
{
	FGeometryCacheMeshData* MeshData;
	int32 ReadIndex;
	int32 FrameIndex;
	bool Completed;
};

FGeometryCacheUsdStream::FGeometryCacheUsdStream(TWeakObjectPtr<UGeometryCacheTrackUsd> InUsdTrack, FReadUsdMeshFunction InReadFunc)
: UsdTrack(InUsdTrack)
, ReadFunc(InReadFunc)
, bCancellationRequested(false)
{
	for (int32 Index = 0; Index < kUsdReadConcurrency; ++Index)
	{
		// Populate the ReadIndices. Note that it used as a stack
		ReadIndices.Push(Index);

		// Populate pool of reusable ReadRequests
		ReadRequestsPool.Add(new FGeometryCacheUsdStreamReadRequest());
	}
}

FGeometryCacheUsdStream::~FGeometryCacheUsdStream()
{
	CancelRequests();

	// Delete all the cached MeshData
	FScopeLock Lock(&CriticalSection);
	for (FFrameIndexToMeshData::TIterator Iterator = FramesAvailable.CreateIterator(); Iterator; ++Iterator)
	{
		delete Iterator->Value;
	}

	// And ReadRequests from the pool
	for (int32 Index = 0; Index < ReadRequestsPool.Num(); ++Index)
	{
		delete ReadRequestsPool[Index];
	}
}

int32 FGeometryCacheUsdStream::CancelRequests()
{
	TGuardValue<TAtomic<bool>, bool> CancellationRequested(bCancellationRequested, true);

	// Clear the FramesNeeded to prevent scheduling further reads
	FramesNeeded.Empty();

	// Wait for all read requests to complete
	TArray<int32> CompletedFrames;
	while (FramesRequested.Num())
	{
		UpdateRequestStatus(CompletedFrames);
		if (FramesRequested.Num())
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

	return CompletedFrames.Num();
}

bool FGeometryCacheUsdStream::RequestFrameData()
{
	check(IsInGameThread());

	if (FramesNeeded.Num() == 0)
	{
		return false;
	}

	int32 FrameIndex = FramesNeeded[0];

	// Don't schedule the same FrameIndex twice
	for (int32 Index = 0; Index < FramesRequested.Num(); ++Index)
	{
		if (FramesRequested[Index]->FrameIndex == FrameIndex)
		{
			return true;
		}
	}

	if (ReadIndices.Num() > 0)
	{
		// Get any ReadIndex available
		const int32 ReadIndex = ReadIndices.Pop();

		// Take the ReadRequest from the pool at ReadIndex and initialize it
		FGeometryCacheUsdStreamReadRequest*& ReadRequest = ReadRequestsPool[ReadIndex];
		ReadRequest->FrameIndex = FrameIndex;
		ReadRequest->ReadIndex = ReadIndex;
		ReadRequest->MeshData = new FGeometryCacheMeshData;
		ReadRequest->Completed = false;

		// Change the frame status from needed to requested
		FramesNeeded.Remove(FrameIndex);
		FramesRequested.Add(ReadRequest);

		// Schedule asynchronous read of the MeshData
		Async(
#if WITH_EDITOR
			EAsyncExecution::LargeThreadPool,
#else
			EAsyncExecution::ThreadPool,
#endif // WITH_EDITOR
			[this, ReadRequest]()
			{
				if (!bCancellationRequested)
				{
					ReadFunc( UsdTrack, ReadRequest->FrameIndex, *ReadRequest->MeshData );
				}
				ReadRequest->Completed = true;
			});

		return true;
	}
	return false;
}

void FGeometryCacheUsdStream::UpdateRequestStatus(TArray<int32>& OutFramesCompleted)
{
	check(IsInGameThread());

	FScopeLock Lock(&CriticalSection);

	UGeometryCacheTrackUsd* Track = UsdTrack.Get();

	bool bCompletedARequest = false;

	// Check the completion status of the read requests in progress
	TArray<FGeometryCacheUsdStreamReadRequest*> CompletedRequests;
	for (FGeometryCacheUsdStreamReadRequest* ReadRequest : FramesRequested)
	{
		if (ReadRequest->Completed)
		{
			// Queue for removal after iterating
			CompletedRequests.Add(ReadRequest);

			// Cache result of read for retrieval later
			// #ueent_todo: Implement some sort of LRU cache to reduce memory usage
			FramesAvailable.Add(ReadRequest->FrameIndex, ReadRequest->MeshData);

			// Push back the ReadIndex for reuse
			ReadIndices.Push(ReadRequest->ReadIndex);

			// Output the completed frame
			OutFramesCompleted.Add(ReadRequest->FrameIndex);

			bCompletedARequest = true;
		}
	}

	for (FGeometryCacheUsdStreamReadRequest* ReadRequest : CompletedRequests)
	{
		FramesRequested.Remove(ReadRequest);
	}

	// We're fully done fetching what we need from USD for now, we can drop the track's strong stage reference so that the stage
	// can close if needed. The track can reopen the stage whenever needed though (it will also notify the user on the output
	// log if it did so: check CreateGeometryCache in USDGeomMeshTranslator.cpp).
	// This is not exactly ideal, as the closed stage may have changes that the reopened stage doesn't, but the
	// alternative would leave the stage open indefinitely (as the transaction buffer will keep the track alive for undo/redo),
	// which can lead to even more strange behavior (like changes that persist even if you close and open the stage, etc.).
	// Whenever the track reopens the stage it will use the stage cache anyway, so if that stage is open for any other reason
	// (e.g. we did an undo and the stage actor reopened it) then the operation will just retrieve the stage from the cache,
	// which is cheap.
	if ( bCompletedARequest && FramesNeeded.Num() == 0 && FramesRequested.Num() == 0 && Track )
	{
		Track->CurrentStage = UE::FUsdStage();
	}
}

void FGeometryCacheUsdStream::Prefetch(int32 StartFrameIndex, int32 NumFrames)
{
	UGeometryCacheTrackUsd* ValidUsdTrack = UsdTrack.Get();
	if ( !ValidUsdTrack )
	{
		return;
	}

	const int32 StartIndex = ValidUsdTrack->GetStartFrameIndex();
	const int32 EndIndex = ValidUsdTrack->GetEndFrameIndex();
	const int32 MaxNumFrames = EndIndex - StartIndex;

	// Validate the number of frames to be loaded
	if (NumFrames == 0)
	{
		// If no value specified, load the whole stream
		NumFrames = MaxNumFrames;
	}
	else
	{
		NumFrames = FMath::Clamp(NumFrames, 1, MaxNumFrames);
	}

	// Populate the list of frames needed to be loaded starting from given StartFrameIndex up to NumFrames or EndIndex
	StartFrameIndex = FMath::Clamp(StartFrameIndex, StartIndex, EndIndex);
	for (int32 Index = StartFrameIndex; NumFrames > 0 && Index < EndIndex; ++Index, --NumFrames)
	{
		FramesNeeded.Add(Index);
	}

	// End of the range might have been reached before the requested NumFrames so add the remaining frames starting from StartIndex
	for (int32 Index = StartIndex; NumFrames > 0; ++Index, --NumFrames)
	{
		FramesNeeded.Add(Index);
	}

	if (FramesNeeded.Num() > 0)
	{
		// Force the first frame to be loaded and ready for retrieval
		LoadFrameData(FramesNeeded[0]);
		FramesNeeded.RemoveAt(0);
	}
}

uint32 FGeometryCacheUsdStream::GetNumFramesNeeded()
{
	return FramesNeeded.Num();
}

bool FGeometryCacheUsdStream::GetFrameData(int32 FrameIndex, FGeometryCacheMeshData& OutMeshData)
{
	FScopeLock Lock(&CriticalSection);
	if (FGeometryCacheMeshData** MeshDataPtr = FramesAvailable.Find(FrameIndex))
	{
		OutMeshData = **MeshDataPtr;
		return true;
	}
	// If the user requested a frame that isn't loaded yet, synchronously fetch it right away
	// or else UGeometryCacheTrackUsd::GetSampleInfo may return invalid bounding boxes that may lead
	// to issues, and wouldn't otherwise be updated until that frame is requested again.
	// It may lead to a bit of stuttering when animating through an unloaded section with the sequencer
	// or by dragging the Time property of the stage actor, but the alternative would be a spam of
	// warnings on the output log and glitchy bounding boxes on the level
	else if ( IsInGameThread() )
	{
		LoadFrameData( FrameIndex );
		return GetFrameData( FrameIndex, OutMeshData );
	}

	return false;
}

void FGeometryCacheUsdStream::LoadFrameData(int32 FrameIndex)
{
	check(IsInGameThread());

	if ( FramesAvailable.Contains( FrameIndex ) )
	{
		return;
	}

	UGeometryCacheTrackUsd* Track = UsdTrack.Get();
	if ( !Track )
	{
		return;
	}

	// Synchronously load the requested frame data
	FGeometryCacheMeshData* MeshData = new FGeometryCacheMeshData;
	ReadFunc( UsdTrack, FrameIndex, *MeshData );

	FramesAvailable.Add(FrameIndex, MeshData);
}

const FGeometryCacheStreamStats& FGeometryCacheUsdStream::GetStreamStats() const
{
	static FGeometryCacheStreamStats NoStats;
	return NoStats;
}

void FGeometryCacheUsdStream::SetLimits(float MaxMemoryAllowed, float MaxCachedDuration)
{
}
