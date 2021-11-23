// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGeometryCacheStream.h"
#include <atomic>

struct FGeometryCacheAbcStreamReadRequest;
class UGeometryCacheTrackAbcFile;

class FGeometryCacheAbcStream : public IGeometryCacheStream
{
public:
	FGeometryCacheAbcStream(UGeometryCacheTrackAbcFile* InAbcTrack);
	virtual ~FGeometryCacheAbcStream();

	//~ Begin IGeometryCacheStream Interface
	virtual void Prefetch(int32 StartFrameIndex, int32 NumFrames = 0) override;
	virtual uint32 GetNumFramesNeeded() override;
	virtual bool RequestFrameData() override;
	virtual void UpdateRequestStatus(TArray<int32>& OutFramesCompleted) override;
	virtual bool GetFrameData(int32 FrameIndex, FGeometryCacheMeshData& OutMeshData) override;
	virtual int32 CancelRequests() override;
	virtual const FGeometryCacheStreamStats& GetStreamStats() const override;
	virtual void SetLimits(float MaxMemoryAllowed, float MaxCachedDuration) override;
	//~ End IGeometryCacheStream Interface

private:

	void GetMeshData(int32 FrameIndex, int32 ConcurrencyIndex, FGeometryCacheMeshData& OutMeshData);
	void LoadFrameData(int32 FrameIndex);
	void UpdateFramesNeeded(int32 StartIndex, int32 NumFrames);
	void IncrementMemoryStat(const FGeometryCacheMeshData& MeshData);
	void DecrementMemoryStat(const FGeometryCacheMeshData& MeshData);

	UGeometryCacheTrackAbcFile* AbcTrack;

	TArray<int32> ReadIndices;
	TArray<FGeometryCacheAbcStreamReadRequest*> ReadRequestsPool;

	TArray<int32> FramesNeeded;
	TArray<int32> FramesToBeCached;
	TArray<FGeometryCacheAbcStreamReadRequest*> FramesRequested;

	typedef TMap<int32, FGeometryCacheMeshData*> FFrameIndexToMeshData;
	FFrameIndexToMeshData FramesAvailable;
	FRWLock FramesAvailableLock;

	std::atomic<bool> bCancellationRequested;

	mutable FGeometryCacheStreamStats Stats;
	FString Hash;
	float SecondsPerFrame;
	std::atomic<int32> LastAccessedFrameIndex;
	int32 MaxCachedFrames;
	float MaxCachedDuration;
	float MaxMemAllowed;
	float MemoryUsed;
	std::atomic<bool> bCacheNeedsUpdate;
};