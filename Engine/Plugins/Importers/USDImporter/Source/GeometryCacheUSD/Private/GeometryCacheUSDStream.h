// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGeometryCacheStream.h"
#include "GeometryCacheTrackUSD.h"

struct FGeometryCacheUsdStreamReadRequest;

class FGeometryCacheUsdStream : public IGeometryCacheStream
{
public:
	FGeometryCacheUsdStream( TWeakObjectPtr<UGeometryCacheTrackUsd> InUsdTrack, FReadUsdMeshFunction InReadFunc );
	virtual ~FGeometryCacheUsdStream();

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

	void LoadFrameData(int32 FrameIndex);

	TWeakObjectPtr<UGeometryCacheTrackUsd> UsdTrack;
	FReadUsdMeshFunction ReadFunc;

	TArray<int32> ReadIndices;
	TArray<FGeometryCacheUsdStreamReadRequest*> ReadRequestsPool;

	TArray<int32> FramesNeeded;
	TArray<FGeometryCacheUsdStreamReadRequest*> FramesRequested;

	typedef TMap<int32, FGeometryCacheMeshData*> FFrameIndexToMeshData;
	FFrameIndexToMeshData FramesAvailable;

	FCriticalSection CriticalSection;

	TAtomic<bool> bCancellationRequested;
};

