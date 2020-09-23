// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshCardRepresentation.cpp
=============================================================================*/

#include "MeshCardRepresentation.h"
#include "HAL/RunnableThread.h"
#include "HAL/Runnable.h"
#include "Misc/App.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshResources.h"
#include "ProfilingDebugging/CookStats.h"
#include "Templates/UniquePtr.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Async/ParallelFor.h"

#if WITH_EDITOR
#include "DerivedDataCacheInterface.h"
#include "MeshUtilities.h"
#endif

#if WITH_EDITORONLY_DATA
#include "IMeshBuilderModule.h"
#endif

#if ENABLE_COOK_STATS
namespace CardRepresentationCookStats
{
	FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("CardRepresentation.Usage"), TEXT(""));
	});
}
#endif

static TAutoConsoleVariable<int32> CVarCardRepresentation(
	TEXT("r.MeshCardRepresentation"),
	1,
	TEXT(""),
	ECVF_ReadOnly);

static TAutoConsoleVariable<float> CVarLumenCubeMapTreeBuildMinSurface(
	TEXT("r.LumenCubeMapTreeBuild.MinSurface"),
	0.1f,
	TEXT("Min surface treshold to spawn a new card, [0;1] range."),
	ECVF_ReadOnly);

FCardRepresentationAsyncQueue* GCardRepresentationAsyncQueue = NULL;

#if WITH_EDITOR

// DDC key for card representation data, must be changed when modifying the generation code or data format
#define CARDREPRESENTATION_DERIVEDDATA_VER TEXT("378A453D4B7A4B163E62A302B1EE8BD8")

FString BuildCardRepresentationDerivedDataKey(const FString& InMeshKey)
{
	const float MinSurfaceThreshold = CVarLumenCubeMapTreeBuildMinSurface.GetValueOnAnyThread();

	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("CARD"),
		*FString::Printf(TEXT("%s_%s_%.3f"), *InMeshKey, CARDREPRESENTATION_DERIVEDDATA_VER, MinSurfaceThreshold),
		TEXT(""));
}

#endif

#if WITH_EDITORONLY_DATA

void BeginCacheMeshCardRepresentation(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMeshAsset, FStaticMeshRenderData& RenderData, const FString& DistanceFieldKey, FSourceMeshDataForDerivedDataTask* OptionalSourceMeshData)
{
	static const auto CVarCards = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MeshCardRepresentation"));

	if (CVarCards->GetValueOnAnyThread() != 0)
	{
		FString Key = BuildCardRepresentationDerivedDataKey(DistanceFieldKey);
		if (RenderData.LODResources.IsValidIndex(0))
		{
			if (!RenderData.LODResources[0].CardRepresentationData)
			{
				RenderData.LODResources[0].CardRepresentationData = new FCardRepresentationData();
			}

			UStaticMesh* MeshToGenerateFrom = StaticMeshAsset;

			RenderData.LODResources[0].CardRepresentationData->CacheDerivedData(Key, TargetPlatform, StaticMeshAsset, MeshToGenerateFrom, OptionalSourceMeshData);
		}
	}
}

void FCardRepresentationData::CacheDerivedData(const FString& InDDCKey, const ITargetPlatform* TargetPlatform, UStaticMesh* Mesh, UStaticMesh* GenerateSource, FSourceMeshDataForDerivedDataTask* OptionalSourceMeshData)
{
	TArray<uint8> DerivedData;

	COOK_STAT(auto Timer = CardRepresentationCookStats::UsageStats.TimeSyncWork());
	if (GetDerivedDataCacheRef().GetSynchronous(*InDDCKey, DerivedData, Mesh->GetPathName()))
	{
		COOK_STAT(Timer.AddHit(DerivedData.Num()));
		FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);
		Ar << *this;
	}
	else
	{
		// We don't actually build the resource until later, so only track the cycles used here.
		COOK_STAT(Timer.TrackCyclesOnly());
		FAsyncCardRepresentationTask* NewTask = new FAsyncCardRepresentationTask;
		NewTask->DDCKey = InDDCKey;
		check(Mesh && GenerateSource);
		NewTask->StaticMesh = Mesh;
		NewTask->GenerateSource = GenerateSource;
		NewTask->GeneratedCardRepresentation = new FCardRepresentationData();

		// Nanite overrides source static mesh with a coarse representation. Need to load original data before we build the mesh SDF.
		if (OptionalSourceMeshData)
		{
			NewTask->SourceMeshData = *OptionalSourceMeshData;
		}
		else if (Mesh->NaniteSettings.bEnabled)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("FCardRepresentationData::BuildMesh"));

			uint32 NumTexCoords = 0;
			bool bHasColors = false;

			IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForPlatform(TargetPlatform);
			if (!MeshBuilderModule.BuildMesh(Mesh, NewTask->SourceMeshData.Vertices, NewTask->SourceMeshData.Indices, NewTask->SourceMeshData.Sections, /* bBuildOnlyPosition */ true, NumTexCoords, bHasColors))
			{
				UE_LOG(LogStaticMesh, Error, TEXT("Failed to build static mesh. See previous line(s) for details."));
				return;
			}
		}

		GCardRepresentationAsyncQueue->AddTask(NewTask);
	}
}

#endif

int32 GUseAsyncCardRepresentationBuildQueue = 1;
static FAutoConsoleVariableRef CVarCardRepresentationAsyncBuildQueue(
	TEXT("r.MeshCardRepresentation.Async"),
	GUseAsyncCardRepresentationBuildQueue,
	TEXT("."),
	ECVF_Default | ECVF_ReadOnly
	);

class FBuildCardRepresentationThreadRunnable : public FRunnable
{
public:
	/** Initialization constructor. */
	FBuildCardRepresentationThreadRunnable(
		FCardRepresentationAsyncQueue* InAsyncQueue
		)
		: NextThreadIndex(0)
		, AsyncQueue(*InAsyncQueue)
		, Thread(nullptr)
		, bIsRunning(false)
		, bForceFinish(false)
	{}

	virtual ~FBuildCardRepresentationThreadRunnable()
	{
		check(!bIsRunning);
	}

	// FRunnable interface.
	virtual bool Init() { bIsRunning = true; return true; }
	virtual void Exit() { bIsRunning = false; }
	virtual void Stop() { bForceFinish = true; }
	virtual uint32 Run();

	void Launch()
	{
		check(!bIsRunning);

		// Calling Reset will call Kill which in turn will call Stop and set bForceFinish to true.
		Thread.Reset();

		// Now we can set bForceFinish to false without being overwritten by the old thread shutting down.
		bForceFinish = false;
		Thread.Reset(FRunnableThread::Create(this, *FString::Printf(TEXT("BuildCardRepresentationThread%u"), NextThreadIndex), 0, TPri_SlightlyBelowNormal, FPlatformAffinity::GetPoolThreadMask()));

		// Set this now before exiting so that IsRunning() returns true without having to wait on the thread to be completely started.
		bIsRunning = true;
		NextThreadIndex++;
	}

	inline bool IsRunning() { return bIsRunning; }

private:

	int32 NextThreadIndex;

	FCardRepresentationAsyncQueue& AsyncQueue;

	/** The runnable thread */
	TUniquePtr<FRunnableThread> Thread;

	TUniquePtr<FQueuedThreadPool> WorkerThreadPool;

	volatile bool bIsRunning;
	volatile bool bForceFinish;
};

FQueuedThreadPool* CreateCardWorkerThreadPool()
{
	/*
	const int32 NumThreads = FMath::Max<int32>(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2, 1);
	FQueuedThreadPool* WorkerThreadPool = FQueuedThreadPool::Allocate();
	WorkerThreadPool->Create(NumThreads, 32 * 1024, TPri_BelowNormal);
	return WorkerThreadPool;
	*/

	return nullptr;
}

uint32 FBuildCardRepresentationThreadRunnable::Run()
{
	bool bHasWork = true;
	
	// Do not exit right away if no work to do as it often leads to stop and go problems
	// when tasks are being queued at a slower rate than the processor capability to process them.
	const uint64 ExitAfterIdleCycle = static_cast<uint64>(10.0 / FPlatformTime::GetSecondsPerCycle64()); // 10s

	uint64 LastWorkCycle = FPlatformTime::Cycles64();
	while (!bForceFinish && (bHasWork || (FPlatformTime::Cycles64() - LastWorkCycle) < ExitAfterIdleCycle))
	{
		// LIFO build order, since meshes actually visible in a map are typically loaded last
		FAsyncCardRepresentationTask* Task = AsyncQueue.TaskQueue.Pop();

		FQueuedThreadPool* ThreadPool = nullptr;

#if WITH_EDITOR
		ThreadPool = GLargeThreadPool;
#endif

		if (Task)
		{
			if (!ThreadPool)
			{
				if (!WorkerThreadPool)
				{
					WorkerThreadPool.Reset(CreateCardWorkerThreadPool());
				}

				ThreadPool = WorkerThreadPool.Get();
			}

			AsyncQueue.Build(Task, *ThreadPool);
			LastWorkCycle = FPlatformTime::Cycles64();

			bHasWork = true;
		}
		else
		{
			bHasWork = false;
			FPlatformProcess::Sleep(.01f);
		}
	}

	WorkerThreadPool = nullptr;

	return 0;
}

FAsyncCardRepresentationTask::FAsyncCardRepresentationTask()
	: StaticMesh(nullptr)
	, GenerateSource(nullptr)
{
}


FCardRepresentationAsyncQueue::FCardRepresentationAsyncQueue() 
{
#if WITH_EDITOR
	MeshUtilities = NULL;
#endif

	ThreadRunnable = MakeUnique<FBuildCardRepresentationThreadRunnable>(this);
}

FCardRepresentationAsyncQueue::~FCardRepresentationAsyncQueue()
{
}

void FCardRepresentationAsyncQueue::AddTask(FAsyncCardRepresentationTask* Task)
{
#if WITH_EDITOR
	if (!MeshUtilities)
	{
		MeshUtilities = &FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
	}
	
	{
		// Array protection when called from multiple threads
		FScopeLock Lock(&CriticalSection);
		ReferencedTasks.Add(Task);
	}

	if (GUseAsyncCardRepresentationBuildQueue)
	{
		TaskQueue.Push(Task);

		// Logic protection when called from multiple threads
		FScopeLock Lock(&CriticalSection);
		if (!ThreadRunnable->IsRunning())
		{
			ThreadRunnable->Launch();
		}
	}
	else
	{
		TUniquePtr<FQueuedThreadPool> WorkerThreadPool(CreateCardWorkerThreadPool());
		Build(Task, *WorkerThreadPool);
	}
#else
	UE_LOG(LogStaticMesh,Fatal,TEXT("Tried to build a card representation without editor support (this should have been done during cooking)"));
#endif
}

void FCardRepresentationAsyncQueue::BlockUntilBuildComplete(UStaticMesh* StaticMesh, bool bWarnIfBlocked)
{
	// We will track the wait time here, but only the cycles used.
	// This function is called whether or not an async task is pending, 
	// so we have to look elsewhere to properly count how many resources have actually finished building.
	COOK_STAT(auto Timer = CardRepresentationCookStats::UsageStats.TimeAsyncWait());
	COOK_STAT(Timer.TrackCyclesOnly());
	bool bReferenced = false;
	bool bHadToBlock = false;
	double StartTime = 0;

	do 
	{
		ProcessAsyncTasks();

		bReferenced = false;

		{
			FScopeLock Lock(&CriticalSection);
			for (int TaskIndex = 0; TaskIndex < ReferencedTasks.Num(); TaskIndex++)
			{
				bReferenced = bReferenced || ReferencedTasks[TaskIndex]->StaticMesh == StaticMesh;
				bReferenced = bReferenced || ReferencedTasks[TaskIndex]->GenerateSource == StaticMesh;
			}
		}

		if (bReferenced)
		{
			if (!bHadToBlock)
			{
				StartTime = FPlatformTime::Seconds();
			}

			bHadToBlock = true;
			FPlatformProcess::Sleep(.01f);
		}
	} 
	while (bReferenced);

	if (bHadToBlock &&
		bWarnIfBlocked
#if WITH_EDITOR
		&& !FAutomationTestFramework::Get().GetCurrentTest() // HACK - Don't output this warning during automation test
#endif
		)
	{
		UE_LOG(LogStaticMesh, Display, TEXT("Main thread blocked for %.3fs for async card representation build of %s to complete!  This can happen if the mesh is rebuilt excessively."),
			(float)(FPlatformTime::Seconds() - StartTime), 
			*StaticMesh->GetName());
	}
}

void FCardRepresentationAsyncQueue::BlockUntilAllBuildsComplete()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCardRepresentationAsyncQueue::BlockUntilAllBuildsComplete)
	do 
	{
		ProcessAsyncTasks();
		FPlatformProcess::Sleep(.01f);
	} 
	while (GetNumOutstandingTasks() > 0);
}

void FCardRepresentationAsyncQueue::Build(FAsyncCardRepresentationTask* Task, FQueuedThreadPool& ThreadPool)
{
#if WITH_EDITOR
	
	// Editor 'force delete' can null any UObject pointers which are seen by reference collecting (eg UProperty or serialized)
	if (Task->StaticMesh && Task->GenerateSource)
	{
		const FStaticMeshLODResources& LODModel = Task->GenerateSource->RenderData->LODResources[0];

		Task->bSuccess = MeshUtilities->GenerateCardRepresentationData(
			Task->StaticMesh->GetName(),
			Task->SourceMeshData,
			LODModel,
			ThreadPool,
			Task->GenerateSource->RenderData->Bounds,
			Task->GenerateSource->RenderData->LODResources[0].DistanceFieldData,
			*Task->GeneratedCardRepresentation);
	}

    CompletedTasks.Push(Task);

#endif
}

void FCardRepresentationAsyncQueue::AddReferencedObjects(FReferenceCollector& Collector)
{	
	FScopeLock Lock(&CriticalSection);
	for (int TaskIndex = 0; TaskIndex < ReferencedTasks.Num(); TaskIndex++)
	{
		// Make sure none of the UObjects referenced by the async tasks are GC'ed during the task
		Collector.AddReferencedObject(ReferencedTasks[TaskIndex]->StaticMesh);
		Collector.AddReferencedObject(ReferencedTasks[TaskIndex]->GenerateSource);
	}
}

void FCardRepresentationAsyncQueue::ProcessAsyncTasks()
{
#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(FCardRepresentationAsyncQueue::ProcessAsyncTasks)

	TArray<FAsyncCardRepresentationTask*> LocalCompletedTasks;
	CompletedTasks.PopAll(LocalCompletedTasks);

	for (int TaskIndex = 0; TaskIndex < LocalCompletedTasks.Num(); TaskIndex++)
	{
		// We want to count each resource built from a DDC miss, so count each iteration of the loop separately.
		COOK_STAT(auto Timer = CardRepresentationCookStats::UsageStats.TimeSyncWork());
		FAsyncCardRepresentationTask* Task = LocalCompletedTasks[TaskIndex];

		{
			FScopeLock Lock(&CriticalSection);
			ReferencedTasks.Remove(Task);
		}

		// Editor 'force delete' can null any UObject pointers which are seen by reference collecting (eg UProperty or serialized)
		if (Task->StaticMesh && Task->bSuccess)
		{
			FCardRepresentationData* OldCardData = Task->StaticMesh->RenderData->LODResources[0].CardRepresentationData;

			{
				// Cause all components using this static mesh to get re-registered, which will recreate their proxies and primitive uniform buffers
				FStaticMeshComponentRecreateRenderStateContext RecreateRenderStateContext(Task->StaticMesh, false);

				// Assign the new data
				Task->StaticMesh->RenderData->LODResources[0].CardRepresentationData = Task->GeneratedCardRepresentation;
			}

			// Rendering thread may still be referencing the old one, use the deferred cleanup interface to delete it next frame when it is safe
			BeginCleanup(OldCardData);

			{
				TArray<uint8> DerivedData;
				// Save built data to DDC
				FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
				Ar << *(Task->StaticMesh->RenderData->LODResources[0].CardRepresentationData);
				GetDerivedDataCacheRef().Put(*Task->DDCKey, DerivedData, Task->StaticMesh->GetPathName());
				COOK_STAT(Timer.AddMiss(DerivedData.Num()));
			}
		}

		delete Task;
	}

	bool bRemainingTasks = false;
	{
		FScopeLock Lock(&CriticalSection);
		bRemainingTasks = ReferencedTasks.Num() > 0;
	}

	if (bRemainingTasks && !ThreadRunnable->IsRunning())
	{
		ThreadRunnable->Launch();
	}
#endif
}

void FCardRepresentationAsyncQueue::Shutdown()
{
	ThreadRunnable->Stop();
	bool bLogged = false;

	while (ThreadRunnable->IsRunning())
	{
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogStaticMesh,Log,TEXT("Abandoning remaining async card representation tasks for shutdown"));
		}
		FPlatformProcess::Sleep(0.01f);
	}
}