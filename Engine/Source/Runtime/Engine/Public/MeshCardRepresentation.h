// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshCardRepresentation.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/LockFreeList.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "Engine/EngineTypes.h"
#include "UObject/GCObject.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "Templates/UniquePtr.h"
#include "DerivedMeshDataTaskUtils.h"

template <class T> class TLockFreePointerListLIFO;

class FLumenCubeMapBuildData
{
public:
	// -X, +X, -Y, +Y, -Z, +Z
	int32 FaceIndices[6];

	bool operator==(const FLumenCubeMapBuildData& B) const
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FaceIndices); ++Index)
		{
			if (FaceIndices[Index] != B.FaceIndices[Index])
			{
				return false;
			}
		}

		return true;
	}

	friend FArchive& operator<<(FArchive& Ar, FLumenCubeMapBuildData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FaceIndices); ++Index)
		{
			Ar << Data.FaceIndices[Index];
		}

		return Ar;
	}
};

class FLumenCubeMapFaceBuildData
{
public:
	FVector Center;
	FVector Extent;

	// -X, +X, -Y, +Y, -Z, +Z
	int32 Orientation;

	friend FArchive& operator<<(FArchive& Ar, FLumenCubeMapFaceBuildData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.Center;
		Ar << Data.Extent;
		Ar << Data.Orientation;
		return Ar;
	}
};

class FCubeMapTreeBuildData
{
public:
	FBox LUTVolumeBounds;
	FIntVector LUTVolumeResolution;

	TArray<FLumenCubeMapBuildData> CubeMapBuiltData;
	TArray<FLumenCubeMapFaceBuildData> FaceBuiltData;
	TArray<uint8> LookupVolumeData;

	friend FArchive& operator<<(FArchive& Ar, FCubeMapTreeBuildData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.LUTVolumeBounds;
		Ar << Data.LUTVolumeResolution;
		Ar << Data.CubeMapBuiltData;
		Ar << Data.FaceBuiltData;
		Ar << Data.LookupVolumeData;
		return Ar;
	}
};

// Unique id per card representation data instance.
class FCardRepresentationDataId
{
public:
	uint32 Value = 0;

	bool IsValid() const
	{
		return Value != 0;
	}

	bool operator==(FCardRepresentationDataId B) const
	{
		return Value == B.Value;
	}

	friend uint32 GetTypeHash(FCardRepresentationDataId DataId)
	{
		return GetTypeHash(DataId.Value);
	}
};

/** Card representation payload and output of the mesh build process. */
class FCardRepresentationData : public FDeferredCleanupInterface
{
public:

	FCubeMapTreeBuildData CubeMapTreeBuildData;

	FCardRepresentationDataId CardRepresentationDataId;

	FCardRepresentationData() 
	{
		// 0 means invalid id.
		static uint32 NextCardRepresentationId = 0;
		++NextCardRepresentationId;
		CardRepresentationDataId.Value = NextCardRepresentationId;
	}

	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));
	}

	SIZE_T GetResourceSizeBytes() const
	{
		FResourceSizeEx ResSize;
		GetResourceSizeEx(ResSize);
		return ResSize.GetTotalMemoryBytes();
	}

#if WITH_EDITORONLY_DATA

	void CacheDerivedData(const FString& InDDCKey, const ITargetPlatform* TargetPlatform, UStaticMesh* Mesh, UStaticMesh* GenerateSource, FSourceMeshDataForDerivedDataTask* OptionalSourceMeshData);

#endif

	friend FArchive& operator<<(FArchive& Ar,FCardRepresentationData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.CubeMapTreeBuildData;
		return Ar;
	}
};

class FAsyncCardRepresentationTask
{
public:
	FAsyncCardRepresentationTask();

	bool bSuccess = false;

	FSourceMeshDataForDerivedDataTask SourceMeshData;
	UStaticMesh* StaticMesh;
	UStaticMesh* GenerateSource;
	FString DDCKey;
	FCardRepresentationData* GeneratedCardRepresentation;
};

/** Class that manages asynchronous building of mesh distance fields. */
class FCardRepresentationAsyncQueue : public FGCObject
{
public:

	ENGINE_API FCardRepresentationAsyncQueue();

	ENGINE_API virtual ~FCardRepresentationAsyncQueue();

	/** Adds a new build task. */
	ENGINE_API void AddTask(FAsyncCardRepresentationTask* Task);

	/** Blocks the main thread until the async build of the specified mesh is complete. */
	ENGINE_API void BlockUntilBuildComplete(UStaticMesh* StaticMesh, bool bWarnIfBlocked);

	/** Blocks the main thread until all async builds complete. */
	ENGINE_API void BlockUntilAllBuildsComplete();

	/** Called once per frame, fetches completed tasks and applies them to the scene. */
	ENGINE_API void ProcessAsyncTasks();

	/** Exposes UObject references used by the async build. */
	ENGINE_API void AddReferencedObjects(FReferenceCollector& Collector);

	/** Blocks until it is safe to shut down (worker threads are idle). */
	ENGINE_API void Shutdown();

	int32 GetNumOutstandingTasks() const
	{
		FScopeLock Lock(&CriticalSection);
		return ReferencedTasks.Num();
	}

private:

	/** Builds a single task with the given threadpool.  Called from the worker thread. */
	void Build(FAsyncCardRepresentationTask* Task, class FQueuedThreadPool& ThreadPool);

	/** Thread that will build any tasks in TaskQueue and exit when there are no more. */
	class TUniquePtr<class FBuildCardRepresentationThreadRunnable> ThreadRunnable;

	/** Game-thread managed list of tasks in the async system. */
	TArray<FAsyncCardRepresentationTask*> ReferencedTasks;

	/** Tasks that have not yet started processing yet. */
	TLockFreePointerListLIFO<FAsyncCardRepresentationTask> TaskQueue;

	/** Tasks that have completed processing. */
	TLockFreePointerListLIFO<FAsyncCardRepresentationTask> CompletedTasks;

	class IMeshUtilities* MeshUtilities;

	mutable FCriticalSection CriticalSection;

	friend class FBuildCardRepresentationThreadRunnable;
};

/** Global build queue. */
extern ENGINE_API FCardRepresentationAsyncQueue* GCardRepresentationAsyncQueue;

extern ENGINE_API FString BuildCardRepresentationDerivedDataKey(const FString& InMeshKey);

extern ENGINE_API void BeginCacheMeshCardRepresentation(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMeshAsset, class FStaticMeshRenderData& RenderData, const FString& DistanceFieldKey, FSourceMeshDataForDerivedDataTask* OptionalSourceMeshData);