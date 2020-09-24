// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGeometryCacheMeshData;

/** Interface to stream GeometryCacheMeshData asynchronously from any source through the GeometryCacheStreamer */
class IGeometryCacheStream
{
public:
	virtual ~IGeometryCacheStream() = default;

	/** Prefetch NumFrames starting from the given StartFrameIndex. If no NumFrames given, prefetch the whole stream */
	virtual void Prefetch(int32 StartFrameIndex, int32 NumFrames = 0) = 0;

	/** Return list of frame indices needed to be loaded */
	virtual const TArray<int32>& GetFramesNeeded() = 0;
	
	/** Request a read of the given FrameIndex. Return true if the request could be handled */
	virtual bool RequestFrameData(int32 FrameIndex) = 0;

	/** Update the status of the read requests currently in progress. Return the frame indices that were completed */
	virtual void UpdateRequestStatus(TArray<int32>& OutFramesCompleted) = 0;

	/* Get the MeshData at given FrameIndex without waiting for data to be ready
	 * Return true if MeshData could be retrieved
	 */
	virtual bool GetFrameData(int32 FrameIndex, FGeometryCacheMeshData& OutMeshData) = 0;

	/** Cancel the scheduled read requests. Return the number of requests that were canceled */
	virtual int32 CancelRequests() = 0;
};
