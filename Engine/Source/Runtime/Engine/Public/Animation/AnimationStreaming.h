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
#include "Templates/Atomic.h"
class UAnimStreamable;
struct FAnimationStreamingManager;

// 
struct FLoadedAnimationChunk
{
	TAtomic<FCompressedAnimSequence*> CompressedAnimData;

	class IAsyncReadRequest* IORequest;
	double RequestStart;

	uint32	Index;
	bool	bOwnsCompressedData;

	FLoadedAnimationChunk()
		: CompressedAnimData(nullptr)
		, IORequest(nullptr)
		, RequestStart(-1.0)
		, Index(0)
		, bOwnsCompressedData(false)
	{
	}

	~FLoadedAnimationChunk()
	{
		checkf(CompressedAnimData == nullptr, TEXT("Animation chunk compressed data ptr not null (%p), Index: %u"), CompressedAnimData.Load(), Index);
	}

	void CleanUpIORequest();
};

/**
 * Contains everything that will be needed by a Streamable Anim that's streaming in data
 */
struct FStreamingAnimationData final
{
	FStreamingAnimationData();
	~FStreamingAnimationData();

	// Frees streaming animation data resources, blocks pending async IO requests
	void FreeResources();

	/**
	 * Sets up the streaming wave data and loads the first chunk of audio for instant play
	 *
	 * @param Anim	The streamable animation we are managing
	 */
	bool Initialize(UAnimStreamable* InStreamableAnim, FAnimationStreamingManager* InAnimationStreamingManager);

	/**
	 * Updates the streaming status of the animation and performs finalization when appropriate. The function returns
	 * true while there are pending requests in flight and updating needs to continue.
	 *
	 * @return					true if there are requests in flight, false otherwise
	 */
	bool UpdateStreamingStatus();

	/**
	 * Tells the SoundWave which chunks are currently required so that it can start loading any needed
	 *
	 * @param InChunkIndices The Chunk Indices that are currently needed by all sources using this sound
	 * @param bShouldPrioritizeAsyncIORequest Whether request should have higher priority than usual
	 */
	//void UpdateChunkRequests(FWaveRequest& InWaveRequest);

		/**
	 * Checks whether the requested chunk indices differ from those loaded
	 *
	 * @param IndicesToLoad		List of chunk indices that should be loaded
	 * @param IndicesToFree		List of chunk indices that should be freed
	 * @return Whether any changes to loaded chunks are required
	 */
	bool HasPendingRequests(TArray<uint32>& IndicesToLoad, TArray<uint32>& IndicesToFree) const;

	/**
	 * Kicks off any pending requests
	 */
	void BeginPendingRequests(const TArray<uint32>& IndicesToLoad, const TArray<uint32>& IndicesToFree);

	/**
	* Blocks till all pending requests are fulfilled.
	*
	* @param TimeLimit		Optional time limit for processing, in seconds. Specifying 0 means infinite time limit.
	* @return				Return true if there are no requests left in flight, false if the time limit was reached before they were finished.
	*/
	bool BlockTillAllRequestsFinished(float TimeLimit = 0.0f);

#if WITH_EDITORONLY_DATA
	/**
	 * Finishes any Derived Data Cache requests that may be in progress
	 *
	 * @return Whether any of the requests failed.
	 */
	bool FinishDDCRequests();
#endif //WITH_EDITORONLY_DATA

private:
	// Don't allow copy construction as it could free shared memory
	FStreamingAnimationData(const FStreamingAnimationData& that);
	FStreamingAnimationData& operator=(FStreamingAnimationData const&);

	// Creates a new chunk, returns the chunk index
	FLoadedAnimationChunk& AddNewLoadedChunk(uint32 ChunkIndex, FCompressedAnimSequence* ExistingData);
	void FreeLoadedChunk(FLoadedAnimationChunk& LoadedChunk);
	void ResetRequestedChunks();

public:
	/** AnimStreamable this streaming data is for */
	UAnimStreamable* StreamableAnim;

	/* Contains pointers to Chunks of audio data that have been streamed in */
	TArray<FLoadedAnimationChunk> LoadedChunks;

	class IAsyncReadFileHandle* IORequestHandle;

	/** Indices of chunks that are currently loaded */
	TArray<uint32>	LoadedChunkIndices;

	TArray<uint32> RequestedChunks;

	/** Indices of chunks we want to have loaded */
	//FWaveRequest	CurrentRequest;

#if WITH_EDITORONLY_DATA
	/** Pending async derived data streaming tasks */
	//TIndirectArray<FAsyncStreamDerivedChunkTask> PendingAsyncStreamDerivedChunkTasks;
#endif // #if WITH_EDITORONLY_DATA

	/** Ptr to owning audio streaming manager. */
	FAnimationStreamingManager* AnimationStreamingManager;
};

/** Struct used to store results of an async file load. */
/*struct FASyncAnimationChunkLoadResult
{
	// Place to safely copy the ptr of a loaded audio chunk when load result is finished
	uint8* DataResults;

	// Actual storage of the loaded audio chunk, will be filled on audio thread.
	FStreamingAnimationData* StreamingAnimData;

	// Loaded audio chunk index
	int32 LoadedChunkIndex;

	FASyncAnimationChunkLoadResult()
		: DataResults(nullptr)
		, StreamingAnimData(nullptr)
		, LoadedChunkIndex(INDEX_NONE)
	{}
};*/


/**
* Streaming manager dealing with audio.
*/
struct FAnimationStreamingManager : public IAnimationStreamingManager
{
	/** Constructor, initializing all members */
	FAnimationStreamingManager();

	virtual ~FAnimationStreamingManager();

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
	virtual void AddStreamingAnim(UAnimStreamable* Anim) override;
	virtual bool RemoveStreamingAnim(UAnimStreamable* Anim) override;
	/*void AddDecoder(ICompressedAudioInfo* CompressedAudioInfo);
	void RemoveDecoder(ICompressedAudioInfo* CompressedAudioInfo);
	bool IsManagedStreamingSoundWave(const USoundWave* SoundWave) const;
	bool IsStreamingInProgress(const USoundWave* SoundWave);
	bool CanCreateSoundSource(const FWaveInstance* WaveInstance) const;
	void AddStreamingSoundSource(FSoundSource* SoundSource);
	void RemoveStreamingSoundSource(FSoundSource* SoundSource);
	bool IsManagedStreamingSoundSource(const FSoundSource* SoundSource) const;*/
	virtual const FCompressedAnimSequence* GetLoadedChunk(const UAnimStreamable* Anim, uint32 ChunkIndex) const override;
	// End IAudioStreamingManager interface

	/** Called when an async callback is made on an async loading audio chunk request. */
	void OnAsyncFileCallback(FStreamingAnimationData* StreamingAnimData, int32 ChunkIndex, int64 ReadSize, IAsyncReadRequest* ReadRequest);

protected:

	/** Sound Waves being managed. */
	TMap<UAnimStreamable*, FStreamingAnimationData*> StreamingAnimations;

	/** Critical section to protect usage of shared gamethread/workerthread members */
	mutable FCriticalSection CriticalSection;
};
