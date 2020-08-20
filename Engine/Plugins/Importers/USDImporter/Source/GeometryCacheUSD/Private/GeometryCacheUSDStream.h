// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGeometryCacheStream.h"
#include "GeometryCacheTrackUSD.h"

struct FGeometryCacheUsdStreamReadRequest;

class FGeometryCacheUsdStream : public IGeometryCacheStream
{
public:
	FGeometryCacheUsdStream(UGeometryCacheTrackUsd* InUsdTrack, FReadUsdMeshFunction InReadFunc, const FString& InPrimPath);
	virtual ~FGeometryCacheUsdStream();

	//~ Begin IGeometryCacheStream Interface
	virtual void Prefetch(int32 StartFrameIndex, int32 NumFrames = 0) override;
	virtual const TArray<int32>& GetFramesNeeded() override;
	virtual bool RequestFrameData(int32 FrameIndex) override;
	virtual void UpdateRequestStatus(TArray<int32>& OutFramesCompleted) override;
	virtual bool GetFrameData(int32 FrameIndex, FGeometryCacheMeshData& OutMeshData) override;
	virtual int32 CancelRequests() override;
	//~ End IGeometryCacheStream Interface

private:

	void LoadFrameData(int32 FrameIndex);

	UGeometryCacheTrackUsd* UsdTrack;
	FReadUsdMeshFunction ReadFunc;

	TArray<int32> ReadIndices;
	TArray<FGeometryCacheUsdStreamReadRequest*> ReadRequestsPool;

	TArray<int32> FramesNeeded;
	TArray<FGeometryCacheUsdStreamReadRequest*> FramesRequested;

	typedef TMap<int32, FGeometryCacheMeshData*> FFrameIndexToMeshData;
	FFrameIndexToMeshData FramesAvailable;

	FCriticalSection CriticalSection;

	TAtomic<bool> bCancellationRequested;

	FString PrimPath;
};