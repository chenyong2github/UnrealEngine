// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
AudioStreaming.h: Definitions of classes used for audio streaming.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/IndirectArray.h"
#include "Containers/Queue.h"
#include "Stats/Stats.h"
#include "ContentStreaming.h"
#include "Async/AsyncWork.h"
#include "Async/AsyncFileHandle.h"
#include "HAL/ThreadSafeBool.h"
#include "AudioStreaming.h"


#define DEBUG_STREAM_CACHE !UE_BUILD_SHIPPING

// Basic fixed-size LRU cache for retaining chunks of compressed audio data.
class FAudioChunkCache
{
public:
	struct FChunkKey
	{
		USoundWave* SoundWave = nullptr;
		FName SoundWaveName = FName();
		uint32 ChunkIndex = INDEX_NONE;

		inline bool operator==(const FChunkKey& Other) const
		{
			return (SoundWaveName == Other.SoundWaveName) && (ChunkIndex == Other.ChunkIndex);
		}
	};

	FAudioChunkCache(uint32 InMaxChunkSize, uint32 NumChunks, uint64 InMemoryLimitInBytes);
	
	~FAudioChunkCache();

	// Places chunk in cache, or puts this chunk back at the top of the cache if it's already loaded. Returns false on failure.
	bool AddOrTouchChunk(const FChunkKey& InKey, TFunction<void(EAudioChunkLoadResult) > OnLoadCompleted);

	// Returns the chunk asked for, or an empty TArrayView if that chunk is not loaded.
	TArrayView<uint8> GetChunk(const FChunkKey& InKey, bool bBlockForLoadCompletion);

	// add an additional reference for a chunk.
	void AddNewReferenceToChunk(const FChunkKey& InKey);
	void RemoveReferenceToChunk(const FChunkKey& InKey);

	// Evict all sounds from the cache.
	void ClearCache();

	// This function will reclaim memory by freeing as many chunks as needed to free BytesToFree.
	// returns the amount of bytes we were actually able to free.
	// It's important to note that this will block any chunk requests.
	uint64 TrimMemory(uint64 BytesToFree);

	// This function will continue to lock until any async file loads are finished.
	void BlockForAllPendingLoads() const;

	// This function will cancel any in-flight loads and wait for their completion.
	void CancelAllPendingLoads();

	// Reports the size of this cache's memory pool, in bytes.
	uint64 ReportCacheSize();

	// Debug tools:
	// Call this to start enqueing reports on any cache misses to a queue.
	// This queue will continue to grow until FlushCacheMissLog is called.
	void BeginLoggingCacheMisses();

	// This will stop enqueueing reports of cache misses.
	void StopLoggingCacheMisses();

	// When called, flushes the entire queue of cache misses that has accumulated
	// And prints them to a formatted 
	FString FlushCacheMissLog();

	// Static helper function to make sure a chunk is withing the bounds of a USoundWave.
	static bool IsKeyValid(const FChunkKey& InKey);

	const int32 MaxChunkSize;

	// This is for debugging purposes only. Prints the elements in the cache from most recently used to least.
	// Returns the dimensions of this debug log so that multiple caches can be tiled across the screen.
	TPair<int, int> DebugDisplay(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation) const;

private:

#if DEBUG_STREAM_CACHE
	// This struct lets us breadcrumb debug information.
	struct FCacheElementDebugInfo
	{
		// The total number of chunks in the sound wave.
		int32 NumTotalChunks;

		// Number of times this chunk was requested during its time in the cache.
		int32 NumTimesTouched;

		double TimeLoadStarted;
		// Amount of time spent loading the audio file.
		double TimeToLoad;

		// This allows every call to AddOrTouchChunk to report how far down the cache
		// it was when it was needed, or INDEX_NONE if it wasn't in the cache when it was needed.
		TArray<int32> PreviousLocationsBeforeBeingTouched;
		FCriticalSection PreviousLocationsLock;

		FCacheElementDebugInfo()
			: NumTotalChunks(0)
			, NumTimesTouched(0)
			, TimeLoadStarted(0.0)
			, TimeToLoad(0.0)
		{
		}

		void Reset()
		{
			NumTotalChunks = 0;
			NumTimesTouched = 0;
			TimeLoadStarted = 0;

			FScopeLock ScopeLock(&PreviousLocationsLock);
			PreviousLocationsBeforeBeingTouched.Empty();
		}
	};
#endif

	// Struct containing a single element in our LRU Cache.  
	struct FCacheElement
	{
		FChunkKey Key;
		TArray<uint8> ChunkData;
		uint32 ChunkDataSize;
		FCacheElement* MoreRecentElement;
		FCacheElement* LessRecentElement;
		uint32 CacheIndex;

		FThreadSafeBool bIsLoaded;
		
		// How many disparate consumers have called GetLoadedChunk.
		FThreadSafeCounter NumConsumers;

#if WITH_EDITORONLY_DATA
		TUniquePtr<FAsyncStreamDerivedChunkTask> DDCTask;
#endif

		TUniquePtr<IAsyncReadFileHandle> FileHandle;
		// Handle to our async read request operation.
		TUniquePtr<IAsyncReadRequest> ReadRequest;

#if DEBUG_STREAM_CACHE
		FCacheElementDebugInfo DebugInfo;
#endif

		FCacheElement(uint32 MaxChunkSize, uint32 InCacheIndex)
			: ChunkDataSize(0)
			, MoreRecentElement(nullptr)
			, LessRecentElement(nullptr)
			, CacheIndex(InCacheIndex)
			, bIsLoaded(false)
		{
		}

		void WaitForAsyncLoadCompletion(bool bCancel)
		{
#if WITH_EDITORONLY_DATA
			if (DDCTask.IsValid() && !DDCTask->IsDone())
			{
				DDCTask->Cancel();
				DDCTask->EnsureCompletion(false);
			}
#endif

			if (ReadRequest.IsValid())
			{
				if (bCancel)
				{
					ReadRequest->Cancel();
				}
				
				ReadRequest->WaitCompletion();
				ReadRequest.Reset();
			}
		}

		bool IsLoadInProgress() const
		{
			return !bIsLoaded;
		}

		bool IsInUse() const
		{
			return NumConsumers.GetValue() > 0;
		}

		bool CanEvictChunk() const
		{
			return !IsInUse() && !IsLoadInProgress();
		}

		~FCacheElement()
		{
			WaitForAsyncLoadCompletion(true);
			checkf(NumConsumers.GetValue() == 0, TEXT("Tried to destroy streaming cache while the cached data was in use!"));
		}
	};

	// Our actual memory pool.
	TArray<FCacheElement> CachePool;
	FCacheElement* MostRecentElement;
	FCacheElement* LeastRecentElement;

	// This is incremented on every call of InsertChunk until we hit CachePool.Num() or MemoryCounterBytes hits MemoryLimitBytes.
	int32 ChunksInUse;

	// This counter is used to start evicting chunks before we hit CachePool.Num().
	TAtomic<uint64> MemoryCounterBytes;
	uint64 MemoryLimitBytes;

	// Number of async load operations we have currently in flight.
	FThreadSafeCounter NumberOfLoadsInFlight;

	// Critical section: only used when we are modifying element positions in the cache. This only happens in TouchElement, EvictLeastRecentChunk, and TrimMemory.
	// Individual cache elements should be thread safe to access.
	FCriticalSection CacheMutationCriticalSection;
	 
	// This struct is used for logging cache misses.
	struct FCacheMissInfo
	{
		FName SoundWaveName;
		uint32 ChunkIndex;
		uint32 TotalChunksInWave;
		bool bBlockedForLoad;
	};

	// This queue is pushed to anytime GetChunk fails to get the chunk. and bLogCacheMisses is true.
	TQueue<FCacheMissInfo> CacheMissQueue;

	// This is set to true when BeginLoggingCacheMisses is called. 
	bool bLogCacheMisses;

	// Returns cached element if it exists in our cache, nullptr otherwise.
	FCacheElement* FindElementForKey(const FChunkKey& InKey);

	// Puts this element at the front of the linked list.
	void TouchElement(FCacheElement* InElement);

	// Inserts a new element into the cache, potentially evicting the oldest element in the cache.
	FCacheElement* InsertChunk(const FChunkKey& InKey);

	// This is called once we have more than one chunk in our cache:
	void SetUpLeastRecentChunk();

	// This is called in InsertChunk. it determines whether we should add a new chunk at the tail of the linked list or
	// evict the least recent chunk.
	bool ShouldAddNewChunk() const;

	// Returns the least recent chunk and fixes up the linked list accordingly.
	FCacheElement* EvictLeastRecentChunk();

	void KickOffAsyncLoad(FCacheElement* CacheElement, const FChunkKey& InKey, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted);
	EAsyncIOPriorityAndFlags GetAsyncPriorityForChunk(const FChunkKey& InKey);
};

// This is used to sort the cache array from smallest chunk size to biggest.
inline bool operator< (const FAudioChunkCache& Element1, const FAudioChunkCache& Element2)
{
	return Element1.MaxChunkSize < Element2.MaxChunkSize;
}

struct FCachedAudioStreamingManagerParams
{
	struct FCacheDimensions
	{
		// The max size, in bytes, of a single chunk of compressed audio.
		// During cook, compressed audio assets will be chunked based on this amount.
		int32 MaxChunkSize;

		// The maximum number of elements stored in a single cache before it is evicted.
		// At runtime, this will be clamped to ensure that it is greater than the amount of
		// sources that can be playing simultaneously.
		int32 NumElements;

		// The maximum number of elements stored in a single cache before it is evicted.
		// At runtime, this will be clamped to ensure that it is greater than the amount of
		// sources that can be playing simultaneously.
		uint64 MaxMemoryInBytes;
	};

	// Most use cases will only use a single cache, but applications can optionally
	// use multiple LRU caches to reduce churn for specific types of sounds.
	// For example, an application can have
	// a cache for short sounds with room for many elements, and a separate cache
	// for longer sounds with fewer elements.
	TArray<FCacheDimensions> Caches;
};

/**
 * This implementation of the audio streaming manager uses an internal LRU cache (or in more advanced applications, a bank of parallel LRU caches)
 */
struct FCachedAudioStreamingManager : public IAudioStreamingManager
{
public:
	/** Constructor, initializing all members */
	FCachedAudioStreamingManager(const FCachedAudioStreamingManagerParams& InitParams);

	virtual ~FCachedAudioStreamingManager();

	// IStreamingManager interface
	virtual void UpdateResourceStreaming( float DeltaTime, bool bProcessEverything=false ) override;
	virtual int32 BlockTillAllRequestsFinished( float TimeLimit = 0.0f, bool bLogResults = false ) override;
	virtual void CancelForcedResources() override;
	virtual void NotifyLevelChange() override;
	virtual void SetDisregardWorldResourcesForFrames( int32 NumFrames ) override;
	virtual void AddLevel( class ULevel* Level ) override;
	virtual void RemoveLevel( class ULevel* Level ) override;
	virtual void NotifyLevelOffset( class ULevel* Level, const FVector& Offset ) override;
	// End IStreamingManager interface

	// IAudioStreamingManager interface
	virtual void AddStreamingSoundWave(USoundWave* SoundWave) override;
	virtual void RemoveStreamingSoundWave(USoundWave* SoundWave) override;
	virtual void AddDecoder(ICompressedAudioInfo* CompressedAudioInfo) override;
	virtual void RemoveDecoder(ICompressedAudioInfo* CompressedAudioInfo) override;
	virtual bool IsManagedStreamingSoundWave(const USoundWave* SoundWave) const override;
	virtual bool IsStreamingInProgress(const USoundWave* SoundWave) override;
	virtual bool CanCreateSoundSource(const FWaveInstance* WaveInstance) const override;
	virtual void AddStreamingSoundSource(FSoundSource* SoundSource) override;
	virtual void RemoveStreamingSoundSource(FSoundSource* SoundSource) override;
	virtual bool IsManagedStreamingSoundSource(const FSoundSource* SoundSource) const override;
	virtual bool RequestChunk(USoundWave* SoundWave, uint32 ChunkIndex, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted) override;
	virtual FAudioChunkHandle GetLoadedChunk(const USoundWave* SoundWave, uint32 ChunkIndex, bool bBlockForLoad = false) const override;
	virtual uint64 TrimMemory(uint64 NumBytesToFree) override;
	virtual int32 RenderStatAudioStreaming(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation) override;
	// End IAudioStreamingManager interface

protected:

	// These are used to reference count consumers of audio chunks.
	virtual void AddReferenceToChunk(const FAudioChunkHandle& InHandle) override;
	virtual void RemoveReferenceToChunk(const FAudioChunkHandle& InHandle) override;

	/**
	 * Returns which cache this sound wave should be in,
	 * based on the size of this sound wave's chunk,
	 * or nullptr if MemoryLoadOnDemand is disabled.
	 */
	FAudioChunkCache* GetCacheForWave(const USoundWave* InSoundWave) const;

	/**
	 * Returns the next chunk to kick off a load for, or INDEX_NONE if there is only one chunk to cache.
	 */
	int32 GetNextChunkIndex(const USoundWave* InSoundWave, uint32 CurrentChunkIndex) const;

	/** Audio chunk caches. These are set up on initialization. */
	TArray<FAudioChunkCache> CacheArray;

	

};
