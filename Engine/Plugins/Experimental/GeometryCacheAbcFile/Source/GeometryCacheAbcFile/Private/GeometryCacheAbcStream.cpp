// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheAbcStream.h"
#include "AbcFile.h"
#include "AbcUtilities.h"
#include "Async/Async.h"
#include "DerivedDataCacheInterface.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheStreamerSettings.h"
#include "GeometryCacheTrackAbcFile.h"
#include "Math/NumericLimits.h"
#include "Misc/CoreMisc.h"
#include "Misc/ScopeLock.h"

static bool GAbcStreamCacheInDDC = true;
static FAutoConsoleVariableRef CVarAbcStreamCacheInDDC(
	TEXT("GeometryCache.Streamer.AbcStream.CacheInDDC"),
	GAbcStreamCacheInDDC,
	TEXT("Cache the streamed Alembic mesh data in the DDC"));

// Max read concurrency is 8 due to limitation in AbcFile
static int32 kAbcReadConcurrency = 8;

enum class EAbcStreamReadRequestStatus
{
	Scheduled,
	Completed,
	Cancelled
};

struct FGeometryCacheAbcStreamReadRequest
{
	FGeometryCacheMeshData* MeshData = nullptr;
	int32 ReadIndex = 0;
	int32 FrameIndex = 0;
	EAbcStreamReadRequestStatus Status = EAbcStreamReadRequestStatus::Scheduled;
};

// If AbcStream derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define ABCSTREAM_DERIVED_DATA_VERSION TEXT("88025D2E38A54CF29FA5A6CAE686B013")

class FAbcStreamDDCUtils
{
private:
	static const FString& GetAbcStreamDerivedDataVersion()
	{
		static FString CachedVersionString(ABCSTREAM_DERIVED_DATA_VERSION);
		return CachedVersionString;
	}

	static FString BuildDerivedDataKey(const FString& KeySuffix)
	{
		return FDerivedDataCacheInterface::BuildCacheKey(TEXT("ABCSTREAM_"), *GetAbcStreamDerivedDataVersion(), *KeySuffix);
	}

	static FString BuildAbcStreamDerivedDataKeySuffix(UGeometryCacheTrackAbcFile* AbcTrack, int32 FrameIndex)
	{
		return AbcTrack->GetAbcTrackHash() + TEXT("_") + LexToString(FrameIndex);
	}

public:
	static FString GetAbcStreamDDCKey(UGeometryCacheTrackAbcFile* AbcTrack, int32 FrameIndex)
	{
		return BuildDerivedDataKey(BuildAbcStreamDerivedDataKeySuffix(AbcTrack, FrameIndex));
	}
};

FGeometryCacheAbcStream::FGeometryCacheAbcStream(UGeometryCacheTrackAbcFile* InAbcTrack)
: AbcTrack(InAbcTrack)
, bCancellationRequested(false)
, LastAccessedFrameIndex(0)
, MaxCachedFrames(0)
, MaxCachedDuration(0.f)
, MaxMemAllowed(TNumericLimits<float>::Max())
, MemoryUsed(0.f)
, bCacheNeedsUpdate(false)
{
	SecondsPerFrame = AbcTrack->GetAbcFile().GetSecondsPerFrame();
	Hash = AbcTrack->GetAbcTrackHash();

	for (int32 Index = 0; Index < kAbcReadConcurrency; ++Index)
	{
		// Populate the ReadIndices. Note that it used as a stack
		ReadIndices.Push(Index);

		// Populate pool of reusable ReadRequests
		ReadRequestsPool.Add(new FGeometryCacheAbcStreamReadRequest());
	}
}

FGeometryCacheAbcStream::~FGeometryCacheAbcStream()
{
	CancelRequests();

	// Delete all the cached MeshData
	FReadScopeLock ReadLock(FramesAvailableLock);
	for (const auto& Pair : FramesAvailable)
	{
		FGeometryCacheMeshData* MeshData = Pair.Value;
		DecrementMemoryStat(*MeshData);
		delete MeshData;
	}

	// And ReadRequests from the pool
	for (int32 Index = 0; Index < ReadRequestsPool.Num(); ++Index)
	{
		delete ReadRequestsPool[Index];
	}
}

int32 FGeometryCacheAbcStream::CancelRequests()
{
	TGuardValue<std::atomic<bool>, bool> CancellationRequested(bCancellationRequested, true);

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

void FGeometryCacheAbcStream::GetMeshData(int32 FrameIndex, int32 ConcurrencyIndex, FGeometryCacheMeshData& MeshData)
{
	// Get the mesh data straight from the Alembic file or from the DDC if it's already cached
	if (GAbcStreamCacheInDDC)
	{
		const FString DerivedDataKey = FAbcStreamDDCUtils::GetAbcStreamDDCKey(AbcTrack, FrameIndex);
		const FString& AbcFile = AbcTrack->GetSourceFile();

		TArray<uint8> DerivedData;
		if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData, AbcFile))
		{
			FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);
			Ar << MeshData;
		}
		else
		{
			FAbcUtilities::GetFrameMeshData(AbcTrack->GetAbcFile(), FrameIndex, MeshData, ConcurrencyIndex);

			FMemoryWriter Ar(DerivedData, true);
			Ar << MeshData;

			GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData, AbcFile);
		}
	}
	else
	{
		// Synchronously load the requested frame data
		FAbcUtilities::GetFrameMeshData(AbcTrack->GetAbcFile(), FrameIndex, MeshData, ConcurrencyIndex);
	}
}

bool FGeometryCacheAbcStream::RequestFrameData()
{
	check(IsInGameThread());

	if (FramesNeeded.Num() == 0)
	{
		return false;
	}

	FReadScopeLock ReadLock(FramesAvailableLock);

	// Get the next frame index to read that is not already available and not in flight
	int32 FrameIndex = FramesNeeded[0];
	bool bFrameIndexValid = false;
	while (FramesNeeded.Num() && !bFrameIndexValid)
	{
		FrameIndex = FramesNeeded[0];
		bFrameIndexValid = true;
		if (FramesAvailable.Contains(FrameIndex))
		{
			FramesNeeded.Remove(FrameIndex);
			bFrameIndexValid = false;
		}
		for (const FGeometryCacheAbcStreamReadRequest* Request : FramesRequested)
		{
			if (Request->FrameIndex == FrameIndex)
			{
				FramesNeeded.Remove(FrameIndex);
				bFrameIndexValid = false;
				break;
			}
		}
	}

	if (!bFrameIndexValid)
	{
		return false;
	}

	if (ReadIndices.Num() > 0)
	{
		// Get any ReadIndex available
		const int32 ReadIndex = ReadIndices.Pop(false);

		// Take the ReadRequest from the pool at ReadIndex and initialize it
		FGeometryCacheAbcStreamReadRequest*& ReadRequest = ReadRequestsPool[ReadIndex];
		ReadRequest->FrameIndex = FrameIndex;
		ReadRequest->ReadIndex = ReadIndex;
		ReadRequest->MeshData = new FGeometryCacheMeshData;
		ReadRequest->Status = EAbcStreamReadRequestStatus::Scheduled;

		// Change the frame status from needed to requested
		FramesNeeded.Remove(FrameIndex);
		FramesRequested.Add(ReadRequest);

		// Schedule asynchronous read of the MeshData
		Async(EAsyncExecution::LargeThreadPool,
			[this, ReadRequest]()
			{
				if (!bCancellationRequested)
				{
					checkSlow(ReadRequest->MeshData);
					GetMeshData(ReadRequest->FrameIndex, ReadRequest->ReadIndex, *ReadRequest->MeshData);
					ReadRequest->Status = EAbcStreamReadRequestStatus::Completed;
				}
				else
				{
					ReadRequest->Status = EAbcStreamReadRequestStatus::Cancelled;
				}
			});

		return true;
	}
	return false;
}

void FGeometryCacheAbcStream::UpdateRequestStatus(TArray<int32>& OutFramesCompleted)
{
	check(IsInGameThread());

	// Check if the cache needs to be updated either because the frame has advanced or because there's a new memory limit
	if (bCacheNeedsUpdate)
	{
		bCacheNeedsUpdate = false;
		UpdateFramesNeeded(LastAccessedFrameIndex, MaxCachedFrames);

		// Remove the unneeded frames, those that are available but don't need to be cached
		TArray<int32> UnneededFrames;
		{
			FReadScopeLock ReadLock(FramesAvailableLock);
			for (const auto& Pair : FramesAvailable)
			{
				int32 FrameIndex = Pair.Key;
				if (!FramesToBeCached.Contains(FrameIndex))
				{
					FGeometryCacheMeshData* MeshData = Pair.Value;
					DecrementMemoryStat(*MeshData);
					delete MeshData;

					UnneededFrames.Add(FrameIndex);
				}
			}
		}

		{
			FWriteScopeLock WriteLock(FramesAvailableLock);
			for (int32 FrameIndex : UnneededFrames)
			{
				FramesAvailable.Remove(FrameIndex);
			}
		}
	}

	FWriteScopeLock WriteLock(FramesAvailableLock);

	// Check the completion status of the read requests in progress
	TArray<FGeometryCacheAbcStreamReadRequest*> CompletedRequests;
	for (FGeometryCacheAbcStreamReadRequest* ReadRequest : FramesRequested)
	{
		// A cancelled read is still considered completed, it just hasn't read any data
		if (ReadRequest->Status == EAbcStreamReadRequestStatus::Completed ||
			ReadRequest->Status == EAbcStreamReadRequestStatus::Cancelled)
		{
			// Queue for removal after iterating
			CompletedRequests.Add(ReadRequest);

			// A cancelled read has an allocated mesh data that needs to be deleted
			bool bDeleteMeshData = ReadRequest->Status == EAbcStreamReadRequestStatus::Cancelled;
			if (ReadRequest->Status == EAbcStreamReadRequestStatus::Completed)
			{
				checkSlow(ReadRequest->MeshData);
				FResourceSizeEx ResSize;
				ReadRequest->MeshData->GetResourceSizeEx(ResSize);
				float FrameDataSize = float(ResSize.GetTotalMemoryBytes()) / (1024 * 1024);

				if (MemoryUsed + FrameDataSize < MaxMemAllowed)
				{
					if (!FramesAvailable.Contains(ReadRequest->FrameIndex))
					{
						// Cache result of read for retrieval later
						FramesAvailable.Add(ReadRequest->FrameIndex, ReadRequest->MeshData);
						IncrementMemoryStat(*ReadRequest->MeshData);
					}
					else
					{
						// The requested frame was already available, just delete it
						bDeleteMeshData = true;
					}
				}
				else
				{
					// The frame doesn't fit in the allowed memory budget so delete it
					// It should be requested again later if it's still needed
					bDeleteMeshData = true;
				}
			}

			// Push back the ReadIndex for reuse
			ReadIndices.Push(ReadRequest->ReadIndex);

			// Output the completed frame
			OutFramesCompleted.Add(ReadRequest->FrameIndex);

			if (bDeleteMeshData)
			{
				delete ReadRequest->MeshData;
				ReadRequest->MeshData = nullptr;
			}
		}
	}

	for (FGeometryCacheAbcStreamReadRequest* ReadRequest : CompletedRequests)
	{
		FramesRequested.Remove(ReadRequest);
	}
}

void FGeometryCacheAbcStream::UpdateFramesNeeded(int32 StartFrameIndex, int32 NumFrames)
{
	FramesToBeCached.Empty(NumFrames);
	FramesNeeded.Empty(NumFrames);

	const FAbcFile& AbcFile = AbcTrack->GetAbcFile();
	const int32 StartIndex = AbcFile.GetStartFrameIndex();
	const int32 EndIndex = AbcFile.GetEndFrameIndex();

	// FramesToBeCached are the frame indices that are required for playback, available or not,
	// while FramesNeeded are the frame indices that are not available yet so they need to be read
	auto AddFrameIndex = [this, &NumFrames](int32 FrameIndex)
	{
		FramesToBeCached.Add(FrameIndex);
		if (!FramesAvailable.Contains(FrameIndex))
		{
			FramesNeeded.Add(FrameIndex);
		}
		--NumFrames;
	};

	FReadScopeLock ReadLock(FramesAvailableLock);

	StartFrameIndex = FMath::Clamp(StartFrameIndex, StartIndex, EndIndex);

	// Also reserve space for the frame before start since playback is double-buffered
	int PreviousFrameIndex = FMath::Clamp(StartFrameIndex - 1, StartIndex, EndIndex);
	if (PreviousFrameIndex != StartFrameIndex)
	{
		--NumFrames;
	}

	// Populate the list of frame indices from given StartFrameIndex up to NumFrames or EndIndex
	for (int32 Index = StartFrameIndex; NumFrames > 0 && Index < EndIndex; ++Index)
	{
		AddFrameIndex(Index);
	}

	// End of the range might have been reached before the requested NumFrames so add the remaining frames starting from StartIndex
	for (int32 Index = StartIndex; NumFrames > 0 && Index < PreviousFrameIndex; ++Index)
	{
		AddFrameIndex(Index);
	}

	// Frame before start is added at the end to preserve the priority of the other frames
	if (PreviousFrameIndex != StartFrameIndex)
	{
		AddFrameIndex(PreviousFrameIndex);
	}
}

void FGeometryCacheAbcStream::Prefetch(int32 StartFrameIndex, int32 NumFrames)
{
	const FAbcFile& AbcFile = AbcTrack->GetAbcFile();
	const int32 MaxNumFrames = AbcFile.GetImportNumFrames();

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

	MaxCachedFrames = NumFrames;

	UpdateFramesNeeded(StartFrameIndex, NumFrames);

	if (FramesNeeded.Num() > 0)
	{
		// Force the first frame to be loaded and ready for retrieval
		LoadFrameData(FramesNeeded[0]);
		FramesNeeded.RemoveAt(0);
	}
}

uint32 FGeometryCacheAbcStream::GetNumFramesNeeded()
{
	return FramesNeeded.Num();
}

bool FGeometryCacheAbcStream::GetFrameData(int32 FrameIndex, FGeometryCacheMeshData& OutMeshData)
{
	// This function can be called from the render thread
	int32 PrevLastAccessedFrameIndex = LastAccessedFrameIndex.exchange(FrameIndex);
	if (PrevLastAccessedFrameIndex != FrameIndex)
	{
		bCacheNeedsUpdate = true;
	}

	{
		FReadScopeLock ReadLock(FramesAvailableLock);
		if (FGeometryCacheMeshData** MeshDataPtr = FramesAvailable.Find(FrameIndex))
		{
			OutMeshData = **MeshDataPtr;
			return true;
		}
	}

	return false;
}

void FGeometryCacheAbcStream::LoadFrameData(int32 FrameIndex)
{
	check(IsInGameThread());

	FWriteScopeLock WriteLock(FramesAvailableLock);
	if (FramesAvailable.Contains(FrameIndex))
	{
		return;
	}

	FGeometryCacheMeshData* MeshData = new FGeometryCacheMeshData;
	GetMeshData(FrameIndex, 0, *MeshData);
	FramesAvailable.Add(FrameIndex, MeshData);
	IncrementMemoryStat(*MeshData);
}

const FGeometryCacheStreamStats& FGeometryCacheAbcStream::GetStreamStats() const
{
	check(IsInGameThread());

	const int32 NumFrames = FramesAvailable.Num();
	const float Secs = SecondsPerFrame * NumFrames;

	Stats.NumCachedFrames = NumFrames;
	Stats.CachedDuration = Secs;
	Stats.MemoryUsed = MemoryUsed;
	Stats.AverageBitrate = MemoryUsed / Secs;
	return Stats;
}

void FGeometryCacheAbcStream::SetLimits(float InMaxMemoryAllowed, float InMaxCachedDuration)
{
	check(IsInGameThread());

	if (InMaxMemoryAllowed != MaxMemAllowed)
	{
		if (InMaxMemoryAllowed < MaxMemAllowed)
		{
			bCacheNeedsUpdate = true;
		}
		MaxMemAllowed = InMaxMemoryAllowed;
		MaxCachedDuration = FMath::Min(InMaxCachedDuration, AbcTrack->GetAbcFile().GetImportLength());
		MaxCachedFrames = FMath::Min(FMath::CeilToInt(MaxCachedDuration / SecondsPerFrame), AbcTrack->GetAbcFile().GetImportNumFrames());
	}
}

void FGeometryCacheAbcStream::IncrementMemoryStat(const FGeometryCacheMeshData& MeshData)
{
	FResourceSizeEx ResSize;
	MeshData.GetResourceSizeEx(ResSize);

	float SizeInBytes = ResSize.GetTotalMemoryBytes();
	MemoryUsed += SizeInBytes / (1024 * 1024);
}

void FGeometryCacheAbcStream::DecrementMemoryStat(const FGeometryCacheMeshData& MeshData)
{
	FResourceSizeEx ResSize;
	MeshData.GetResourceSizeEx(ResSize);

	float SizeInBytes = ResSize.GetTotalMemoryBytes();
	MemoryUsed -= SizeInBytes / (1024 * 1024);
}
