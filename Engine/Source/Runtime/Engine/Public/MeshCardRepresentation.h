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
#include "AssetCompilingManager.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "Templates/UniquePtr.h"
#include "DerivedMeshDataTaskUtils.h"
#include "Async/AsyncWork.h"

template <class T> class TLockFreePointerListLIFO;
class FSignedDistanceFieldBuildMaterialData;

class FLumenCardBuildData
{
public:
	FVector Center;
	FVector Extent;

	// -X, +X, -Y, +Y, -Z, +Z
	int32 Orientation;
	int32 LODLevel;

	static FVector TransformFaceExtent(FVector Extent, int32 Orientation)
	{
		if (Orientation / 2 == 2)
		{
			return FVector(Extent.Y, Extent.X, Extent.Z);
		}
		else if (Orientation / 2 == 1)
		{
			return FVector(Extent.Z, Extent.X, Extent.Y);
		}
		else
		{
			return FVector(Extent.Y, Extent.Z, Extent.X);
		}
	}

	friend FArchive& operator<<(FArchive& Ar, FLumenCardBuildData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.Center;
		Ar << Data.Extent;
		Ar << Data.Orientation;
		Ar << Data.LODLevel;
		return Ar;
	}
};

class FLumenCardBuildDebugPoint
{
public:
	FVector Origin;
	int32 Orientation;
	bool bValid;
};

class FLumenCardBuildDebugLine
{
public:
	FVector Origin;
	FVector EndPoint;
	int32 Orientation;
};

class FMeshCardsBuildData
{
public:
	FBox Bounds;
	int32 MaxLODLevel;
	TArray<FLumenCardBuildData> CardBuildData;

	// Temporary debug visualization data
	TArray<FLumenCardBuildDebugPoint> DebugPoints;
	TArray<FLumenCardBuildDebugLine> DebugLines;

	friend FArchive& operator<<(FArchive& Ar, FMeshCardsBuildData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.Bounds;
		Ar << Data.MaxLODLevel;
		Ar << Data.CardBuildData;
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

	FMeshCardsBuildData MeshCardsBuildData;

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

	void CacheDerivedData(const FString& InDDCKey, const ITargetPlatform* TargetPlatform, UStaticMesh* Mesh, UStaticMesh* GenerateSource, bool bGenerateDistanceFieldAsIfTwoSided, FSourceMeshDataForDerivedDataTask* OptionalSourceMeshData);

#endif

	friend FArchive& operator<<(FArchive& Ar,FCardRepresentationData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.MeshCardsBuildData;
		return Ar;
	}
};


class FAsyncCardRepresentationTask;
class FAsyncCardRepresentationTaskWorker : public FNonAbandonableTask
{
public:
	FAsyncCardRepresentationTaskWorker(FAsyncCardRepresentationTask& InTask)
		: Task(InTask)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncCardRepresentationTaskWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

	void DoWork();

private:
	FAsyncCardRepresentationTask& Task;
};

class FAsyncCardRepresentationTask
{
public:
	bool bSuccess = false;

#if WITH_EDITOR
	TArray<FSignedDistanceFieldBuildMaterialData> MaterialBlendModes;
#endif

	FSourceMeshDataForDerivedDataTask SourceMeshData;
	bool bGenerateDistanceFieldAsIfTwoSided = false;
	UStaticMesh* StaticMesh = nullptr;
	UStaticMesh* GenerateSource = nullptr;
	FString DDCKey;
	FCardRepresentationData* GeneratedCardRepresentation;
	TUniquePtr<FAsyncTask<FAsyncCardRepresentationTaskWorker>> AsyncTask = nullptr;
};

/** Class that manages asynchronous building of mesh distance fields. */
class FCardRepresentationAsyncQueue : IAssetCompilingManager
{
public:

	ENGINE_API FCardRepresentationAsyncQueue();

	ENGINE_API virtual ~FCardRepresentationAsyncQueue();

	/** Adds a new build task. */
	ENGINE_API void AddTask(FAsyncCardRepresentationTask* Task);

	/** Cancel the build on this specific static mesh or block until it is completed if already started. */
	ENGINE_API void CancelBuild(UStaticMesh* StaticMesh);

	/** Blocks the main thread until the async build are either cancelled or completed. */
	ENGINE_API void CancelAllOutstandingBuilds();

	/** Blocks the main thread until the async build of the specified mesh is complete. */
	ENGINE_API void BlockUntilBuildComplete(UStaticMesh* StaticMesh, bool bWarnIfBlocked);

	/** Blocks the main thread until all async builds complete. */
	ENGINE_API void BlockUntilAllBuildsComplete();

	/** Called once per frame, fetches completed tasks and applies them to the scene. */
	ENGINE_API void ProcessAsyncTasks(bool bLimitExecutionTime = false) override;

	/** Blocks until it is safe to shut down (worker threads are idle). */
	ENGINE_API void Shutdown() override;

	int32 GetNumOutstandingTasks() const
	{
		FScopeLock Lock(&CriticalSection);
		return ReferencedTasks.Num();
	}

	/** Get the name of the asset type this compiler handles */
	ENGINE_API static FName GetStaticAssetTypeName();

private:
	friend FAsyncCardRepresentationTaskWorker;

	ENGINE_API FName GetAssetTypeName() const override;
	ENGINE_API FTextFormat GetAssetNameFormat() const override;
	ENGINE_API TArrayView<FName> GetDependentTypeNames() const override;
	ENGINE_API int32 GetNumRemainingAssets() const override;
	ENGINE_API void FinishAllCompilation() override;
	
	void ProcessPendingTasks();

	TUniquePtr<FQueuedThreadPool> ThreadPool;

	/** Builds a single task with the given threadpool.  Called from the worker thread. */
	void Build(FAsyncCardRepresentationTask* Task, class FQueuedThreadPool& ThreadPool);

	/** Change the priority of the background task. */
	void RescheduleBackgroundTask(FAsyncCardRepresentationTask* InTask, EQueuedWorkPriority InPriority);

	/** Task will be sent to a background worker. */
	void StartBackgroundTask(FAsyncCardRepresentationTask* Task);

	/** Cancel or finish any background work for the given task. */
	static void CancelAndDeleteBackgroundTask(TArray<FAsyncCardRepresentationTask*> Tasks);

	/** Return whether the task has become invalid and should be cancelled (i.e. reference unreachable objects) */
	bool IsTaskInvalid(FAsyncCardRepresentationTask* Task) const;

	/** Used to cancel tasks that are not needed anymore when garbage collection occurs */
	void OnPostReachabilityAnalysis();

	/** Game-thread managed list of tasks in the async system. */
	TArray<FAsyncCardRepresentationTask*> ReferencedTasks;

	/** Tasks that are waiting on static mesh compilation to proceed */
	TArray<FAsyncCardRepresentationTask*> PendingTasks;

	/** Tasks that have completed processing. */
	TLockFreePointerListLIFO<FAsyncCardRepresentationTask> CompletedTasks;

	FDelegateHandle PostReachabilityAnalysisHandle;

	class IMeshUtilities* MeshUtilities;

	mutable FCriticalSection CriticalSection;
};

/** Global build queue. */
extern ENGINE_API FCardRepresentationAsyncQueue* GCardRepresentationAsyncQueue;

extern ENGINE_API FString BuildCardRepresentationDerivedDataKey(const FString& InMeshKey);

extern ENGINE_API void BeginCacheMeshCardRepresentation(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMeshAsset, class FStaticMeshRenderData& RenderData, const FString& DistanceFieldKey, FSourceMeshDataForDerivedDataTask* OptionalSourceMeshData);