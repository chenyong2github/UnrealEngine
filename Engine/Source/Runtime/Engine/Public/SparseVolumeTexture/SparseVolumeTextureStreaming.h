// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ContentStreaming.h"
#include "Containers/Map.h"

class UStreamableSparseVolumeTexture;
class FSparseVolumeTextureSceneProxy;

struct FLoadedSparseVolumeTextureChunk
{
	std::atomic<FSparseVolumeTextureSceneProxy*> Proxy = nullptr;
	class IBulkDataIORequest* IORequest = nullptr;
	double RequestStart = -1.0;
	int32 ChunkIndex = INDEX_NONE;
	bool bOwnsProxy = false;

	~FLoadedSparseVolumeTextureChunk();
	void CleanUpIORequest();
};

struct FStreamingSparseVolumeTextureData
{
	UStreamableSparseVolumeTexture* SparseVolumeTexture;
	TArray<FLoadedSparseVolumeTextureChunk> LoadedChunks;
	TArray<int32> LoadedChunkIndices;
	TArray<int32> RequestedChunkIndices;
	TArray<int32> LoadFailedChunkIndices;
	mutable FCriticalSection LoadedChunksCriticalSection;

	explicit FStreamingSparseVolumeTextureData();
	~FStreamingSparseVolumeTextureData();

	// Don't allow copy construction as it could free shared memory
	FStreamingSparseVolumeTextureData(const FStreamingSparseVolumeTextureData&) = delete;
	FStreamingSparseVolumeTextureData& operator=(FStreamingSparseVolumeTextureData const&) = delete;

	bool Initialize(UStreamableSparseVolumeTexture* InSparseVolumeTexture, class FSparseVolumeTextureStreamingManager* InStreamingManager);
	void FreeResources();
	bool UpdateStreamingStatus();
	bool HasPendingRequests(TArray<int32>& IndicesToLoad, TArray<int32>& IndicesToFree) const;
	void BeginPendingRequests(const TArray<int32>& IndicesToLoad, const TArray<int32>& IndicesToFree);
	bool BlockTillAllRequestsFinished(float TimeLimit = 0.0f);
	void GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const;

private:
	class FSparseVolumeTextureStreamingManager* StreamingManager;

	FLoadedSparseVolumeTextureChunk& AddNewLoadedChunk(int32 ChunkIndex, FSparseVolumeTextureSceneProxy* ExistingProxy);
	void FreeLoadedChunk(FLoadedSparseVolumeTextureChunk& LoadedChunk);
	void ResetRequestedChunks();
};

class FSparseVolumeTextureStreamingManager : public ISparseVolumeTextureStreamingManager
{
public:
	explicit FSparseVolumeTextureStreamingManager();
	virtual ~FSparseVolumeTextureStreamingManager();

	//~ Begin IStreamingManager Interface
	virtual void UpdateResourceStreaming(float DeltaTime, bool bProcessEverything = false) override;
	virtual int32 BlockTillAllRequestsFinished(float TimeLimit = 0.0f, bool bLogResults = false) override;
	virtual void CancelForcedResources() override { /*empty*/ };
	virtual void NotifyLevelChange() override { /*empty*/ };
	virtual void SetDisregardWorldResourcesForFrames(int32 NumFrames) override { /*empty*/ };
	virtual void AddLevel(class ULevel* Level) override { /*empty*/ };
	virtual void RemoveLevel(class ULevel* Level) override { /*empty*/ };
	virtual void NotifyLevelOffset(class ULevel* Level, const FVector& Offset) override { /*empty*/ };
	//~ End IStreamingManager Interface

	//~ Begin ISparseVolumeTextureStreamingManager Interface
	virtual void AddSparseVolumeTexture(UStreamableSparseVolumeTexture* SparseVolumeTexture) override;
	virtual bool RemoveSparseVolumeTexture(UStreamableSparseVolumeTexture* SparseVolumeTexture) override;
	virtual void GetMemorySizeForSparseVolumeTexture(const UStreamableSparseVolumeTexture* SparseVolumeTexture, SIZE_T* SizeCPU, SIZE_T* SizeGPU) const override;
	virtual const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy(const UStreamableSparseVolumeTexture* SparseVolumeTexture, int32 FrameIndex, int32 MipLevel, bool bTrackAsRequested) override;
	//~ End ISparseVolumeTextureStreamingManager Interface

	void OnAsyncFileCallback(FStreamingSparseVolumeTextureData* StreamingSVTData, int32 ChunkIndex, int64 ReadSize, IBulkDataIORequest* ReadRequest, bool bWasCancelled);

private:
	TMap<UStreamableSparseVolumeTexture*, FStreamingSparseVolumeTextureData*> StreamingSparseVolumeTextures;
	mutable FCriticalSection CriticalSection;
};
