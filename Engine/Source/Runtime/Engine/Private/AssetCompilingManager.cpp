// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetCompilingManager.h"

#include "HAL/LowLevelMemStats.h"
#include "HAL/LowLevelMemTracker.h"

DECLARE_LLM_MEMORY_STAT(TEXT("AssetCompilation"), STAT_AssetCompilationLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AssetCompilation"), STAT_AssetCompilationSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(AssetCompilation, NAME_None, NAME_None, GET_STATFNAME(STAT_AssetCompilationLLM), GET_STATFNAME(STAT_AssetCompilationSummaryLLM));

#include "Misc/QueuedThreadPoolWrapper.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "ActorDeferredScriptManager.h"
#include "StaticMeshCompiler.h"
#include "TextureCompiler.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstance.h"
#include "ObjectCacheContext.h"
#include "AsyncCompilationHelpers.h"
#include "SkeletalMeshCompiler.h"
#include "Algo/TopologicalSort.h"
#include "Algo/Find.h"
#include "ProfilingDebugging/CountersTrace.h"

#define LOCTEXT_NAMESPACE "AssetCompilingManager"

#if WITH_EDITOR

static AsyncCompilationHelpers::FAsyncCompilationStandardCVars CVarAsyncCompilationStandard(
	TEXT("Asset"), 
	TEXT("assets"),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			FAssetCompilingManager::Get().FinishAllCompilation();
		}
	));

static TAutoConsoleVariable<int32> CVarAsyncAssetCompilationMemoryPerCore(
	TEXT("Editor.AsyncAssetCompilationMemoryPerCore"),
	4,
	TEXT("0 - No memory limit per asset.\n")
	TEXT("N - Dynamically adjust concurrency limit by dividing free system memory by this number (in GB).\n")
	TEXT("Limit concurrency for async processing based on RAM available.\n"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsyncAssetCompilationMaxMemoryUsage(
	TEXT("Editor.AsyncAssetCompilationMaxMemoryUsage"),
	0,
	TEXT("0 - No hard memory limit, will be tuned against system available memory (recommended default).\n")
	TEXT("N - Try to limit total memory usage for asset compilation to this amount (in GB).\n")
	TEXT("Try to stay under specified memory limit for asset compilation by reducing concurrency when under memory pressure.\n"),
	ECVF_Default);

TRACE_DECLARE_INT_COUNTER(AsyncCompilationMaxConcurrency, TEXT("AsyncCompilation/MaxConcurrency"));
TRACE_DECLARE_INT_COUNTER(AsyncCompilationConcurrency, TEXT("AsyncCompilation/Concurrency"));
TRACE_DECLARE_MEMORY_COUNTER(AsyncCompilationTotalMemoryLimit, TEXT("AsyncCompilation/TotalMemoryLimit"));
TRACE_DECLARE_MEMORY_COUNTER(AsyncCompilationTotalEstimatedMemory, TEXT("AsyncCompilation/TotalEstimatedMemory"));

#endif //#if WITH_EDITOR

namespace AssetCompilingManagerImpl
{
	static void EnsureInitializedCVars()
	{
#if WITH_EDITOR
		static bool bIsInitialized = false;

		if (!bIsInitialized)
		{
			bIsInitialized = true;

			AsyncCompilationHelpers::EnsureInitializedCVars(
				TEXT("asset"),
				CVarAsyncCompilationStandard.AsyncCompilation,
				CVarAsyncCompilationStandard.AsyncCompilationMaxConcurrency
			);
		}
#endif
	}
}

#if WITH_EDITOR

class FMemoryBoundQueuedThreadPoolWrapper : public FQueuedThreadPoolWrapper
{
public:
	/**
	 * InWrappedQueuedThreadPool  Underlying thread pool to schedule task to.
	 * InMaxConcurrency           Maximum number of concurrent tasks allowed, -1 will limit concurrency to number of threads available in the underlying thread pool.
	 * InPriorityMapper           Thread-safe function used to map any priority from this Queue to the priority that should be used when scheduling the task on the underlying thread pool.
	 */
	FMemoryBoundQueuedThreadPoolWrapper(FQueuedThreadPool* InWrappedQueuedThreadPool, int32 InMaxConcurrency = -1, TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> InPriorityMapper = [](EQueuedWorkPriority InPriority) { return InPriority; })
		: FQueuedThreadPoolWrapper(InWrappedQueuedThreadPool, InMaxConcurrency, InPriorityMapper)
	{
	}

	int64 GetHardMemoryLimit() const
	{
		return (int64)CVarAsyncAssetCompilationMaxMemoryUsage.GetValueOnAnyThread() * 1024 * 1024 * 1024;
	}

	int64 GetDefaultMemoryPerAsset() const
	{
		return FMath::Max(0ll, (int64)CVarAsyncAssetCompilationMemoryPerCore.GetValueOnAnyThread(false) * 1024 * 1024 * 1024);
	}

	int64 GetMemoryLimit() const
	{
		const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();

		// Just make sure available physical fits in a int64, if it's bigger than that, we're not expecting to be memory limited anyway
		// Also uses AvailableVirtual because the system might have plenty of physical memory but still be limited by virtual memory available in some cases.
		//   (i.e. per-process quota, paging file size lower than actual memory available, etc.).
		const int64 AvailableMemory = (int64)FMath::Min3(MemoryStats.AvailablePhysical, MemoryStats.AvailableVirtual, (uint64)INT64_MAX);

		int64 HardMemoryLimit = GetHardMemoryLimit();
		if (HardMemoryLimit > 0)
		{
			return FMath::Min(HardMemoryLimit, AvailableMemory);
		}
		
		return AvailableMemory;
	}

	int64 GetRequiredMemory(const IQueuedWork* InWork) const
	{
		int64 RequiredMemory = InWork->GetRequiredMemory();

		// If the task doesn't support a memory estimate, use global defined default
		if (RequiredMemory == -1)
		{
			RequiredMemory = GetDefaultMemoryPerAsset();
		}

		return RequiredMemory;
	}

	void UpdateCounters()
	{
		TRACE_COUNTER_SET(AsyncCompilationTotalMemoryLimit, GetMemoryLimit());
		TRACE_COUNTER_SET(AsyncCompilationTotalEstimatedMemory, TotalEstimatedMemory);
		TRACE_COUNTER_SET(AsyncCompilationConcurrency, GetCurrentConcurrency());
	}

	void OnScheduled(const IQueuedWork* InWork) override
	{
		TotalEstimatedMemory += GetRequiredMemory(InWork);
		UpdateCounters();
	}

	void OnUnscheduled(const IQueuedWork* InWork) override
	{
		TotalEstimatedMemory -= GetRequiredMemory(InWork);
		check(TotalEstimatedMemory >= 0);
		UpdateCounters();
	}

	int32 GetMaxConcurrency() const override
	{
		int64 RequiredMemory = TotalEstimatedMemory;
		
		// Add next work memory requirement to see if it still fits in available memory
		if (IQueuedWork* NextWork = QueuedWork.Peek())
		{
			RequiredMemory += GetRequiredMemory(NextWork);
		}

		int32 DynamicMaxConcurrency = FQueuedThreadPoolWrapper::GetMaxConcurrency();
		int32 Concurrency = FQueuedThreadPoolWrapper::GetCurrentConcurrency();

		// Never limit below a concurrency of 1 or else we'll starve the asset processing and never be scheduled again
		// The idea for now is to let assets get built one by one when starving on memory, which should not increase OOM compared to the old synchronous behavior.
		if (RequiredMemory > 0 && Concurrency > 0 && RequiredMemory >= GetMemoryLimit())
		{
			DynamicMaxConcurrency = Concurrency; // Limit concurrency to what we already allowed
		}

		TRACE_COUNTER_SET(AsyncCompilationMaxConcurrency, DynamicMaxConcurrency);

		check(DynamicMaxConcurrency > 0);
		return DynamicMaxConcurrency;
	}

private:
	std::atomic<int64> TotalEstimatedMemory {0};
};

#endif // #if WITH_EDITOR

TArrayView<IAssetCompilingManager* const> FAssetCompilingManager::GetRegisteredManagers() const
{
	return TArrayView<IAssetCompilingManager* const>(AssetCompilingManagers);
}

FAssetCompilingManager::FAssetCompilingManager()
{
#if WITH_EDITOR
	RegisterManager(&FStaticMeshCompilingManager::Get());
	RegisterManager(&FSkeletalMeshCompilingManager::Get());
	RegisterManager(&FTextureCompilingManager::Get());
	RegisterManager(&FActorDeferredScriptManager::Get());
#endif
}

FQueuedThreadPool* FAssetCompilingManager::GetThreadPool() const
{
#if WITH_EDITOR
	static FQueuedThreadPoolWrapper* GAssetThreadPool = nullptr;
	if (GAssetThreadPool == nullptr && GLargeThreadPool != nullptr)
	{
		AssetCompilingManagerImpl::EnsureInitializedCVars();

		// We limit concurrency to half the workers because asset compilation is hard on total memory and memory bandwidth and can run slower when going wider than actual cores.
		// Most asset also have some form of inner parallelism during compilation.
		// Recently found out that GThreadPool and GLargeThreadPool have the same amount of workers, so can't rely on GThreadPool to be our limiter here.
		// FPlatformMisc::NumberOfCores() and FPlatformMisc::NumberOfCoresIncludingHyperthreads() also return the same value when -corelimit is used so we can't use FPlatformMisc::NumberOfCores()
		// if we want to keep the same 1:2 relationship with worker count.
		FQueuedThreadPoolWrapper* LimitedThreadPool = new FQueuedThreadPoolWrapper(GLargeThreadPool, FMath::Max(GLargeThreadPool->GetNumThreads() / 2, 1));

		// All asset priorities will resolve to a Low priority once being scheduled.
		// Any asset supporting being built async should be scheduled lower than Normal to let non-async stuff go first
		// However, we let Highest and Blocking priority pass-through as it to benefit from going to foreground threads when required (i.e. Game-thread is waiting on some assets)
		GAssetThreadPool = new FMemoryBoundQueuedThreadPoolWrapper(LimitedThreadPool, -1, [](EQueuedWorkPriority Priority) { return Priority <= EQueuedWorkPriority::Highest ? Priority : EQueuedWorkPriority::Low; });

		AsyncCompilationHelpers::BindThreadPoolToCVar(
			GAssetThreadPool,
			CVarAsyncCompilationStandard.AsyncCompilation,
			CVarAsyncCompilationStandard.AsyncCompilationResume,
			CVarAsyncCompilationStandard.AsyncCompilationMaxConcurrency
		);
	}

	return GAssetThreadPool;
#else
	return GThreadPool;
#endif
}

/**
 * Register an asset compiling manager.
 */
bool FAssetCompilingManager::RegisterManager(IAssetCompilingManager* InAssetCompilingManager)
{
	if (AssetCompilingManagers.Contains(InAssetCompilingManager))
	{
		return false;
	}

	AssetCompilingManagers.Add(InAssetCompilingManager);
	AssetCompilingManagersWithValidDependencies.Add(InAssetCompilingManager);

	auto GetVertexDependencies =
		[this](IAssetCompilingManager* CurrentManager)
	{
		TArray<IAssetCompilingManager*> Connections;
		for (FName DependentTypeName : CurrentManager->GetDependentTypeNames())
		{
			IAssetCompilingManager** DependentManager =
				Algo::FindBy(AssetCompilingManagersWithValidDependencies, DependentTypeName, [](const IAssetCompilingManager* Manager) { return Manager->GetAssetTypeName();	});

			if (DependentManager)
			{
				Connections.Add(*DependentManager);
			}
		}
		
		return Connections;
	};

	if (!Algo::TopologicalSort(AssetCompilingManagers, GetVertexDependencies))
	{
		AssetCompilingManagersWithValidDependencies.Remove(InAssetCompilingManager);
		UE_LOG(LogAsyncCompilation, Error, TEXT("AssetCompilingManager %s introduced a circular dependency, it's dependencies will be ignored"), *InAssetCompilingManager->GetAssetTypeName().ToString());
	}

	return true;
}

bool FAssetCompilingManager::UnregisterManager(IAssetCompilingManager* InAssetCompilingManager)
{
	if (!AssetCompilingManagers.Contains(InAssetCompilingManager))
	{
		return false;
	}

	AssetCompilingManagers.Remove(InAssetCompilingManager);
	AssetCompilingManagersWithValidDependencies.Remove(InAssetCompilingManager);
	return true;
}

/**
 * Returns the number of outstanding asset compilations.
 */
int32 FAssetCompilingManager::GetNumRemainingAssets() const
{
	int32 RemainingAssets = 0;
	for (IAssetCompilingManager* AssetCompilingManager : AssetCompilingManagers)
	{
		RemainingAssets += AssetCompilingManager->GetNumRemainingAssets();
	}

	return RemainingAssets;
}

/**
 * Blocks until completion of all assets.
 */
void FAssetCompilingManager::FinishAllCompilation()
{
	for (IAssetCompilingManager* AssetCompilingManager : AssetCompilingManagers)
	{
		AssetCompilingManager->FinishAllCompilation();
	}
}

/**
 * Cancel any pending work and blocks until it is safe to shut down.
 */
void FAssetCompilingManager::Shutdown()
{
	for (IAssetCompilingManager* AssetCompilingManager : AssetCompilingManagers)
	{
		AssetCompilingManager->Shutdown();
	}

#if WITH_EDITOR
	if (FParse::Param(FCommandLine::Get(), TEXT("DumpAsyncStallsOnExit")))
	{
		AsyncCompilationHelpers::DumpStallStacks();
	}
#endif // #if WITH_EDITOR
}

FAssetCompilingManager& FAssetCompilingManager::Get()
{
	static FAssetCompilingManager Singleton;
	return Singleton;
}

void FAssetCompilingManager::ProcessAsyncTasks(bool bLimitExecutionTime)
{
	// Reuse ObjectIterator Caching and Reverse lookups for the duration of all asset updates
	FObjectCacheContextScope ObjectCacheScope;

	for (IAssetCompilingManager* AssetCompilingManager : AssetCompilingManagers)
	{
		AssetCompilingManager->ProcessAsyncTasks(bLimitExecutionTime);
	}
}

#undef LOCTEXT_NAMESPACE