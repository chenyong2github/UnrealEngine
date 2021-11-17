// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshCompiler.h"
#include "AssetCompilingManager.h"
#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR

#include "ObjectCacheContext.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Misc/QueuedThreadPoolWrapper.h"
#include "EngineModule.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/StrongObjectPtr.h"
#include "ShaderCompiler.h"
#include "TextureCompiler.h"
#include "Misc/IQueuedWork.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "ContentStreaming.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "AI/NavigationSystemBase.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CountersTrace.h"

#define LOCTEXT_NAMESPACE "SkeletalMeshCompiler"

static AsyncCompilationHelpers::FAsyncCompilationStandardCVars CVarAsyncSkeletalMeshStandard(
	TEXT("SkeletalMesh"),
	TEXT("skeletal meshes"),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			FSkeletalMeshCompilingManager::Get().FinishAllCompilation();
		}
	));

namespace SkeletalMeshCompilingManagerImpl
{
	static void EnsureInitializedCVars()
	{
		static bool bIsInitialized = false;

		if (!bIsInitialized)
		{
			bIsInitialized = true;
			
			AsyncCompilationHelpers::EnsureInitializedCVars(
				TEXT("skeletalmesh"),
				CVarAsyncSkeletalMeshStandard.AsyncCompilation,
				CVarAsyncSkeletalMeshStandard.AsyncCompilationMaxConcurrency,
				GET_MEMBER_NAME_CHECKED(UEditorExperimentalSettings, bEnableAsyncSkeletalMeshCompilation));
		}
	}
}

FSkeletalMeshCompilingManager::FSkeletalMeshCompilingManager()
	: Notification(GetAssetNameFormat())
{
}

FName FSkeletalMeshCompilingManager::GetAssetTypeName() const
{
	return TEXT("UE-SkeletalMesh");
}

FTextFormat FSkeletalMeshCompilingManager::GetAssetNameFormat() const
{
	return LOCTEXT("SkeletalMeshNameFormat", "{0}|plural(one=Skeletal Mesh,other=Skeletal Meshes)");
}

TArrayView<FName> FSkeletalMeshCompilingManager::GetDependentTypeNames() const
{
	// Texture and shaders can affect materials which can affect Skeletal Meshes once they are visible.
	// Adding these dependencies can reduces the actual number of render state update we need to do in a frame
	static FName DependentTypeNames[] = 
	{
		FTextureCompilingManager::GetStaticAssetTypeName(), 
		FShaderCompilingManager::GetStaticAssetTypeName() 
	};
	return TArrayView<FName>(DependentTypeNames);
}

int32 FSkeletalMeshCompilingManager::GetNumRemainingAssets() const
{
	return GetNumRemainingJobs();
}

EQueuedWorkPriority FSkeletalMeshCompilingManager::GetBasePriority(USkeletalMesh* InSkeletalMesh) const
{
	return EQueuedWorkPriority::Low;
}

FQueuedThreadPool* FSkeletalMeshCompilingManager::GetThreadPool() const
{
	static FQueuedThreadPoolDynamicWrapper* GSkeletalMeshThreadPool = nullptr;
	if (GSkeletalMeshThreadPool == nullptr && FAssetCompilingManager::Get().GetThreadPool() != nullptr)
	{
		SkeletalMeshCompilingManagerImpl::EnsureInitializedCVars();

		// For now, skeletal mesh have almost no high-level awareness of their async behavior.
		// Let them build first to avoid game-thread stalls as much as possible.
		TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> PriorityMapper = [](EQueuedWorkPriority) { return EQueuedWorkPriority::Highest; };

		// Skeletal meshes will be scheduled on the asset thread pool, where concurrency limits might by dynamically adjusted depending on memory constraints.
		GSkeletalMeshThreadPool = new FQueuedThreadPoolDynamicWrapper(FAssetCompilingManager::Get().GetThreadPool(), -1, PriorityMapper);

		AsyncCompilationHelpers::BindThreadPoolToCVar(
			GSkeletalMeshThreadPool,
			CVarAsyncSkeletalMeshStandard.AsyncCompilation,
			CVarAsyncSkeletalMeshStandard.AsyncCompilationResume,
			CVarAsyncSkeletalMeshStandard.AsyncCompilationMaxConcurrency
		);
	}

	return GSkeletalMeshThreadPool;
}

void FSkeletalMeshCompilingManager::Shutdown()
{
	bHasShutdown = true;
	if (GetNumRemainingJobs())
	{
		check(IsInGameThread());
		TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshCompilingManager::Shutdown)

		if (GetNumRemainingJobs())
		{
			TArray<USkeletalMesh*> PendingSkeletalMeshes;
			PendingSkeletalMeshes.Reserve(GetNumRemainingJobs());

			for (TWeakObjectPtr<USkeletalMesh>& WeakSkeletalMesh : RegisteredSkeletalMesh)
			{
				if (WeakSkeletalMesh.IsValid())
				{
					USkeletalMesh* SkeletalMesh = WeakSkeletalMesh.Get();
					if (!SkeletalMesh->IsAsyncTaskComplete())
					{
						if (SkeletalMesh->AsyncTask->Cancel())
						{
							SkeletalMesh->AsyncTask.Reset();
						}
					}

					if (SkeletalMesh->AsyncTask)
					{
						PendingSkeletalMeshes.Add(SkeletalMesh);
					}
				}
			}

			FinishCompilation(PendingSkeletalMeshes);
		}
	}
}

bool FSkeletalMeshCompilingManager::IsAsyncCompilationEnabled() const
{
	if (bHasShutdown)
	{
		return false;
	}

	SkeletalMeshCompilingManagerImpl::EnsureInitializedCVars();

	return CVarAsyncSkeletalMeshStandard.AsyncCompilation.GetValueOnAnyThread() != 0;
}

TRACE_DECLARE_INT_COUNTER(QueuedSkeletalMeshCompilation, TEXT("AsyncCompilation/QueuedSkeletalMesh"));
void FSkeletalMeshCompilingManager::UpdateCompilationNotification()
{
	TRACE_COUNTER_SET(QueuedSkeletalMeshCompilation, GetNumRemainingJobs());
	Notification.Update(GetNumRemainingJobs());
}

void FSkeletalMeshCompilingManager::PostCompilation(TArrayView<USkeletalMesh* const> InSkeletalMeshes)
{
	if (InSkeletalMeshes.Num())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(OnAssetPostCompileEvent);

		TArray<FAssetCompileData> AssetsData;
		AssetsData.Reserve(InSkeletalMeshes.Num());

		for (USkeletalMesh* SkeletalMesh : InSkeletalMeshes)
		{
			AssetsData.Emplace(SkeletalMesh);
		}

		FAssetCompilingManager::Get().OnAssetPostCompileEvent().Broadcast(AssetsData);
	}
}

void FSkeletalMeshCompilingManager::PostCompilation(USkeletalMesh* SkeletalMesh)
{
	using namespace SkeletalMeshCompilingManagerImpl;
	
	// If AsyncTask is null here, the task got canceled so we don't need to do anything
	if (SkeletalMesh->AsyncTask)
	{
		check(IsInGameThread());
		TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshCompilingManager::PostCompilation);

		UE_LOG(LogSkeletalMesh, Verbose, TEXT("Refreshing skeletal mesh %s because it is ready"), *SkeletalMesh->GetName());

		FObjectCacheContextScope ObjectCacheScope;

		// The scope is important here to destroy the FSkeletalMeshAsyncBuildScope before broadcasting events
		{
			// Acquire the async task locally to protect against re-entrance
			TUniquePtr<FSkeletalMeshAsyncBuildTask> LocalAsyncTask = MoveTemp(SkeletalMesh->AsyncTask);
			LocalAsyncTask->EnsureCompletion();

			FSkeletalMeshAsyncBuildScope AsyncBuildScope(SkeletalMesh);

			if (LocalAsyncTask->GetTask().PostLoadContext.IsSet())
			{
				SkeletalMesh->FinishPostLoadInternal(*LocalAsyncTask->GetTask().PostLoadContext);

				LocalAsyncTask->GetTask().PostLoadContext.Reset();
			}

			if (LocalAsyncTask->GetTask().BuildContext.IsSet())
			{
				SkeletalMesh->FinishBuildInternal(*LocalAsyncTask->GetTask().BuildContext);

				LocalAsyncTask->GetTask().BuildContext.Reset();
			}
		}

		// Calling this delegate during app exit might be quite dangerous and lead to crash
		// if the content browser wants to refresh a thumbnail it might try to load a package
		// which will then fail due to various reasons related to the editor shutting down.
		// Triggering this callback while garbage collecting can also result in listeners trying to look up objects
		if (!GExitPurge && !IsGarbageCollecting())
		{
			// Generate an empty property changed event, to force the asset registry tag
			// to be refreshed now that RenderData is available.
			FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
			FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(SkeletalMesh, EmptyPropertyChangedEvent);
		}
	}
}

bool FSkeletalMeshCompilingManager::IsAsyncCompilationAllowed(USkeletalMesh* SkeletalMesh) const
{
	return IsAsyncCompilationEnabled();
}

FSkeletalMeshCompilingManager& FSkeletalMeshCompilingManager::Get()
{
	static FSkeletalMeshCompilingManager Singleton;
	return Singleton;
}

int32 FSkeletalMeshCompilingManager::GetNumRemainingJobs() const
{
	return RegisteredSkeletalMesh.Num();
}

void FSkeletalMeshCompilingManager::AddSkeletalMeshes(TArrayView<USkeletalMesh* const> InSkeletalMeshes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshCompilingManager::AddSkeletalMeshes)
	check(IsInGameThread());

	// Wait until we gather enough mesh to process
	// to amortize the cost of scanning components
	//ProcessSkeletalMeshes(32 /* MinBatchSize */);

	for (USkeletalMesh* SkeletalMesh : InSkeletalMeshes)
	{
		check(SkeletalMesh->AsyncTask != nullptr);
		RegisteredSkeletalMesh.Emplace(SkeletalMesh);
	}

	UpdateCompilationNotification();
}

void FSkeletalMeshCompilingManager::FinishCompilation(TArrayView<USkeletalMesh* const> InSkeletalMeshes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshCompilingManager::FinishCompilation);

	check(IsInGameThread());

	TArray<USkeletalMesh*> PendingSkeletalMeshes;
	PendingSkeletalMeshes.Reserve(InSkeletalMeshes.Num());

	for (USkeletalMesh* SkeletalMesh : InSkeletalMeshes)
	{
		if (RegisteredSkeletalMesh.Contains(SkeletalMesh))
		{
			PendingSkeletalMeshes.Emplace(SkeletalMesh);
		}
	}

	if (PendingSkeletalMeshes.Num())
	{
		class FCompilableSkeletalMesh : public AsyncCompilationHelpers::TCompilableAsyncTask<FSkeletalMeshAsyncBuildTask>
		{
		public:
			FCompilableSkeletalMesh(USkeletalMesh* InSkeletalMesh)
				: SkeletalMesh(InSkeletalMesh)
			{
			}

			FSkeletalMeshAsyncBuildTask* GetAsyncTask() override
			{
				return SkeletalMesh->AsyncTask.Get();
			}

			TStrongObjectPtr<USkeletalMesh> SkeletalMesh;
			FName GetName() override { return SkeletalMesh->GetFName(); }
		};

		TArray<FCompilableSkeletalMesh> CompilableSkeletalMesh(PendingSkeletalMeshes);

		FObjectCacheContextScope ObjectCacheScope;
		AsyncCompilationHelpers::FinishCompilation(
			[&CompilableSkeletalMesh](int32 Index) -> AsyncCompilationHelpers::ICompilable& { return CompilableSkeletalMesh[Index]; },
			CompilableSkeletalMesh.Num(),
			LOCTEXT("SkeletalMeshes", "Skeletal Meshes"),
			LogSkeletalMesh,
			[this](AsyncCompilationHelpers::ICompilable* Object)
			{
				USkeletalMesh* SkeletalMesh = static_cast<FCompilableSkeletalMesh*>(Object)->SkeletalMesh.Get();
				PostCompilation(SkeletalMesh);
				RegisteredSkeletalMesh.Remove(SkeletalMesh);
			}
		);

		PostCompilation(PendingSkeletalMeshes);

		UpdateCompilationNotification();
	}
}

void FSkeletalMeshCompilingManager::FinishCompilationsForGame()
{
	
}

void FSkeletalMeshCompilingManager::FinishAllCompilation()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshCompilingManager::FinishAllCompilation)

	if (GetNumRemainingJobs())
	{
		TArray<USkeletalMesh*> PendingSkeletalMeshes;
		PendingSkeletalMeshes.Reserve(GetNumRemainingJobs());

		for (TWeakObjectPtr<USkeletalMesh>& SkeletalMesh : RegisteredSkeletalMesh)
		{
			if (SkeletalMesh.IsValid())
			{
				PendingSkeletalMeshes.Add(SkeletalMesh.Get());
			}
		}

		FinishCompilation(PendingSkeletalMeshes);
	}
}

void FSkeletalMeshCompilingManager::Reschedule()
{
	
}

void FSkeletalMeshCompilingManager::ProcessSkeletalMeshes(bool bLimitExecutionTime, int32 MinBatchSize)
{
	using namespace SkeletalMeshCompilingManagerImpl;
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshCompilingManager::ProcessSkeletalMeshes);
	const int32 NumRemainingMeshes = GetNumRemainingJobs();
	// Spread out the load over multiple frames but if too many meshes, convergence is more important than frame time
	const int32 MaxMeshUpdatesPerFrame = bLimitExecutionTime ? FMath::Max(64, NumRemainingMeshes / 10) : INT32_MAX;

	FObjectCacheContextScope ObjectCacheScope;
	if (NumRemainingMeshes && NumRemainingMeshes >= MinBatchSize)
	{
		TSet<USkeletalMesh*> SkeletalMeshesToProcess;
		for (TWeakObjectPtr<USkeletalMesh>& SkeletalMesh : RegisteredSkeletalMesh)
		{
			if (SkeletalMesh.IsValid())
			{
				SkeletalMeshesToProcess.Add(SkeletalMesh.Get());
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessFinishedSkeletalMeshes);

			const double TickStartTime = FPlatformTime::Seconds();

			TSet<TWeakObjectPtr<USkeletalMesh>> SkeletalMeshesToPostpone;
			TArray<USkeletalMesh*> ProcessedSkeletalMeshes;
			if (SkeletalMeshesToProcess.Num())
			{
				for (USkeletalMesh* SkeletalMesh : SkeletalMeshesToProcess)
				{
					const bool bHasMeshUpdateLeft = ProcessedSkeletalMeshes.Num() <= MaxMeshUpdatesPerFrame;
					if (bHasMeshUpdateLeft && SkeletalMesh->IsAsyncTaskComplete())
					{
						PostCompilation(SkeletalMesh);
						ProcessedSkeletalMeshes.Add(SkeletalMesh);
					}
					else
					{
						SkeletalMeshesToPostpone.Emplace(SkeletalMesh);
					}
				}
			}

			RegisteredSkeletalMesh = MoveTemp(SkeletalMeshesToPostpone);

			PostCompilation(ProcessedSkeletalMeshes);
		}
	}
}

void FSkeletalMeshCompilingManager::ProcessAsyncTasks(bool bLimitExecutionTime)
{
	FObjectCacheContextScope ObjectCacheScope;
	FinishCompilationsForGame();

	Reschedule();

	ProcessSkeletalMeshes(bLimitExecutionTime);

	UpdateCompilationNotification();
}

#endif // #if WITH_EDITOR

#undef LOCTEXT_NAMESPACE