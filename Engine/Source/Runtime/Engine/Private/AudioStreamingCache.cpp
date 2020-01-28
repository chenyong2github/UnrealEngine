// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
AudioStreaming.cpp: Implementation of audio streaming classes.
=============================================================================*/

#include "AudioStreamingCache.h"
#include "Misc/CoreStats.h"
#include "Sound/SoundWave.h"
#include "Sound/AudioSettings.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Async/AsyncFileHandle.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "AudioDecompress.h"
#include "AudioCompressionSettingsUtils.h"

static int32 DebugMaxElementsDisplayCVar = 128;
FAutoConsoleVariableRef CVarDebugDisplayCaches(
	TEXT("au.streamcaching.MaxCachesToDisplay"),
	DebugMaxElementsDisplayCVar,
	TEXT("Sets the max amount of stream chunks to display on screen.\n")
	TEXT("n: Number of elements to display on screen."),
	ECVF_Default);

static int32 ForceBlockForLoadCVar = 0;
FAutoConsoleVariableRef CVarForceBlockForLoad(
	TEXT("au.streamcaching.ForceBlockForLoad"),
	ForceBlockForLoadCVar,
	TEXT("when set to a nonzero value, blocks GetLoadedChunk until the disk read is complete.\n")
	TEXT("n: Number of elements to display on screen."),
	ECVF_Default);

static int32 TrimCacheWhenOverBudgetCVar = 1;
FAutoConsoleVariableRef CVarTrimCacheWhenOverBudget(
	TEXT("au.streamcaching.TrimCacheWhenOverBudget"),
	TrimCacheWhenOverBudgetCVar,
	TEXT("when set to a nonzero value, TrimMemory will be called in AddOrTouchChunk to ensure we never go over budget.\n")
	TEXT("n: Number of elements to display on screen."),
	ECVF_Default);

static int32 ReadRequestPriorityCVar = 2;
FAutoConsoleVariableRef CVarReadRequestPriority(
	TEXT("au.streamcaching.ReadRequestPriority"),
	ReadRequestPriorityCVar,
	TEXT("This cvar sets the default request priority for audio chunks when Stream Caching is turned on.\n")
	TEXT("0: High, 1: Normal, 2: Below Normal, 3: Low, 4: Min"),
	ECVF_Default);

static float StreamCacheSizeOverrideMBCVar = 0.0f;
FAutoConsoleVariableRef CVarStreamCacheSizeOverrideMB(
	TEXT("au.streamcaching.StreamCacheSizeOverrideMB"),
	StreamCacheSizeOverrideMBCVar,
	TEXT("This cvar can be set to override the size of the cache.\n")
	TEXT("0: use cache size from project settings. n: the new cache size in megabytes."),
	ECVF_Default);

static FAutoConsoleCommand GFlushAudioCacheCommand(
	TEXT("au.streamcaching.FlushAudioCache"),
	TEXT("This will flush any non retained audio from the cache when Stream Caching is enabled."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		static constexpr uint64 NumBytesToFree = TNumericLimits<uint64>::Max() / 2;
		uint64 NumBytesFreed = IStreamingManager::Get().GetAudioStreamingManager().TrimMemory(NumBytesToFree);

		UE_LOG(LogAudio, Display, TEXT("Audio Cache Flushed! %d megabytes free."), NumBytesFreed / (1024.0 * 1024.0));
	})
);

static FAutoConsoleCommand GResizeAudioCacheCommand(
	TEXT("au.streamcaching.ResizeAudioCacheTo"),
	TEXT("This will try to cull enough audio chunks to shrink the audio stream cache to the new size if neccessary, and keep the cache at that size."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
{
	if (Args.Num() < 1)
	{
		return;
	}

	const float InMB = FCString::Atof(*Args[0]);

	if (InMB <= 0.0f)
	{
		return;
	}

	static IConsoleVariable* StreamCacheSizeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.streamcaching.StreamCacheSizeOverrideMB"));
	check(StreamCacheSizeCVar);

	uint64 NewCacheSizeInBytes = ((uint64)(InMB * 1024)) * 1024;
	uint64 OldCacheSizeInBytes = ((uint64)(StreamCacheSizeCVar->GetFloat() * 1024)) * 1024;

	// TODO: here we delete the difference between the old cache size and the new cache size,
	// but we don't actually need to do this unless the cache is full.
	// In the future we can use our current cache usage to figure out how much we need to trim.
	if (NewCacheSizeInBytes < OldCacheSizeInBytes)
	{
		uint64 NumBytesToFree = OldCacheSizeInBytes - NewCacheSizeInBytes;
		IStreamingManager::Get().GetAudioStreamingManager().TrimMemory(NumBytesToFree);
	}

	StreamCacheSizeCVar->Set(InMB);

	UE_LOG(LogAudio, Display, TEXT("Audio Cache Shrunk! Now set to be %f MB."), InMB);
})
);

static FAutoConsoleCommand GEnableProfilingAudioCacheCommand(
	TEXT("au.streamcaching.StartProfiling"),
	TEXT("This will start a performance-intensive profiling mode for this streaming manager. Profile stats can be output with audiomemreport."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	IStreamingManager::Get().GetAudioStreamingManager().SetProfilingMode(true);

	UE_LOG(LogAudio, Display, TEXT("Enabled profiling mode on the audio stream cache."));
})
);

static FAutoConsoleCommand GDisableProfilingAudioCacheCommand(
	TEXT("au.streamcaching.StopProfiling"),
	TEXT("This will start a performance-intensive profiling mode for this streaming manager. Profile stats can be output with audiomemreport."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	IStreamingManager::Get().GetAudioStreamingManager().SetProfilingMode(false);

	UE_LOG(LogAudio, Display, TEXT("Disabled profiling mode on the audio stream cache."));
})
);

FCachedAudioStreamingManager::FCachedAudioStreamingManager(const FCachedAudioStreamingManagerParams& InitParams)
{
	check(FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching());
	checkf(InitParams.Caches.Num() > 0, TEXT("FCachedAudioStreamingManager should be initialized with dimensions for at least one cache."));

	// const FAudioStreamCachingSettings& CacheSettings = FPlatformCompressionUtilities::GetStreamCachingSettingsForCurrentPlatform();
	for (const FCachedAudioStreamingManagerParams::FCacheDimensions& CacheDimensions : InitParams.Caches)
	{
		CacheArray.Emplace(CacheDimensions.MaxChunkSize, CacheDimensions.NumElements, CacheDimensions.MaxMemoryInBytes);
	}

	// Here we make sure our CacheArray is sorted from smallest MaxChunkSize to biggest, so that GetCacheForWave can scan through these caches to find the appropriate cache for the chunk size.
	CacheArray.Sort();
}

FCachedAudioStreamingManager::~FCachedAudioStreamingManager()
{
}

void FCachedAudioStreamingManager::UpdateResourceStreaming(float DeltaTime, bool bProcessEverything /*= false*/)
{
	// The cached audio streaming manager doesn't tick.
}

int32 FCachedAudioStreamingManager::BlockTillAllRequestsFinished(float TimeLimit, bool bLogResults)
{
	// TODO: Honor TimeLimit and bLogResults. Since we cancel any in flight read requests, this should not spin out.
	for (FAudioChunkCache& Cache : CacheArray)
	{
		Cache.CancelAllPendingLoads();
	}

	return 0;
}

void FCachedAudioStreamingManager::CancelForcedResources()
{
	// Unused.
}

void FCachedAudioStreamingManager::NotifyLevelChange()
{
	// Unused.
}

void FCachedAudioStreamingManager::SetDisregardWorldResourcesForFrames(int32 NumFrames)
{
	// Unused.
}

void FCachedAudioStreamingManager::AddLevel(class ULevel* Level)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveLevel(class ULevel* Level)
{
	// Unused.
}

void FCachedAudioStreamingManager::NotifyLevelOffset(class ULevel* Level, const FVector& Offset)
{
	// Unused.
}

void FCachedAudioStreamingManager::AddStreamingSoundWave(USoundWave* SoundWave)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveStreamingSoundWave(USoundWave* SoundWave)
{
	// Unused.
}

void FCachedAudioStreamingManager::AddDecoder(ICompressedAudioInfo* InCompressedAudioInfo)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveDecoder(ICompressedAudioInfo* InCompressedAudioInfo)
{
	//Unused.
}

bool FCachedAudioStreamingManager::IsManagedStreamingSoundWave(const USoundWave* SoundWave) const
{
	// Unused. The concept of a sound wave being "managed" doesn't apply here.
	checkf(false, TEXT("Not Implemented!"));
	return true;
}

bool FCachedAudioStreamingManager::IsStreamingInProgress(const USoundWave* SoundWave)
{
	// This function is used in USoundWave cleanup.
	// Since this manager owns the binary data we are streaming off of,
	// It's safe to delete the USoundWave as long as
	// There are NO sound sources playing with this Sound Wave.
	//
	// This is because a playing sound source might kick off a load for a new chunk,
	// which dereferences the corresponding USoundWave.
	//
	// As of right now, this is handled by USoundWave::FreeResources(), called
	// by USoundWave::IsReadyForFinishDestroy.
	return false;
}

bool FCachedAudioStreamingManager::CanCreateSoundSource(const FWaveInstance* WaveInstance) const
{
	return true;
}

void FCachedAudioStreamingManager::AddStreamingSoundSource(FSoundSource* SoundSource)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveStreamingSoundSource(FSoundSource* SoundSource)
{
	// Unused.
}

bool FCachedAudioStreamingManager::IsManagedStreamingSoundSource(const FSoundSource* SoundSource) const
{
	// Unused. The concept of a sound wave being "managed" doesn't apply here.
	checkf(false, TEXT("Not Implemented!"));
	return true;
}

FAudioChunkHandle FCachedAudioStreamingManager::GetLoadedChunk(const USoundWave* SoundWave, uint32 ChunkIndex, bool bBlockForLoad) const
{
	bBlockForLoad |= (ForceBlockForLoadCVar != 0);

	// If this sound wave is managed by a cache, use that to get the chunk:
	FAudioChunkCache* Cache = GetCacheForWave(SoundWave);
	if (Cache)
	{
		// With this code, the zeroth chunk should never get hit.
		checkf(ChunkIndex != 0, TEXT("Decoder tried to access the zeroth chunk through the streaming manager. Use USoundWave::GetZerothChunk() instead."));

		// TODO:  See if we can avoid non-const calls on the USoundWave here.
		USoundWave* MutableWave = const_cast<USoundWave*>(SoundWave);
		const FAudioChunkCache::FChunkKey ChunkKey =
		{
			  MutableWave
			, SoundWave->GetFName()
			, ChunkIndex
#if WITH_EDITOR
			, (uint32)SoundWave->CurrentChunkRevision.GetValue()
#endif
		};

		if (!FAudioChunkCache::IsKeyValid(ChunkKey))
		{
			UE_LOG(LogAudio, Warning, TEXT("Invalid Chunk Index %d Requested for Wave %s!"), ChunkIndex, *SoundWave->GetName());
			return FAudioChunkHandle();
		}

		// The function call below increments the reference count to the internal chunk.
		TArrayView<uint8> LoadedChunk = Cache->GetChunk(ChunkKey, bBlockForLoad);

		// Finally, if there's a chunk after this in the sound, request that it is in the cache.
		const int32 NextChunk = GetNextChunkIndex(SoundWave, ChunkIndex);

		if (NextChunk != INDEX_NONE)
		{
			const FAudioChunkCache::FChunkKey NextChunkKey = 
			{ 
				  MutableWave 
				, SoundWave->GetFName() 
				, ((uint32)NextChunk) 
#if WITH_EDITOR
				, (uint32)SoundWave->CurrentChunkRevision.GetValue()
#endif
			};

			bool bIsValidChunk = Cache->AddOrTouchChunk(NextChunkKey, [](EAudioChunkLoadResult) {}, ENamedThreads::AnyThread);
			if (!bIsValidChunk)
			{
				UE_LOG(LogAudio, Warning, TEXT("Cache overflow!!! couldn't load chunk %d for sound %s!"), ChunkIndex, *SoundWave->GetName());
			}
		}

		return BuildChunkHandle(LoadedChunk.GetData(), LoadedChunk.Num(), SoundWave, SoundWave->GetFName(), ChunkIndex);
	}
	else
	{
		ensureMsgf(false, TEXT("Failed to find cache for wave %s. Are you sure this is a streaming wave?"), *SoundWave->GetName());
		return FAudioChunkHandle();
	}
}

FAudioChunkCache* FCachedAudioStreamingManager::GetCacheForWave(const USoundWave* InSoundWave) const
{
	check(InSoundWave);

	// We only cache chunks beyond the zeroth chunk of audio (which is inlined directly on the asset)
	if (InSoundWave->RunningPlatformData && InSoundWave->RunningPlatformData->Chunks.Num() > 1)
	{
		const int32 SoundWaveChunkSize = InSoundWave->RunningPlatformData->Chunks[1].AudioDataSize;
		return GetCacheForChunkSize(SoundWaveChunkSize);
	}
	else
	{
		return nullptr;
	}
}

FAudioChunkCache* FCachedAudioStreamingManager::GetCacheForChunkSize(uint32 InChunkSize) const
{
	// Iterate over our caches until we find the lowest MaxChunkSize cache this sound's chunks will fit into. 
	for (int32 CacheIndex = 0; CacheIndex < CacheArray.Num(); CacheIndex++)
	{
		check(CacheArray[CacheIndex].MaxChunkSize >= 0);
		if (InChunkSize <= ((uint32) CacheArray[CacheIndex].MaxChunkSize))
		{
			return const_cast<FAudioChunkCache*>(&CacheArray[CacheIndex]);
		}
	}

	// If we ever hit this, something may have wrong during cook.
	// Please check to make sure this platform's implementation of IAudioFormat honors the MaxChunkSize parameter passed into SplitDataForStreaming,
	// or that FStreamedAudioCacheDerivedDataWorker::BuildStreamedAudio() is passing the correct MaxChunkSize to IAudioFormat::SplitDataForStreaming.
	ensureMsgf(false, TEXT("Chunks in SoundWave are too large: %d bytes"), InChunkSize);
	return nullptr;
}

int32 FCachedAudioStreamingManager::GetNextChunkIndex(const USoundWave* InSoundWave, uint32 CurrentChunkIndex) const
{
	check(InSoundWave);
	// TODO: Figure out a way to tell whether this wave is looping or not.
	// if(bNotLooping) return ((int32) CurrentChunkIndex) < (InSoundWave->RunningPlatformData->Chunks.Num() - 1);
	
	const int32 NumChunksTotal = InSoundWave->GetNumChunks();
	if (NumChunksTotal <= 2)
	{
		// If there's only one chunk to cache (besides the zeroth chunk, which is inlined),
		// We don't need to load anything.
		return INDEX_NONE;
	}
	else if(CurrentChunkIndex == (NumChunksTotal - 1))
	{
		// if we're on the last chunk, load the first chunk after the zeroth chunk.
		return 1;
	}
	else
	{
		// Otherwise, there's another chunk of audio after this one before the end of this file.
		return CurrentChunkIndex + 1;
	}
}

void FCachedAudioStreamingManager::AddReferenceToChunk(const FAudioChunkHandle& InHandle)
{
	FAudioChunkCache* Cache = GetCacheForChunkSize(InHandle.CachedDataNumBytes);
	check(Cache);

	FAudioChunkCache::FChunkKey ChunkKey =
	{
		  const_cast<USoundWave*>(InHandle.CorrespondingWave)
		, InHandle.CorrespondingWaveName
		, ((uint32) InHandle.ChunkIndex)
#if WITH_EDITOR
		, InHandle.ChunkGeneration
#endif
	};

	Cache->AddNewReferenceToChunk(ChunkKey);
}

void FCachedAudioStreamingManager::RemoveReferenceToChunk(const FAudioChunkHandle& InHandle)
{
	FAudioChunkCache* Cache = GetCacheForChunkSize(InHandle.CachedDataNumBytes);
	check(Cache);

	FAudioChunkCache::FChunkKey ChunkKey =
	{
		  const_cast<USoundWave*>(InHandle.CorrespondingWave)
		, InHandle.CorrespondingWaveName
		, ((uint32) InHandle.ChunkIndex)
#if WITH_EDITOR
		, InHandle.ChunkGeneration
#endif
	};

	Cache->RemoveReferenceToChunk(ChunkKey);
}

bool FCachedAudioStreamingManager::RequestChunk(USoundWave* SoundWave, uint32 ChunkIndex, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted, ENamedThreads::Type ThreadToCallOnLoadCompletedOn)
{
	FAudioChunkCache* Cache = GetCacheForWave(SoundWave);
	if (Cache)
	{
		FAudioChunkCache::FChunkKey ChunkKey = { SoundWave, SoundWave->GetFName(), ChunkIndex };
		return Cache->AddOrTouchChunk(ChunkKey, OnLoadCompleted, ThreadToCallOnLoadCompletedOn);
	}
	else
	{
		// This can hit if an out of bounds chunk was requested, or the zeroth chunk was requested from the streaming manager.
		ensureMsgf(false, TEXT("GetCacheForWave failed for SoundWave %s!"), *SoundWave->GetName());
		return false;
	}
}

FAudioChunkCache::FAudioChunkCache(uint32 InMaxChunkSize, uint32 NumChunks, uint64 InMemoryLimitInBytes)
	: MaxChunkSize(InMaxChunkSize)
	, MostRecentElement(nullptr)
	, LeastRecentElement(nullptr)
	, ChunksInUse(0)
	, MemoryCounterBytes(0)
	, MemoryLimitBytes(InMemoryLimitInBytes)
	, bLogCacheMisses(false)
{
	CachePool.Reset(NumChunks);
	for (uint32 Index = 0; Index < NumChunks; Index++)
	{
		CachePool.Emplace(MaxChunkSize, Index);
	}
}

FAudioChunkCache::~FAudioChunkCache()
{
	// While this is handled by the default destructor, we do this to ensure that we don't leak async read operations.
	CachePool.Reset();
	check(NumberOfLoadsInFlight.GetValue() == 0);
}

bool FAudioChunkCache::AddOrTouchChunk(const FChunkKey& InKey, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted, ENamedThreads::Type CallbackThread)
{
	// Update cache limit if needed.
	if (!FMath::IsNearlyZero(StreamCacheSizeOverrideMBCVar) && StreamCacheSizeOverrideMBCVar > 0.0f)
	{
		MemoryLimitBytes = ((uint64)(StreamCacheSizeOverrideMBCVar * 1024)) * 1024;
	}
	
	if (!IsKeyValid(InKey))
	{
		ensure(false);
		ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult::ChunkOutOfBounds, OnLoadCompleted, CallbackThread);
		return false;
	}

	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	FCacheElement* FoundElement = FindElementForKey(InKey);
	
	if (FoundElement)
	{
		TouchElement(FoundElement);
		if (FoundElement->bIsLoaded)
		{
			ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult::AlreadyLoaded, OnLoadCompleted, CallbackThread);
		}

#if DEBUG_STREAM_CACHE
		FoundElement->DebugInfo.NumTimesTouched++;
#endif

		return true;
	}
	else
	{
		FCacheElement* CacheElement = InsertChunk(InKey);

		if (!CacheElement)
		{
			ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult::CacheBlown, OnLoadCompleted, CallbackThread);
			return false;
		}

#if DEBUG_STREAM_CACHE
		CacheElement->DebugInfo.bWasCacheMiss = true;
#endif

		KickOffAsyncLoad(CacheElement, InKey, OnLoadCompleted, CallbackThread);

		if (TrimCacheWhenOverBudgetCVar != 0 && MemoryCounterBytes > MemoryLimitBytes)
		{
			TrimMemory(MemoryCounterBytes - MemoryLimitBytes);
		}

		return true;
	}
}

TArrayView<uint8> FAudioChunkCache::GetChunk(const FChunkKey& InKey, bool bBlockForLoadCompletion)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	FCacheElement* FoundElement = FindElementForKey(InKey);
	if (FoundElement)
	{
		TouchElement(FoundElement);
		if (FoundElement->IsLoadInProgress())
		{
			if (bBlockForLoadCompletion)
			{
				FoundElement->WaitForAsyncLoadCompletion(false);
			}
			else
			{
				return TArrayView<uint8>();
			}
		}
		

		// If this value is ever negative, it means that we're decrementing more than we're incrementing:
		check(FoundElement->NumConsumers.GetValue() >= 0);
		FoundElement->NumConsumers.Increment();
		return TArrayView<uint8>(FoundElement->ChunkData.GetData(), FoundElement->ChunkDataSize);
	}
	else
	{
		// If we missed it, kick off a new load with it.
		FoundElement = InsertChunk(InKey);

		if (!FoundElement)
		{
			return TArrayView<uint8>();
		}

		KickOffAsyncLoad(FoundElement, InKey, [](EAudioChunkLoadResult InResult) {}, ENamedThreads::AnyThread);

		if (bBlockForLoadCompletion)
		{
			// If bBlockForLoadCompletion was true and we don't have an element present, we have to load the element into the cache:
			FoundElement->WaitForAsyncLoadCompletion(false);

			FoundElement->NumConsumers.Increment();
			return TArrayView<uint8>(FoundElement->ChunkData.GetData(), FoundElement->ChunkDataSize);
		}
		else if (bLogCacheMisses)
		{
			// Chunks missing. Log this as a miss.
			const uint32 TotalNumChunksInWave = InKey.SoundWave->GetNumChunks();

			FCacheMissInfo CacheMissInfo = { InKey.SoundWaveName, InKey.ChunkIndex, TotalNumChunksInWave, false };
			CacheMissQueue.Enqueue(MoveTemp(CacheMissInfo));
		}

		// We missed, return an empty array view.
		return TArrayView<uint8>();
	}
}

void FAudioChunkCache::AddNewReferenceToChunk(const FChunkKey& InKey)
{
	FCacheElement* FoundElement = FindElementForKey(InKey);
	check(FoundElement);

	// If this value is ever negative, it means that we're decrementing more than we're incrementing:
	check(FoundElement->NumConsumers.GetValue() >= 0);
	FoundElement->NumConsumers.Increment();
}

void FAudioChunkCache::RemoveReferenceToChunk(const FChunkKey& InKey)
{
	FCacheElement* FoundElement = FindElementForKey(InKey);
	check(FoundElement);

	// If this value is ever less than 1 when we hit this code, it means that we're decrementing more than we're incrementing:
	check(FoundElement->NumConsumers.GetValue() >= 1);
	FoundElement->NumConsumers.Decrement();
}

void FAudioChunkCache::ClearCache()
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	const uint32 NumChunks = CachePool.Num();

	CachePool.Reset(NumChunks);
	check(NumberOfLoadsInFlight.GetValue() == 0);

	for (uint32 Index = 0; Index < NumChunks; Index++)
	{
		CachePool.Emplace(MaxChunkSize, Index);
	}

	MostRecentElement = nullptr;
	LeastRecentElement = nullptr;
	ChunksInUse = 0;
}

uint64 FAudioChunkCache::TrimMemory(uint64 BytesToFree)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	if (!MostRecentElement || MostRecentElement->LessRecentElement == nullptr)
	{
		return 0;
	}

	FCacheElement* CurrentElement = LeastRecentElement;

	// In order to avoid cycles, we always leave at least two chunks in the cache.
	const FCacheElement* ElementToStopAt = MostRecentElement->LessRecentElement;

	uint64 BytesFreed = 0;
	while (CurrentElement != ElementToStopAt && BytesFreed < BytesToFree)
	{
		if (CurrentElement->CanEvictChunk())
		{
			BytesFreed += CurrentElement->ChunkData.Num();
			MemoryCounterBytes -= CurrentElement->ChunkData.Num();
			// Empty the chunk data and invalidate the key.
			CurrentElement->ChunkData.Empty();
			CurrentElement->ChunkDataSize = 0;
			CurrentElement->Key = FChunkKey();

#if DEBUG_STREAM_CACHE
			// Reset debug info:
			CurrentElement->DebugInfo.Reset();
#endif
		}

		// Important to note that we don't actually relink chunks here,
		// So by trimming memory we are not moving chunks up the recency list.
		CurrentElement = CurrentElement->MoreRecentElement;
	}

	return BytesFreed;
}

void FAudioChunkCache::BlockForAllPendingLoads() const
{
	bool bLoadInProgress = false;

	float TimeStarted = FPlatformTime::Seconds();

	do 
	{
		// If we did find an in flight async load,
		// sleep to let other threads complete this task.
		if (bLoadInProgress)
		{
			float TimeSinceStarted = FPlatformTime::Seconds() - TimeStarted;
			UE_LOG(LogAudio, Log, TEXT("Waited %f seconds for async audio chunk loads."), TimeSinceStarted);
			FPlatformProcess::Sleep(0.0f);
		}

		{
			FScopeLock ScopeLock(const_cast<FCriticalSection*>(&CacheMutationCriticalSection));

			// Iterate through every element until we find one with a load in progress.
			const FCacheElement* CurrentElement = MostRecentElement;
			while (CurrentElement != nullptr)
			{
				bLoadInProgress |= CurrentElement->IsLoadInProgress();
				CurrentElement = CurrentElement->LessRecentElement;
			}
		}
	} while (bLoadInProgress);
}

void FAudioChunkCache::CancelAllPendingLoads()
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	FCacheElement* CurrentElement = MostRecentElement;
	while (CurrentElement != nullptr)
	{
		CurrentElement->WaitForAsyncLoadCompletion(true);
		CurrentElement = CurrentElement->LessRecentElement;
	}
}

uint64 FAudioChunkCache::ReportCacheSize()
{
	const uint32 NumChunks = CachePool.Num();

	return MaxChunkSize * NumChunks;
}

void FAudioChunkCache::BeginLoggingCacheMisses()
{
	bLogCacheMisses = true;
}

void FAudioChunkCache::StopLoggingCacheMisses()
{
	bLogCacheMisses = false;
}

FString FAudioChunkCache::FlushCacheMissLog()
{
	FString ConcatenatedCacheMisses;
	ConcatenatedCacheMisses.Append(TEXT("All Cache Misses:\nSoundWave:\t, ChunkIndex\n"));

	struct FMissedChunk 
	{
		FName SoundWaveName;
		int32 ChunkIndex;
		int32 MissCount;
	};

	struct FCacheMissSortPredicate
	{
		FORCEINLINE bool operator()(const FMissedChunk& A, const FMissedChunk& B) const
		{
			return A.MissCount < B.MissCount;
		}
	};

	TMap<FChunkKey, int32> CacheMissCount;

	FCacheMissInfo CacheMissInfo;
	while (CacheMissQueue.Dequeue(CacheMissInfo))
	{
		ConcatenatedCacheMisses.Append(CacheMissInfo.SoundWaveName.ToString());
		ConcatenatedCacheMisses.Append(TEXT("\t, "));
		ConcatenatedCacheMisses.AppendInt(CacheMissInfo.ChunkIndex);
		ConcatenatedCacheMisses.Append(TEXT("\n"));

		FChunkKey Chunk = 
		{ 
			nullptr, 
			CacheMissInfo.SoundWaveName, 
			CacheMissInfo.ChunkIndex,
#if WITH_EDITOR
			INDEX_NONE
#endif
		};

		int32& MissCount = CacheMissCount.FindOrAdd(Chunk);
		MissCount++;
	}

	// Sort our cache miss count map:
	TArray<FMissedChunk> ChunkMissArray;
	for (auto& CacheMiss : CacheMissCount)
	{
		FMissedChunk MissedChunk =
		{
			CacheMiss.Key.SoundWaveName,
			CacheMiss.Key.ChunkIndex,
			CacheMiss.Value
		};

		ChunkMissArray.Add(MissedChunk);
	}

	ChunkMissArray.Sort(FCacheMissSortPredicate());

	FString TopChunkMissesLog = TEXT("Most Missed Chunks:\n");
	TopChunkMissesLog += TEXT("Name:\t, Index:\t, Miss Count:\n");
	for (FMissedChunk& MissedChunk : ChunkMissArray)
	{
		TopChunkMissesLog.Append(MissedChunk.SoundWaveName.ToString());
		TopChunkMissesLog.Append(TEXT("\t, "));
		TopChunkMissesLog.AppendInt(MissedChunk.ChunkIndex);
		TopChunkMissesLog.Append(TEXT("\t, "));
		TopChunkMissesLog.AppendInt(MissedChunk.MissCount);
		TopChunkMissesLog.Append(TEXT("\n"));
	}

	return TopChunkMissesLog + TEXT("\n") + ConcatenatedCacheMisses;
}

FAudioChunkCache::FCacheElement* FAudioChunkCache::FindElementForKey(const FChunkKey& InKey)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	FCacheElement* CurrentElement = MostRecentElement;

#if DEBUG_STREAM_CACHE
	// In debuggable situations, we breadcrumb how far down the cache the cache we were.
	int32 ElementPosition = 0;
#endif

	while (CurrentElement != nullptr)
	{
		if (InKey == CurrentElement->Key)
		{
			
#if DEBUG_STREAM_CACHE
			float& CMA = CurrentElement->DebugInfo.AverageLocationInCacheWhenNeeded;
			CMA += ((ElementPosition - CMA) / (CurrentElement->DebugInfo.NumTimesTouched + 1));
#endif

			return CurrentElement;
		}
		else
		{
			CurrentElement = CurrentElement->LessRecentElement;

#if DEBUG_STREAM_CACHE
			ElementPosition++;
#endif
		}
	}

	return CurrentElement;
}

void FAudioChunkCache::TouchElement(FCacheElement* InElement)
{
	checkSlow(InElement);

	// Check to ensure we do not have any cycles in our list.
	// If this first check is hit, try to ensure that EvictLeastRecent chunk isn't evicting the top two chunks.
	check(MostRecentElement == nullptr || MostRecentElement != LeastRecentElement);
	check(InElement->LessRecentElement != InElement);

	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	// If this is already the most recent element, we don't need to do anything.
	if (InElement == MostRecentElement)
	{
		return;
	}

	// If this was previously the least recent chunk, update LeastRecentElement.
	if (LeastRecentElement == InElement)
	{
		LeastRecentElement = InElement->MoreRecentElement;
	}

	FCacheElement* PreviousLessRecent = InElement->LessRecentElement;
	FCacheElement* PreviousMoreRecent = InElement->MoreRecentElement;
	FCacheElement* PreviousMostRecent = MostRecentElement;

	check(PreviousMostRecent != InElement);

	// Move this element to the top:
	MostRecentElement = InElement;
	InElement->MoreRecentElement = nullptr;
	InElement->LessRecentElement = PreviousMostRecent;

	if (PreviousMostRecent != nullptr)
	{
		PreviousMostRecent->MoreRecentElement = InElement;
	}

	if (PreviousLessRecent == PreviousMoreRecent)
	{
		return;
	}
	else
	{
		// Link InElement's previous neighbors together:
		if (PreviousLessRecent != nullptr)
		{
			PreviousLessRecent->MoreRecentElement = PreviousMoreRecent;
		}

		if (PreviousMoreRecent != nullptr)
		{
			PreviousMoreRecent->LessRecentElement = PreviousLessRecent;
		}
	}
}

bool FAudioChunkCache::ShouldAddNewChunk() const
{
	return (ChunksInUse < CachePool.Num()) && (MemoryCounterBytes.Load() < MemoryLimitBytes);
}

FAudioChunkCache::FCacheElement* FAudioChunkCache::InsertChunk(const FChunkKey& InKey)
{
	FCacheElement* CacheElement = nullptr;
	
	{
		FScopeLock ScopeLock(&CacheMutationCriticalSection);

		if (ShouldAddNewChunk())
		{
			// We haven't filled up the pool yet, so we don't need to evict anything.
			CacheElement = &CachePool[ChunksInUse];
			ChunksInUse++;
		}
		else
		{
			// The pools filled, so we're going to need to evict.
			CacheElement = EvictLeastRecentChunk();

			if (!CacheElement)
			{
				return nullptr;
			}
		}
	}

	check(CacheElement);
	CacheElement->bIsLoaded = false;
	CacheElement->Key = InKey;
	TouchElement(CacheElement);

	// If we've got multiple chunks, we can not cache the least recent chunk
	// without worrying about a circular dependency.
	if (LeastRecentElement == nullptr && ChunksInUse > 1)
	{
		SetUpLeastRecentChunk();
	}

	return CacheElement;
}

void FAudioChunkCache::SetUpLeastRecentChunk()
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	FCacheElement* CacheElement = MostRecentElement;
	while (CacheElement->LessRecentElement != nullptr)
	{
		CacheElement = CacheElement->LessRecentElement;
	}

	LeastRecentElement = CacheElement;
}

FAudioChunkCache::FCacheElement* FAudioChunkCache::EvictLeastRecentChunk()
{
	FCacheElement* CacheElement = LeastRecentElement;

	// If the least recent chunk is evictable, evict it.
	if (CacheElement->CanEvictChunk())
	{
		FCacheElement* NewLeastRecentElement = CacheElement->MoreRecentElement;
		check(NewLeastRecentElement);

		LeastRecentElement = NewLeastRecentElement;
	}
	else
	{
		// We should never hit this code path unless we have at least two chunks active.
		check(MostRecentElement && MostRecentElement->LessRecentElement);

		// In order to avoid cycles, we always leave at least two chunks in the cache.
		const FCacheElement* ElementToStopAt = MostRecentElement->LessRecentElement;

		// Otherwise, we need to crawl up the cache from least recent used to most to find a chunk that is not in use:
		while (CacheElement != ElementToStopAt)
		{
			if (CacheElement->CanEvictChunk())
			{
				// Link the two neighboring chunks:
				if (CacheElement->MoreRecentElement)
				{
					CacheElement->MoreRecentElement->LessRecentElement = CacheElement->LessRecentElement;
				}
				
				// If we ever hit this while loop it means that CacheElement is not the least recently used element.
				check(CacheElement->LessRecentElement);
				CacheElement->LessRecentElement->MoreRecentElement = CacheElement->MoreRecentElement;
				break;
			}
			else
			{
				CacheElement = CacheElement->MoreRecentElement;
			}

			// If we ever hit this, it means that we couldn't find any cache elements that aren't in use.
			if (CacheElement != MostRecentElement)
			{
				ensureMsgf(false, TEXT("Cache blown! Please increase the cache size or load less audio."));
				return nullptr;
			}
		}
	}

#if DEBUG_STREAM_CACHE
	// Reset debug information:
	CacheElement->DebugInfo.Reset();
#endif

	return CacheElement;
}

void FAudioChunkCache::KickOffAsyncLoad(FCacheElement* CacheElement, const FChunkKey& InKey, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted, ENamedThreads::Type CallbackThread)
{
	LLM_SCOPE(ELLMTag::Audio);
	check(CacheElement);
	
	const FStreamedAudioChunk& Chunk = InKey.SoundWave->RunningPlatformData->Chunks[InKey.ChunkIndex];
	int32 ChunkDataSize = Chunk.AudioDataSize;

	EAsyncIOPriorityAndFlags AsyncIOPriority = GetAsyncPriorityForChunk(InKey);

	MemoryCounterBytes -= CacheElement->ChunkData.Num();
	// Reallocate our chunk data This allows us to shrink if possible.
	CacheElement->ChunkData.SetNumUninitialized(Chunk.AudioDataSize, true);
	MemoryCounterBytes += CacheElement->ChunkData.Num();

#if DEBUG_STREAM_CACHE
	CacheElement->DebugInfo.NumTotalChunks = InKey.SoundWave->GetNumChunks() - 1;
#endif

	// In editor, we retrieve from the DDC. In non-editor situations, we read the chunk async from the pak file.
#if WITH_EDITORONLY_DATA
	if (Chunk.DerivedDataKey.IsEmpty() == false)
	{
		CacheElement->ChunkDataSize = ChunkDataSize;

		INC_DWORD_STAT_BY(STAT_AudioMemorySize, ChunkDataSize);
		INC_DWORD_STAT_BY(STAT_AudioMemory, ChunkDataSize);

		if (CacheElement->DDCTask.IsValid())
		{
			check(CacheElement->DDCTask->IsDone());
		}
		
#if DEBUG_STREAM_CACHE
		CacheElement->DebugInfo.TimeLoadStarted = FPlatformTime::Cycles64();
#endif


		TFunction<void(bool)> OnLoadComplete = [OnLoadCompleted, CallbackThread, CacheElement, InKey, ChunkDataSize](bool bRequestFailed)
		{
			// Populate key and DataSize. The async read request was set up to write directly into CacheElement->ChunkData.
			CacheElement->Key = InKey;
			CacheElement->ChunkDataSize = ChunkDataSize;
			CacheElement->bIsLoaded = true;

#if DEBUG_STREAM_CACHE
			CacheElement->DebugInfo.TimeToLoad = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - CacheElement->DebugInfo.TimeLoadStarted);
#endif
			EAudioChunkLoadResult ChunkLoadResult = bRequestFailed ? EAudioChunkLoadResult::Interrupted : EAudioChunkLoadResult::Completed;
			ExecuteOnLoadCompleteCallback(ChunkLoadResult, OnLoadCompleted, CallbackThread);
		};

		NumberOfLoadsInFlight.Increment();

		CacheElement->DDCTask.Reset(new FAsyncStreamDerivedChunkTask(
			Chunk.DerivedDataKey,
			CacheElement->ChunkData.GetData(),
			ChunkDataSize,
			&NumberOfLoadsInFlight,
			MoveTemp(OnLoadComplete)
		));

		CacheElement->DDCTask->StartBackgroundTask();
	}
	else
#endif // #if WITH_EDITORONLY_DATA
	{
		if (CacheElement->IsLoadInProgress())
		{
			CacheElement->WaitForAsyncLoadCompletion(true);
		}

		// Sanity check our bulk data against
		const int32 ChunkBulkDataSize = Chunk.BulkData.GetBulkDataSize();
		check(ChunkDataSize <= ChunkBulkDataSize);
		check(((int32)ChunkDataSize) <= CacheElement->ChunkData.Num());

		// If we ever want to eliminate zero-padding in chunks, that could be done here:
		//ensureAlwaysMsgf(AudioChunkSize == ChunkBulkDataSize, TEXT("For memory load on demand, we do not zero-pad to page sizes."));
		
		NumberOfLoadsInFlight.Increment();

		FBulkDataIORequestCallBack AsyncFileCallBack = [this, OnLoadCompleted, CacheElement, InKey, ChunkDataSize](bool bWasCancelled, IBulkDataIORequest*)
		{
			// Populate key and DataSize. The async read request was set up to write directly into CacheElement->ChunkData.
			CacheElement->Key = InKey;
			CacheElement->ChunkDataSize = ChunkDataSize;
			CacheElement->bIsLoaded = true;

#if DEBUG_STREAM_CACHE
			CacheElement->DebugInfo.TimeToLoad = (FPlatformTime::Seconds() - CacheElement->DebugInfo.TimeLoadStarted) * 1000.0f;
#endif

			OnLoadCompleted(bWasCancelled ? EAudioChunkLoadResult::Interrupted : EAudioChunkLoadResult::Completed);

			NumberOfLoadsInFlight.Decrement();
		};

#if DEBUG_STREAM_CACHE
		CacheElement->DebugInfo.TimeLoadStarted = FPlatformTime::Seconds();
#endif

		CacheElement->ReadRequest.Reset(Chunk.BulkData.CreateStreamingRequest( 0, ChunkDataSize, AsyncIOPriority, &AsyncFileCallBack, CacheElement->ChunkData.GetData()));
		if (!CacheElement->ReadRequest.IsValid())
		{
			UE_LOG(LogAudio, Error, TEXT("Chunk load in audio LRU cache failed."));
			OnLoadCompleted(EAudioChunkLoadResult::ChunkOutOfBounds);
			NumberOfLoadsInFlight.Decrement();
		}
	}
}

EAsyncIOPriorityAndFlags FAudioChunkCache::GetAsyncPriorityForChunk(const FChunkKey& InKey)
{

	// TODO: In the future we can add an enum to USoundWaves to tweak load priority of individual assets.

	switch (ReadRequestPriorityCVar)
	{
		case 4:
		{
			return AIOP_MIN;
		}
		case 3:
		{
			return AIOP_Low;
		}
		case 2:
		{
			return AIOP_BelowNormal;
		}
		case 1:
		{
			return AIOP_Normal;
		}
		case 0:
		default:
		{
			return AIOP_High;
		}
	}
}

void FAudioChunkCache::ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult Result, const TFunction<void(EAudioChunkLoadResult)>& OnLoadCompleted, const ENamedThreads::Type& CallbackThread)
{
	if (CallbackThread == ENamedThreads::AnyThread)
	{
		OnLoadCompleted(Result);
	}
	else
	{
		// Dispatch an async notify.
		AsyncTask(CallbackThread, [OnLoadCompleted, Result]() 
		{
			OnLoadCompleted(Result);
		});
	}
}

bool FAudioChunkCache::IsKeyValid(const FChunkKey& InKey)
{
	return InKey.ChunkIndex < TNumericLimits<uint32>::Max() && ((int32)InKey.ChunkIndex) < InKey.SoundWave->RunningPlatformData->Chunks.Num();
}

#include "UnrealEngine.h"

int32 FCachedAudioStreamingManager::RenderStatAudioStreaming(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	Canvas->DrawShadowedString(X, Y, TEXT("Stream Caches:"), UEngine::GetSmallFont(), FLinearColor::White);
	Y += 12;

	int32 CacheIndex = 0;
	int32 Height = Y;
	for (const FAudioChunkCache& Cache : CacheArray)
	{
		FString CacheTitle = *FString::Printf(TEXT("Cache %d"), CacheIndex);
		Canvas->DrawShadowedString(X, Y, *CacheTitle, UEngine::GetSmallFont(), FLinearColor::White);
		Y += 12;

		TPair<int, int> Size = Cache.DebugDisplay(World, Viewport, Canvas, X, Y, ViewLocation, ViewRotation);
		
		// Separate caches are laid out horizontally across the screen, so the total height is equal to our tallest cache panel:
		X += Size.Key;
		Height = FMath::Max(Height, Size.Value);
	}

	return Y + Height;
}

FString FCachedAudioStreamingManager::GenerateMemoryReport()
{
	FString OutputString;
	for (FAudioChunkCache& Cache : CacheArray)
	{
		OutputString += Cache.DebugPrint();
	}

	return OutputString;
}

void FCachedAudioStreamingManager::SetProfilingMode(bool bEnabled)
{
	if (bEnabled)
	{
		for (FAudioChunkCache& Cache : CacheArray)
		{
			Cache.BeginLoggingCacheMisses();
		}
	}
	else
	{
		for (FAudioChunkCache& Cache : CacheArray)
		{
			Cache.StopLoggingCacheMisses();
		}
	}
}

uint64 FCachedAudioStreamingManager::TrimMemory(uint64 NumBytesToFree)
{
	uint64 NumBytesLeftToFree = NumBytesToFree;
	
	// TODO: When we support multiple caches, it's probably best to do this in reverse,
	// since the caches are sorted from shortest sounds to longest.
	// Freeing longer chunks will get us bigger gains and (presumably) have lower churn.
	for (FAudioChunkCache& Cache : CacheArray)
	{
		uint64 NumBytesFreed = Cache.TrimMemory(NumBytesLeftToFree);

		// NumBytesFreed could potentially be more than what we requested to free (since we delete whole chunks at once).
		NumBytesLeftToFree -= FMath::Min(NumBytesFreed, NumBytesLeftToFree);

		// If we've freed all the memory we needed to, exit.
		if (NumBytesLeftToFree == 0)
		{
			break;
		}
	}

	check(NumBytesLeftToFree <= NumBytesToFree);
	uint64 TotalBytesFreed = NumBytesToFree - NumBytesLeftToFree;

	UE_LOG(LogAudio, Display, TEXT("Call to IAudioStreamingManager::TrimMemory successfully freed %lu of the requested %lu bytes."), TotalBytesFreed, NumBytesToFree);
	return TotalBytesFreed;
}

#include "Engine/Font.h"

TPair<int, int> FAudioChunkCache::DebugDisplay(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation) const
{
	FScopeLock ScopeLock(const_cast<FCriticalSection*>(&CacheMutationCriticalSection));

	// Color scheme:
	static constexpr float ColorMax = 256.0f;


	// Chunk color for a single retainer.
	const FLinearColor RetainChunkColor(44.0f / ColorMax, 207.0f / ColorMax, 47 / ColorMax);

	// Chunk color we lerp to as more retainers are added for a chunk.
	const FLinearColor TotalMassRetainChunkColor(204 / ColorMax, 126 / ColorMax, 43 / ColorMax);

	// A chunk that's loaded but not retained.
	const FLinearColor LoadedChunkColor(47 / ColorMax, 44 / ColorMax, 207 / ColorMax);

	// A chunk that's been trimmed by TrimMemory.
	const FLinearColor TrimmedChunkColor(204 / ColorMax, 46 / ColorMax, 43 / ColorMax);

	// In editor builds, this is a chunk that was built in a previous version of the cook quality settings.
	const FLinearColor StaleChunkColor(143 / ColorMax, 73 / ColorMax, 70 / ColorMax);
	
	// A chunk that currently has an async load in flight.
	const FLinearColor CurrentlyLoadingChunkColor = FLinearColor::Yellow;


	const int32 InitialX = X;
	const int32 InitialY = Y;

	FString NumElementsDetail = *FString::Printf(TEXT("Number of chunks loaded: %d of %d"), ChunksInUse, CachePool.Num());

	// Offset our number of elements loaded horizontally to the right next to the cache title:
	int32 CacheTitleOffsetX = 0;
	int32 CacheTitleOffsetY = 0;
	UEngine::GetSmallFont()->GetStringHeightAndWidth(TEXT("Cache XX "), CacheTitleOffsetY, CacheTitleOffsetX);

	Canvas->DrawShadowedString(X + CacheTitleOffsetX, Y - 12, *NumElementsDetail, UEngine::GetSmallFont(), FLinearColor::Green);
	Y += 10;

	// First pass: We run through and get a snap shot of the amount of memory currently in use.
	FCacheElement* CurrentElement = MostRecentElement;
	uint32 NumBytesCounter = 0;

	while (CurrentElement != nullptr)
	{
		// Note: this is potentially a stale value if we're in the middle of FCacheElement::KickOffAsyncLoad.
		NumBytesCounter += CurrentElement->ChunkData.Num();
		CurrentElement = CurrentElement->LessRecentElement;
	}
	
	// Convert to megabytes and print the total size:
	const double NumMegabytesInUse = (double)NumBytesCounter / (1024 * 1024);
	const double MaxCacheSizeMB = ((double)MemoryLimitBytes) / (1024 * 1024);

	FString CacheMemoryUsage = *FString::Printf(TEXT("Using: %.4f Megabytes (%lu bytes). Max Potential Usage: %.4f Megabytes."), NumMegabytesInUse, MemoryCounterBytes.Load(),  MaxCacheSizeMB);

	// We're going to align this horizontally with the number of elements right above it.
	Canvas->DrawShadowedString(X + CacheTitleOffsetX, Y, *CacheMemoryUsage, UEngine::GetSmallFont(), FLinearColor::Green);
	Y += 12;

	// Second Pass: We're going to list the actual chunks in the cache.
	CurrentElement = MostRecentElement;
	int32 Index = 0;

	float ColorLerpAmount = 0.0f;
	const float ColorLerpStep = 0.04f;

	// More detailed info about individual chunks here:
	while (CurrentElement != nullptr)
	{
		// We use a CVar to clamp the max amount of chunks we display.
		if (Index > DebugMaxElementsDisplayCVar)
		{
			break;
		}

		int32 NumTotalChunks = -1;
		int32 NumTimesTouched = -1;
		double TimeToLoad = -1.0;
		float AveragePlaceInCache = -1.0f;
		bool bWasCacheMiss = false;
		bool bIsStaleChunk = false;

#if DEBUG_STREAM_CACHE
		NumTotalChunks = CurrentElement->DebugInfo.NumTotalChunks;
		NumTimesTouched = CurrentElement->DebugInfo.NumTimesTouched;
		TimeToLoad = CurrentElement->DebugInfo.TimeToLoad;
		AveragePlaceInCache = CurrentElement->DebugInfo.AverageLocationInCacheWhenNeeded;
		bWasCacheMiss = CurrentElement->DebugInfo.bWasCacheMiss;
#endif

#if WITH_EDITOR
		// TODO: Worry about whether the sound wave is alive here. In most editor cases this is ok because the soundwave will always be loaded, but this may not be the case in the future.
		bIsStaleChunk = (CurrentElement->Key.SoundWave == nullptr) || (CurrentElement->Key.SoundWave->CurrentChunkRevision.GetValue() != CurrentElement->Key.ChunkRevision);
#endif

		const bool bWasTrimmed = CurrentElement->ChunkData.Num() == 0;

		FString ElementInfo = *FString::Printf(TEXT("%4i. Size: %6.2f KB   Chunk: %d of %d   Request Count: %d    Average Index: %6.2f  Number of Handles Retaining Chunk: %d     Chunk Load Time: %6.4fms      Name: %s Notes: %s %s"),
			Index,
			CurrentElement->ChunkData.Num() / 1024.0f,
			CurrentElement->Key.ChunkIndex,
			NumTotalChunks,
			NumTimesTouched,
			AveragePlaceInCache,
			CurrentElement->NumConsumers.GetValue(),
			TimeToLoad,
			bWasTrimmed ? TEXT("TRIMMED CHUNK") : *CurrentElement->Key.SoundWaveName.ToString(),
			bWasCacheMiss ? TEXT("(Cache Miss!)") : TEXT(""),
			bIsStaleChunk ? TEXT("(Stale Chunk)") : TEXT("")
			);

		// Since there's a lot of info here,
		// Subtly fading the chunk info to gray seems to help as a visual indicator of how far down on the list things are.
		ColorLerpAmount = FMath::Min(ColorLerpAmount + ColorLerpStep, 1.0f);
		FLinearColor TextColor;
		if (bIsStaleChunk)
		{
			TextColor = FLinearColor::LerpUsingHSV(StaleChunkColor, FLinearColor::Gray, ColorLerpAmount);
		}
		else
		{
			TextColor = FLinearColor::LerpUsingHSV(LoadedChunkColor, FLinearColor::Gray, ColorLerpAmount);
		}

		// If there's a load in flight, paint this element yellow.
		if (CurrentElement->IsLoadInProgress())
		{
			TextColor = FLinearColor::Yellow;
		}
		else if (CurrentElement->IsInUse())
		{
			// We slowly fade our text color based on how many refererences there are to this chunk.
			static const float MaxNumHandles = 12.0f;

			ColorLerpAmount = FMath::Min(CurrentElement->NumConsumers.GetValue() / MaxNumHandles, 1.0f);
			TextColor = FLinearColor::LerpUsingHSV(RetainChunkColor, TotalMassRetainChunkColor, ColorLerpAmount);
		}
		else if (bWasTrimmed)
		{
			TextColor = TrimmedChunkColor;
		}

		Canvas->DrawShadowedString(X, Y, *ElementInfo, UEngine::GetSmallFont(), TextColor);
		Y += 12;

		CurrentElement = CurrentElement->LessRecentElement;
		Index++;
	}

	// The largest element of our debug panel is the initial memory details.
	int32 CacheMemoryTextOffsetX = 0;
	int32 CacheMemoryTextOffsetY = 0;
	UEngine::GetSmallFont()->GetStringHeightAndWidth(*CacheMemoryUsage, CacheMemoryTextOffsetX, CacheMemoryTextOffsetY);

	return TPair<int, int>(X + CacheTitleOffsetX + CacheMemoryTextOffsetX - InitialX, Y - InitialY);
}

FString FAudioChunkCache::DebugPrint()
{
	FScopeLock ScopeLock(const_cast<FCriticalSection*>(&CacheMutationCriticalSection));

	FString OutputString;

	FString NumElementsDetail = *FString::Printf(TEXT("Number of chunks loaded: %d of %d"), ChunksInUse, CachePool.Num());

	OutputString += NumElementsDetail + TEXT("\n");

	// First pass: We run through and get a snap shot of the amount of memory currently in use.
	FCacheElement* CurrentElement = MostRecentElement;
	uint32 NumBytesCounter = 0;

	uint32 NumBytesRetained = 0;

	while (CurrentElement != nullptr)
	{
		// Note: this is potentially a stale value if we're in the middle of FCacheElement::KickOffAsyncLoad.
		NumBytesCounter += CurrentElement->ChunkData.Num();

		if (CurrentElement->IsInUse())
		{
			NumBytesRetained += CurrentElement->ChunkData.Num();
		}

		CurrentElement = CurrentElement->LessRecentElement;
	}

	// Convert to megabytes and print the total size:
	const double NumMegabytesInUse = (double)NumBytesCounter / (1024 * 1024);
	const double NumMegabytesRetained = (double)NumBytesRetained / (1024 * 1024);
	
	const double MaxCacheSizeMB = ((double)MemoryLimitBytes) / (1024 * 1024);
	const double PercentageOfCacheRetained = NumMegabytesRetained / MaxCacheSizeMB;

	FString CacheMemoryHeader = *FString::Printf(TEXT("Retaining:\t, Loaded:\t, Max Potential Usage:\t, \n"));
	FString CacheMemoryUsage = *FString::Printf(TEXT("%.4f Megabytes (%.3f of total capacity)\t,  %.4f Megabytes (%lu bytes)\t, %.4f Megabytes\t, \n"), NumMegabytesRetained, PercentageOfCacheRetained, NumMegabytesInUse, MemoryCounterBytes.Load(), MaxCacheSizeMB);

	OutputString += CacheMemoryHeader + CacheMemoryUsage + TEXT("\n");

	// Second Pass: We're going to list the actual chunks in the cache.
	CurrentElement = MostRecentElement;
	int32 Index = 0;

	OutputString += TEXT("Index:\t, Size (KB):\t, Chunk:\t, Request Count:\t, Average Index:\t, Number of Handles Retaining Chunk:\t, Chunk Load Time:\t, Name: \t, Notes:\t, \n");

	// More detailed info about individual chunks here:
	while (CurrentElement != nullptr)
	{
		// We use a CVar to clamp the max amount of chunks we display.
		if (Index > DebugMaxElementsDisplayCVar)
		{
			break;
		}

		int32 NumTotalChunks = -1;
		int32 NumTimesTouched = -1;
		double TimeToLoad = -1.0;
		float AveragePlaceInCache = -1.0f;
		bool bWasCacheMiss = false;
		bool bIsStaleChunk = false;

#if DEBUG_STREAM_CACHE
		NumTotalChunks = CurrentElement->DebugInfo.NumTotalChunks;
		NumTimesTouched = CurrentElement->DebugInfo.NumTimesTouched;
		TimeToLoad = CurrentElement->DebugInfo.TimeToLoad;
		AveragePlaceInCache = CurrentElement->DebugInfo.AverageLocationInCacheWhenNeeded;
		bWasCacheMiss = CurrentElement->DebugInfo.bWasCacheMiss;
#endif

#if WITH_EDITOR
		// TODO: Worry about whether the sound wave is alive here. In most editor cases this is ok because the soundwave will always be loaded, but this may not be the case in the future.
		bIsStaleChunk = (CurrentElement->Key.SoundWave == nullptr) || (CurrentElement->Key.SoundWave->CurrentChunkRevision.GetValue() != CurrentElement->Key.ChunkRevision);
#endif

		const bool bWasTrimmed = CurrentElement->ChunkData.Num() == 0;

		FString ElementInfo = *FString::Printf(TEXT("%4i.\t, %6.2f KB\t, %d of %d\t, %d\t, %6.2f\t, %d\t,  %6.4fms\t, %s\t, %s %s %s"),
			Index,
			CurrentElement->ChunkData.Num() / 1024.0f,
			CurrentElement->Key.ChunkIndex,
			NumTotalChunks,
			NumTimesTouched,
			AveragePlaceInCache,
			CurrentElement->NumConsumers.GetValue(),
			TimeToLoad,
			bWasTrimmed ? TEXT("TRIMMED CHUNK") : *CurrentElement->Key.SoundWaveName.ToString(),
			bWasCacheMiss ? TEXT("(Cache Miss!)") : TEXT(""),
			bIsStaleChunk ? TEXT("(Stale Chunk)") : TEXT(""),
			CurrentElement->IsLoadInProgress() ? TEXT("(Loading In Progress)") : TEXT("")
		);

		
		OutputString += ElementInfo + TEXT("\n");

		CurrentElement = CurrentElement->LessRecentElement;
		Index++;
	}

	OutputString += TEXT("Cache Miss Log:\n");
	OutputString += FlushCacheMissLog();

	return OutputString;
}
