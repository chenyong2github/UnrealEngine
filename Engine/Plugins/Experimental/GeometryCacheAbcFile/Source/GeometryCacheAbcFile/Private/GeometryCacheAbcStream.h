// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IGeometryCacheStream.h"

struct FGeometryCacheAbcStreamReadRequest;
class UGeometryCacheTrackAbcFile;

class FGeometryCacheAbcStream : public IGeometryCacheStream
{
public:
	FGeometryCacheAbcStream(UGeometryCacheTrackAbcFile* InAbcTrack);
	virtual ~FGeometryCacheAbcStream();

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

	UGeometryCacheTrackAbcFile* AbcTrack;

	TArray<int32> ReadIndices;
	TArray<FGeometryCacheAbcStreamReadRequest*> ReadRequestsPool;

	TArray<int32> FramesNeeded;
	TArray<FGeometryCacheAbcStreamReadRequest*> FramesRequested;

	typedef TMap<int32, FGeometryCacheMeshData*> FFrameIndexToMeshData;
	FFrameIndexToMeshData FramesAvailable;

	FCriticalSection CriticalSection;

	TAtomic<bool> bCancellationRequested;
};