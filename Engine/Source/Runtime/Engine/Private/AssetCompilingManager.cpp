// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetCompilingManager.h"

#if WITH_EDITOR

#include "Misc/QueuedThreadPoolWrapper.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "StaticMeshCompiler.h"
#include "TextureCompiler.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstance.h"

#define LOCTEXT_NAMESPACE "AssetCompilingManager"

static TAutoConsoleVariable<int32> CVarAsyncAssetCompilation(
	TEXT("Editor.AsyncAssetCompilation"),
	1,
	TEXT("1 - Async asset compilation is enabled.\n")
	TEXT("2 - Async asset compilation is enabled but on pause (for debugging).\n")
	TEXT("When enabled, assetes will be replaced by placeholders until they are ready\n")
	TEXT("to reduce stalls on the game thread and improve overall editor performance."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsyncAssetCompilationMaxConcurrency(
	TEXT("Editor.AsyncAssetCompilationMaxConcurrency"),
	-1,
	TEXT("Set the maximum number of concurrent asset compilation, -1 for unlimited."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsyncAssetCompilationMemoryPerCore(
	TEXT("Editor.AsyncAssetCompilationMemoryPerCore"),
	4,
	TEXT("0 - No memory limit per core.\n")
	TEXT("N - Dynamically adjust concurrency limit by dividing free system memory by this number (in GB).\n")
	TEXT("Limit concurrency for async processing based on RAM available.\n"),
	ECVF_Default);

static FAutoConsoleCommand CVarAsyncAssetCompilationFinishAll(
	TEXT("Editor.AsyncAssetCompilationFinishAll"),
	TEXT("Finish all asset compilations"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
	})
);

static TAutoConsoleVariable<int32> CVarAsyncAssetCompilationResume(
	TEXT("Editor.AsyncAssetCompilationResume"),
	0,
	TEXT("Number of queued work to resume while paused."),
	ECVF_Default);

namespace AssetCompilingManagerImpl
{
	static void EnsureInitializedCVars()
	{
		static bool bIsInitialized = false;

		if (!bIsInitialized)
		{
			bIsInitialized = true;

			FString Value;
			if (FParse::Value(FCommandLine::Get(), TEXT("-asyncassetcompilation="), Value))
			{
				int32 AsyncAssetCompilationValue = 0;
				if (Value == TEXT("1") || Value == TEXT("on"))
				{
					AsyncAssetCompilationValue = 1;
				}

				if (Value == TEXT("2") || Value == TEXT("paused"))
				{
					AsyncAssetCompilationValue = 2;
				}

				CVarAsyncAssetCompilation->Set(AsyncAssetCompilationValue, ECVF_SetByCommandline);
			}

			int32 MaxConcurrency = -1;
			if (FParse::Value(FCommandLine::Get(), TEXT("-asyncassetcompilationmaxconcurrency="), MaxConcurrency))
			{
				CVarAsyncAssetCompilationMaxConcurrency->Set(MaxConcurrency, ECVF_SetByCommandline);
			}
		}
	}
}

/**
 * Returns the number of outstanding asset compilations.
 */
int32 FAssetCompilingManager::GetNumRemainingAssets() const
{
	return 
		FStaticMeshCompilingManager::Get().GetNumRemainingMeshes() +
		FTextureCompilingManager::Get().GetNumRemainingTextures();
}

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

	int32 GetMaxConcurrency() const override
	{
		int32 MemoryPerCore = CVarAsyncAssetCompilationMemoryPerCore.GetValueOnAnyThread(false);
		int32 DynamicMaxConcurrency = FQueuedThreadPoolWrapper::GetMaxConcurrency();

		if (MemoryPerCore > 0)
		{
			FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
			uint64 MemoryMaxConcurrency = FMath::Max(1llu, MemoryStats.AvailablePhysical / ((uint64)MemoryPerCore * 1024 * 1024 * 1024));
			DynamicMaxConcurrency = FMath::Min((int32)MemoryMaxConcurrency, DynamicMaxConcurrency);
		}

		return DynamicMaxConcurrency;
	}
};

FQueuedThreadPool* FAssetCompilingManager::GetThreadPool() const
{
	static FQueuedThreadPoolWrapper* GAssetThreadPool = nullptr;
	if (GAssetThreadPool == nullptr)
	{
		AssetCompilingManagerImpl::EnsureInitializedCVars();

		// Wrapping GThreadPool to give AssetThreadPool it's own set of priorities and allow Pausable functionality
		// We're using GThreadPool instead of GLargeThreadPool because asset compilation is hard on total memory and memory bandwidth and can run slower when going wider than actual cores.
		// All asset priorities will resolve to a Low priority once being scheduled in the LargeThreadPool.
		// Any asset supporting being built async should be scheduled lower than Normal to let non-async stuff go first
		const int32 MaxConcurrency = CVarAsyncAssetCompilationMaxConcurrency.GetValueOnAnyThread();
		GAssetThreadPool = new FMemoryBoundQueuedThreadPoolWrapper(GThreadPool, MaxConcurrency, [](EQueuedWorkPriority) { return EQueuedWorkPriority::Low; });

		CVarAsyncAssetCompilation->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					if (Variable->GetInt() == 2)
					{
						GAssetThreadPool->Pause();
					}
					else
					{
						GAssetThreadPool->Resume();
					}
				}
			)
		);

		CVarAsyncAssetCompilationResume->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					if (Variable->GetInt() > 0)
					{
						GAssetThreadPool->Resume(Variable->GetInt());
					}
				}
			)
		);

		CVarAsyncAssetCompilationMaxConcurrency->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					GAssetThreadPool->SetMaxConcurrency(Variable->GetInt());
				}
			)
		);

		if (CVarAsyncAssetCompilation->GetInt() == 2)
		{
			GAssetThreadPool->Pause();
		}
	}

	return GAssetThreadPool;
}

/**
 * Blocks until completion of all assets.
 */
void FAssetCompilingManager::FinishAllCompilation()
{
	FStaticMeshCompilingManager::Get().FinishAllCompilation();
	FTextureCompilingManager::Get().FinishAllCompilation();
}

/**
 * Cancel any pending work and blocks until it is safe to shut down.
 */
void FAssetCompilingManager::Shutdown()
{
	FStaticMeshCompilingManager::Get().Shutdown();
	FTextureCompilingManager::Get().Shutdown();
}

FAssetCompilingManager& FAssetCompilingManager::Get()
{
	static FAssetCompilingManager Singleton;
	return Singleton;
}

void FAssetCompilingManager::ProcessAsyncTasks(bool bLimitExecutionTime)
{
	// Update textures first to avoid having to update the render state
	// of static mesh in the same frame we created them...
	FTextureCompilingManager::Get().ProcessAsyncTasks(bLimitExecutionTime);

	FStaticMeshCompilingManager::Get().ProcessAsyncTasks(bLimitExecutionTime);
}

#undef LOCTEXT_NAMESPACE
#endif // #if WITH_EDITOR
