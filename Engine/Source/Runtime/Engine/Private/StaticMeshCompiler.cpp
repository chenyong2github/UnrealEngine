// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshCompiler.h"
#include "AssetCompilingManager.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR

#include "ObjectCacheContext.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/QueuedThreadPoolWrapper.h"
#include "EngineModule.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/IQueuedWork.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Components/PrimitiveComponent.h"
#include "ContentStreaming.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Rendering/StaticLightingSystemInterface.h"
#include "AI/NavigationSystemBase.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "StaticMeshCompiler"

static TAutoConsoleVariable<int32> CVarAsyncStaticMeshCompilation(
	TEXT("Editor.AsyncStaticMeshCompilation"),
	0,
	TEXT("0 - Async static mesh compilation is disabled.\n")
	TEXT("1 - Async static mesh compilation is enabled.\n")
	TEXT("2 - Async static mesh compilation is enabled but on pause (for debugging).\n")
	TEXT("When enabled, static meshes will be replaced by placeholders until they are ready\n")
	TEXT("to reduce stalls on the game thread and improve overall editor performance."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsyncStaticMeshPlayInEditorMode(
	TEXT("Editor.AsyncStaticMeshPlayInEditorMode"),
	2,
	TEXT("0 - Wait until all static meshes are built before entering PIE. (Slowest but causes no visual or behavior artifacts.) \n")
	TEXT("1 - Wait until all static meshes affecting navigation and physics are built before entering PIE. (Some visuals might be missing during compilation.)\n")
	TEXT("2 - Wait only on static meshes affecting navigation and physics when they are close to the player. (Fastest while still preventing falling through the floor and going through objects.)\n"),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAsyncStaticMeshPlayInEditorDistance(
	TEXT("Editor.AsyncStaticMeshPlayInEditorDistance"),
	2.0f,
	TEXT("Scale applied to the player bounding sphere to determine how far away to force meshes compilation before resuming play.\n")
	TEXT("The effect can be seen during play session when Editor.AsyncStaticMeshPlayInEditorDebugDraw = 1.\n"),
	ECVF_Default);

static TAutoConsoleVariable<bool> CVarAsyncStaticMeshDebugDraw(
	TEXT("Editor.AsyncStaticMeshPlayInEditorDebugDraw"),
	false,
	TEXT("0 - Debug draw for async static mesh compilation is disabled.\n")
	TEXT("1 - Debug draw for async static mesh compilation is enabled.\n")
	TEXT("The collision sphere around the player is drawn in white and can be adjusted with Editor.AsyncStaticMeshPlayInEditorDistance\n")
	TEXT("Any static meshes affecting the physics that are still being compiled will have their bounding box drawn in green.\n")
	TEXT("Any static meshes that were waited on due to being too close to the player will have their bounding box drawn in red for a couple of seconds."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsyncStaticMeshMaxConcurrency(
	TEXT("Editor.AsyncStaticMeshMaxConcurrency"),
	-1,
	TEXT("Set the maximum number of concurrent static mesh compilation, -1 for unlimited."),
	ECVF_Default);

static FAutoConsoleCommand CVarAsyncStaticMeshCompilationFinishAll(
	TEXT("Editor.AsyncStaticMeshCompilationFinishAll"),
	TEXT("Finish all static mesh compilations"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FStaticMeshCompilingManager::Get().FinishAllCompilation();
	})
);

static TAutoConsoleVariable<int32> CVarAsyncStaticMeshCompilationResume(
	TEXT("Editor.AsyncStaticMeshCompilationResume"),
	0,
	TEXT("Number of queued work to resume while paused."),
	ECVF_Default);

namespace StaticMeshCompilingManagerImpl
{
	static void EnsureInitializedCVars()
	{
		static bool bIsInitialized = false;

		if (!bIsInitialized)
		{
			bIsInitialized = true;
			GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().AddLambda(
				[](FName Name)
				{
					if (Name == TEXT("bEnableAsyncStaticMeshCompilation"))
					{
						CVarAsyncStaticMeshCompilation->Set(GetDefault<UEditorExperimentalSettings>()->bEnableAsyncStaticMeshCompilation ? 1 : 0, ECVF_SetByProjectSetting);
					}
				}
			);

			CVarAsyncStaticMeshCompilation->Set(GetDefault<UEditorExperimentalSettings>()->bEnableAsyncStaticMeshCompilation ? 1 : 0, ECVF_SetByProjectSetting);

			FString Value;
			if (FParse::Value(FCommandLine::Get(), TEXT("-asyncstaticmeshcompilation="), Value))
			{
				int32 AsyncStaticMeshCompilationValue = 0;
				if (Value == TEXT("1") || Value == TEXT("on"))
				{
					AsyncStaticMeshCompilationValue = 1;
				}

				if (Value == TEXT("2") || Value == TEXT("paused"))
				{
					AsyncStaticMeshCompilationValue = 2;
				}

				CVarAsyncStaticMeshCompilation->Set(AsyncStaticMeshCompilationValue, ECVF_SetByCommandline);
			}

			int32 MaxConcurrency = -1;
			if (FParse::Value(FCommandLine::Get(), TEXT("-asyncstaticmeshmaxconcurrency="), MaxConcurrency))
			{
				CVarAsyncStaticMeshMaxConcurrency->Set(MaxConcurrency, ECVF_SetByCommandline);
			}
		}
	}
}

EQueuedWorkPriority FStaticMeshCompilingManager::GetBasePriority(UStaticMesh* InStaticMesh) const
{
	return EQueuedWorkPriority::Low;
}

FQueuedThreadPool* FStaticMeshCompilingManager::GetThreadPool() const
{
	static FQueuedThreadPoolDynamicWrapper* GStaticMeshThreadPool = nullptr;
	if (GStaticMeshThreadPool == nullptr)
	{
		StaticMeshCompilingManagerImpl::EnsureInitializedCVars();

		const int32 MaxConcurrency = CVarAsyncStaticMeshMaxConcurrency.GetValueOnAnyThread();

		// Static meshes will be scheduled on the asset thread pool, where concurrency limits might by dynamically adjusted depending on memory constraints.
		GStaticMeshThreadPool = new FQueuedThreadPoolDynamicWrapper(FAssetCompilingManager::Get().GetThreadPool(), MaxConcurrency, [](EQueuedWorkPriority) { return EQueuedWorkPriority::Low; });

		CVarAsyncStaticMeshCompilation->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					if (Variable->GetInt() == 2)
					{
						GStaticMeshThreadPool->Pause();
					}
					else
					{
						GStaticMeshThreadPool->Resume();
					}
				}
				)
			);

		CVarAsyncStaticMeshCompilationResume->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					if (Variable->GetInt() > 0)
					{
						GStaticMeshThreadPool->Resume(Variable->GetInt());
					}
				}
				)
			);

		CVarAsyncStaticMeshMaxConcurrency->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					GStaticMeshThreadPool->SetMaxConcurrency(Variable->GetInt());
				}
				)
			);

		if (CVarAsyncStaticMeshCompilation->GetInt() == 2)
		{
			GStaticMeshThreadPool->Pause();
		}
	}

	return GStaticMeshThreadPool;
}

void FStaticMeshCompilingManager::Shutdown()
{
	bHasShutdown = true;
	if (GetNumRemainingMeshes())
	{
		check(IsInGameThread());
		TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::Shutdown)

		if (GetNumRemainingMeshes())
		{
			TArray<UStaticMesh*> PendingStaticMeshes;
			PendingStaticMeshes.Reserve(GetNumRemainingMeshes());

			for (TWeakObjectPtr<UStaticMesh>& WeakStaticMesh : RegisteredStaticMesh)
			{
				if (WeakStaticMesh.IsValid())
				{
					UStaticMesh* StaticMesh = WeakStaticMesh.Get();
					if (!StaticMesh->IsAsyncTaskComplete())
					{
						if (StaticMesh->AsyncTask->Cancel())
						{
							StaticMesh->AsyncTask.Reset();
						}
					}

					if (StaticMesh->AsyncTask)
					{
						PendingStaticMeshes.Add(StaticMesh);
					}
				}
			}

			FinishCompilation(PendingStaticMeshes);
		}
	}
}

bool FStaticMeshCompilingManager::IsAsyncStaticMeshCompilationEnabled() const
{
	if (bHasShutdown)
	{
		return false;
	}

	StaticMeshCompilingManagerImpl::EnsureInitializedCVars();

	return CVarAsyncStaticMeshCompilation.GetValueOnAnyThread() != 0;
}

void FStaticMeshCompilingManager::UpdateCompilationNotification()
{
	check(IsInGameThread());
	static TWeakPtr<SNotificationItem> StaticMeshCompilationPtr;

	TSharedPtr<SNotificationItem> NotificationItem = StaticMeshCompilationPtr.Pin();

	const int32 NumRemainingCompilations = GetNumRemainingMeshes();
	if (NumRemainingCompilations == 0)
	{
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetText(NSLOCTEXT("StaticMeshBuild", "StaticMeshBuildFinished", "Finished preparing Static Meshes!"));
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
			NotificationItem->ExpireAndFadeout();

			StaticMeshCompilationPtr.Reset();
		}
	}
	else
	{
		if (!NotificationItem.IsValid())
		{
			FNotificationInfo Info(NSLOCTEXT("StaticMeshBuild", "StaticMeshBuildInProgress", "Preparing Static Meshes"));
			Info.bFireAndForget = false;

			// Setting fade out and expire time to 0 as the expire message is currently very obnoxious
			Info.FadeOutDuration = 0.0f;
			Info.ExpireDuration = 0.0f;

			NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
			StaticMeshCompilationPtr = NotificationItem;
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("BuildTasks"), FText::AsNumber(NumRemainingCompilations));
		FText ProgressMessage = FText::Format(NSLOCTEXT("StaticMeshBuild", "StaticMeshBuildInProgressFormat", "Preparing Static Meshes ({BuildTasks})"), Args);

		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		NotificationItem->SetVisibility(EVisibility::HitTestInvisible);
		NotificationItem->SetText(ProgressMessage);
	}
}

void FStaticMeshCompilingManager::PostStaticMeshesCompilation(const TSet<UStaticMesh*>& InStaticMeshes)
{
	if (InStaticMeshes.Num())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(OnAssetPostCompileEvent);

		TArray<FAssetCompileData> AssetsData;
		AssetsData.Reserve(InStaticMeshes.Num());

		for (UStaticMesh* StaticMesh : InStaticMeshes)
		{
			AssetsData.Emplace(StaticMesh);
		}

		FAssetCompilingManager::Get().OnAssetPostCompileEvent().Broadcast(AssetsData);
	}
}

void FStaticMeshCompilingManager::FinishStaticMeshCompilation(UStaticMesh* StaticMesh)
{
	using namespace StaticMeshCompilingManagerImpl;
	
	// If AsyncTask is null here, the task got canceled so we don't need to do anything
	if (StaticMesh->AsyncTask)
	{
		check(IsInGameThread());
		TRACE_CPUPROFILER_EVENT_SCOPE(FinishStaticMeshCompilation);

		UE_LOG(LogStaticMesh, Verbose, TEXT("Refreshing static mesh %s because it is ready"), *StaticMesh->GetName());

		FObjectCacheContextScope ObjectCacheScope;

		// The scope is important here to destroy the FStaticMeshAsyncBuildScope before broadcasting events
		{
			// Acquire the async task locally to protect against re-entrance
			TUniquePtr<FStaticMeshAsyncBuildTask> LocalAsyncTask = MoveTemp(StaticMesh->AsyncTask);
			LocalAsyncTask->EnsureCompletion();

			FStaticMeshAsyncBuildScope AsyncBuildScope(StaticMesh);

			if (LocalAsyncTask->GetTask().PostLoadContext.IsValid())
			{
				StaticMesh->FinishPostLoadInternal(*LocalAsyncTask->GetTask().PostLoadContext);

				LocalAsyncTask->GetTask().PostLoadContext.Reset();
			}

			if (LocalAsyncTask->GetTask().BuildContext.IsValid())
			{
				StaticMesh->FinishBuildInternal(
					ObjectCacheScope.GetContext().GetStaticMeshComponents(StaticMesh),
					LocalAsyncTask->GetTask().BuildContext->bHasRenderDataChanged,
					LocalAsyncTask->GetTask().BuildContext->bShouldComputeExtendedBounds
				);

				LocalAsyncTask->GetTask().BuildContext.Reset();
			}
		}

		for (UStaticMeshComponent* Component : ObjectCacheScope.GetContext().GetStaticMeshComponents(StaticMesh))
		{
			Component->PostStaticMeshCompilation();
		}

		// Generate an empty property changed event, to force the asset registry tag
		// to be refreshed now that RenderData is available.
		FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(StaticMesh, EmptyPropertyChangedEvent);
	}
}

bool FStaticMeshCompilingManager::IsAsyncCompilationAllowed(UStaticMesh* StaticMesh) const
{
	return IsAsyncStaticMeshCompilationEnabled();
}

FStaticMeshCompilingManager& FStaticMeshCompilingManager::Get()
{
	static FStaticMeshCompilingManager Singleton;
	return Singleton;
}

int32 FStaticMeshCompilingManager::GetNumRemainingMeshes() const
{
	return RegisteredStaticMesh.Num();
}

void FStaticMeshCompilingManager::AddStaticMeshes(const TArray<UStaticMesh*>& InStaticMeshes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::AddStaticMeshes)
	check(IsInGameThread());

	// Wait until we gather enough mesh to process
	// to amortize the cost of scanning components
	//ProcessStaticMeshes(32 /* MinBatchSize */);

	for (UStaticMesh* StaticMesh : InStaticMeshes)
	{
		check(StaticMesh->AsyncTask != nullptr);
		RegisteredStaticMesh.Emplace(StaticMesh);
	}
}

void FStaticMeshCompilingManager::FinishCompilation(const TArray<UStaticMesh*>& InStaticMeshes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::FinishCompilation);

	using namespace StaticMeshCompilingManagerImpl;
	check(IsInGameThread());

	FObjectCacheContextScope ObjectCacheScope;
	TSet<UStaticMesh*> PendingStaticMeshes;
	PendingStaticMeshes.Reserve(InStaticMeshes.Num());

	int32 StaticMeshIndex = 0;
	for (UStaticMesh* StaticMesh : InStaticMeshes)
	{
		if (RegisteredStaticMesh.Contains(StaticMesh))
		{
			PendingStaticMeshes.Add(StaticMesh);
		}
	}

	if (PendingStaticMeshes.Num())
	{
		FScopedSlowTask SlowTask((float)PendingStaticMeshes.Num(), LOCTEXT("FinishStaticMeshCompilation", "Waiting on static meshes preparation"), true);
		SlowTask.MakeDialogDelayed(1.0f, false /*bShowCancelButton*/, true /*bAllowInPIE*/);

		struct FStaticMeshTask : public IQueuedWork
		{
			TStrongObjectPtr<UStaticMesh> StaticMesh;
			FEvent* Event;
			FStaticMeshTask() { Event = FPlatformProcess::GetSynchEventFromPool(true); }
			~FStaticMeshTask() { FPlatformProcess::ReturnSynchEventToPool(Event); }
			void DoThreadedWork() override 
			{
				if (StaticMesh->AsyncTask) 
				{ 
					StaticMesh->AsyncTask->EnsureCompletion();
				}
				Event->Trigger(); 
			}

			void Abandon() override { }
		};

		// Perform forced compilation on as many thread as possible in high priority since the game-thread is waiting
		TArray<FStaticMeshTask> PendingTasks;
		PendingTasks.SetNum(PendingStaticMeshes.Num());
		
		int32 PendingTaskIndex = 0;
		for (UStaticMesh* StaticMesh : PendingStaticMeshes)
		{
			PendingTasks[PendingTaskIndex].StaticMesh.Reset(StaticMesh);
			GThreadPool->AddQueuedWork(&PendingTasks[PendingTaskIndex], EQueuedWorkPriority::High);
			PendingTaskIndex++;
		}

		auto UpdateProgress =
			[&SlowTask](float Progress, int32 Done, int32 Total, const FString& CurrentObjectsName)
			{
				return SlowTask.EnterProgressFrame(Progress, FText::FromString(FString::Printf(TEXT("Waiting for static meshes to be ready %d/%d (%s) ..."), Done, Total, *CurrentObjectsName)));
			};

		for (FStaticMeshTask& PendingTask : PendingTasks)
		{
			UStaticMesh* StaticMesh = PendingTask.StaticMesh.Get();
			const FString StaticMeshName = StaticMesh->GetName();
			// Be nice with the game thread and tick the progress at 60 fps even when no progress is being made...
			while (!PendingTask.Event->Wait(16))
			{
				UpdateProgress(0.0f, StaticMeshIndex, InStaticMeshes.Num(), StaticMeshName);
			}
			UE_LOG(LogStaticMesh, Display, TEXT("Waiting for static meshes to be ready %d/%d (%s) ..."), StaticMeshIndex, InStaticMeshes.Num(), *StaticMeshName);
			UpdateProgress(1.f, StaticMeshIndex++, InStaticMeshes.Num(), StaticMeshName);

			FinishStaticMeshCompilation(StaticMesh);

			RegisteredStaticMesh.Remove(StaticMesh);
		}

		PostStaticMeshesCompilation(PendingStaticMeshes);
	}
}

void FStaticMeshCompilingManager::FinishCompilationsForGame()
{
	if (GetNumRemainingMeshes())
	{
		FObjectCacheContextScope ObjectCacheScope;
		// Supports both Game and PIE mode
		const bool bIsPlaying = 
			(GWorld && !GWorld->IsEditorWorld()) ||
			(GEditor && GEditor->PlayWorld && !GEditor->IsSimulateInEditorInProgress());

		if (bIsPlaying)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::FinishCompilationsForGame);

			const int32 PlayInEditorMode = CVarAsyncStaticMeshPlayInEditorMode.GetValueOnGameThread();
			
			const bool bShowDebugDraw = CVarAsyncStaticMeshDebugDraw.GetValueOnGameThread();

			TSet<const UWorld*> PIEWorlds;
			TMultiMap<const UWorld*, FBoxSphereBounds> WorldActors;
			
			float RadiusScale = CVarAsyncStaticMeshPlayInEditorDistance.GetValueOnGameThread();
			for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
			{
				if (WorldContext.WorldType == EWorldType::PIE || WorldContext.WorldType == EWorldType::Game)
				{
					UWorld* World = WorldContext.World();
					PIEWorlds.Add(World);

					// Extract all pawns of the world to support player/bots local and remote.
					if (PlayInEditorMode == 2)
					{
						for (TActorIterator<APawn> It(World); It; ++It)
						{
							const APawn* Pawn = *It;
							if (Pawn)
							{
								FBoxSphereBounds ActorBounds;
								Pawn->GetActorBounds(true, ActorBounds.Origin, ActorBounds.BoxExtent);
								ActorBounds.SphereRadius = ActorBounds.BoxExtent.GetMax() * RadiusScale;
								WorldActors.Emplace(World, ActorBounds);

								if (bShowDebugDraw)
								{
									DrawDebugSphere(World, ActorBounds.Origin, ActorBounds.SphereRadius, 10, FColor::White);
								}
							}
						}
					}
				}
			}
			
			TSet<UStaticMesh*> StaticMeshToCompile;
			TArray<FBoxSphereBounds, TInlineAllocator<16>> ActorsBounds;
			for (const UStaticMeshComponent* Component : ObjectCacheScope.GetContext().GetStaticMeshComponents())
			{
				if (Component->IsRegistered() &&
					PIEWorlds.Contains(Component->GetWorld()) &&
					RegisteredStaticMesh.Contains(Component->GetStaticMesh()) &&
					(PlayInEditorMode == 0 || Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision || Component->IsNavigationRelevant()))
				{
					const FBoxSphereBounds ComponentBounds = Component->Bounds.GetBox();
					const UWorld* ComponentWorld = Component->GetWorld();

					if (PlayInEditorMode == 2)
					{
						ActorsBounds.Reset();
						WorldActors.MultiFind(ComponentWorld, ActorsBounds);
						
						bool bStaticMeshComponentCollided = false;
						if (ActorsBounds.Num())
						{
							for (const FBoxSphereBounds& ActorBounds : ActorsBounds)
							{
								if (FMath::SphereAABBIntersection(ActorBounds.Origin, ActorBounds.SphereRadius * ActorBounds.SphereRadius, ComponentBounds.GetBox()))
								{
									if (bShowDebugDraw)
									{
										DrawDebugBox(ComponentWorld, ComponentBounds.Origin, ComponentBounds.BoxExtent, FColor::Red, false, 10.0f);
									}
								
									bool bIsAlreadyInSet = false;
									StaticMeshToCompile.Add(Component->GetStaticMesh(), &bIsAlreadyInSet);
									if (!bIsAlreadyInSet)
									{
										UE_LOG(
											LogStaticMesh,
											Display,
											TEXT("Waiting on static mesh %s being ready because it affects collision/navigation and is near a player/bot"),
											*Component->GetStaticMesh()->GetFullName()
										);
									}
									bStaticMeshComponentCollided = true;
									break;
								}
							}
						}

						if (bShowDebugDraw && !bStaticMeshComponentCollided)
						{
							DrawDebugBox(ComponentWorld, ComponentBounds.Origin, ComponentBounds.BoxExtent, FColor::Green);
						}
					}
					else 
					{
						bool bIsAlreadyInSet = false;
						StaticMeshToCompile.Add(Component->GetStaticMesh(), &bIsAlreadyInSet);
						if (!bIsAlreadyInSet)
						{
							if (PlayInEditorMode == 0)
							{
								UE_LOG(LogStaticMesh, Display, TEXT("Waiting on static mesh %s being ready before playing"), *Component->GetStaticMesh()->GetFullName());
							}
							else
							{
								UE_LOG(LogStaticMesh, Display, TEXT("Waiting on static mesh %s being ready because it affects collision/navigation"), *Component->GetStaticMesh()->GetFullName());
							}
						}
					}
				}
			}

			if (StaticMeshToCompile.Num())
			{
				FinishCompilation(StaticMeshToCompile.Array());
			}
		}
	}
}

void FStaticMeshCompilingManager::FinishAllCompilation()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::FinishAllCompilation)

	if (GetNumRemainingMeshes())
	{
		TArray<UStaticMesh*> PendingStaticMeshes;
		PendingStaticMeshes.Reserve(GetNumRemainingMeshes());

		for (TWeakObjectPtr<UStaticMesh>& StaticMesh : RegisteredStaticMesh)
		{
			if (StaticMesh.IsValid())
			{
				PendingStaticMeshes.Add(StaticMesh.Get());
			}
		}

		FinishCompilation(PendingStaticMeshes);
	}
}

void FStaticMeshCompilingManager::Reschedule()
{
	using namespace StaticMeshCompilingManagerImpl;
	if (RegisteredStaticMesh.Num() > 1)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::Reschedule);

		FObjectCacheContextScope ObjectCacheScope;
		TSet<UStaticMesh*> StaticMeshesToProcess;
		for (TWeakObjectPtr<UStaticMesh>& StaticMesh : RegisteredStaticMesh)
		{
			if (StaticMesh.IsValid())
			{
				StaticMeshesToProcess.Add(StaticMesh.Get());
			}
		}

		TMap<UStaticMesh*, float> DistanceToEditingViewport;
		{
			if (StaticMeshesToProcess.Num() > 1)
			{
				const int32 NumViews = IStreamingManager::Get().GetNumViews();
			
				const FStreamingViewInfo* BestViewInfo = nullptr;
				for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
				{
					const FStreamingViewInfo& ViewInfo = IStreamingManager::Get().GetViewInformation(ViewIndex);
					if (BestViewInfo == nullptr || ViewInfo.BoostFactor > BestViewInfo->BoostFactor)
					{
						BestViewInfo = &ViewInfo;
					}
				}

				const FVector Location = BestViewInfo ? BestViewInfo->ViewOrigin : FVector(0.0f, 0.0f, 0.0f);
				{
					for (const UStaticMeshComponent* StaticMeshComponent : ObjectCacheScope.GetContext().GetStaticMeshComponents())
					{
						if (StaticMeshComponent->IsRegistered() && StaticMeshesToProcess.Contains(StaticMeshComponent->GetStaticMesh()))
						{
							FVector ComponentLocation = StaticMeshComponent->GetComponentLocation();
							float& Dist = DistanceToEditingViewport.FindOrAdd(StaticMeshComponent->GetStaticMesh(), FLT_MAX);
							float ComponentDist = Location.Dist(ComponentLocation, Location);
							if (ComponentDist < Dist)
							{
								Dist = ComponentDist;
							}
						}
					}
				}
			}

			if (DistanceToEditingViewport.Num())
			{
				StaticMeshesToProcess.Sort(
					[&DistanceToEditingViewport](const UStaticMesh& Lhs, const UStaticMesh& Rhs)
					{
						const float* ResultA = DistanceToEditingViewport.Find(&Lhs);
						const float* ResultB = DistanceToEditingViewport.Find(&Rhs);

						const float FinalResultA = ResultA ? *ResultA : FLT_MAX;
						const float FinalResultB = ResultB ? *ResultB : FLT_MAX;
						return FinalResultA < FinalResultB;
					}
				);
			}

			if (DistanceToEditingViewport.Num())
			{
				FQueuedThreadPoolDynamicWrapper* QueuedThreadPool = (FQueuedThreadPoolDynamicWrapper*)GetThreadPool();
				QueuedThreadPool->Sort(
					[&DistanceToEditingViewport](const IQueuedWork* Lhs, const IQueuedWork* Rhs)
					{
						const FStaticMeshAsyncBuildTask* TaskA = (const FStaticMeshAsyncBuildTask*)Lhs;
						const FStaticMeshAsyncBuildTask* TaskB = (const FStaticMeshAsyncBuildTask*)Rhs;

						const float* ResultA = DistanceToEditingViewport.Find(TaskA->StaticMesh);
						const float* ResultB = DistanceToEditingViewport.Find(TaskB->StaticMesh);

						const float FinalResultA = ResultA ? *ResultA : FLT_MAX;
						const float FinalResultB = ResultB ? *ResultB : FLT_MAX;
						return FinalResultA < FinalResultB;
					}
				);
			}
		}
	}
}

void FStaticMeshCompilingManager::ProcessStaticMeshes(bool bLimitExecutionTime, int32 MinBatchSize)
{
	using namespace StaticMeshCompilingManagerImpl;
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshCompilingManager::ProcessStaticMeshes);
	const int32 NumRemainingMeshes = GetNumRemainingMeshes();
	// Spread out the load over multiple frames but if too many meshes, convergence is more important than frame time
	const int32 MaxMeshUpdatesPerFrame = bLimitExecutionTime ? FMath::Max(64, NumRemainingMeshes / 10) : INT32_MAX;

	FObjectCacheContextScope ObjectCacheScope;
	if (NumRemainingMeshes && NumRemainingMeshes >= MinBatchSize)
	{
		TSet<UStaticMesh*> StaticMeshesToProcess;
		for (TWeakObjectPtr<UStaticMesh>& StaticMesh : RegisteredStaticMesh)
		{
			if (StaticMesh.IsValid())
			{
				StaticMeshesToProcess.Add(StaticMesh.Get());
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessFinishedStaticMeshes);

			const double TickStartTime = FPlatformTime::Seconds();

			TSet<TWeakObjectPtr<UStaticMesh>> StaticMeshesToPostpone;
			TSet<UStaticMesh*> ProcessedStaticMeshes;
			if (StaticMeshesToProcess.Num())
			{
				for (UStaticMesh* StaticMesh : StaticMeshesToProcess)
				{
					const bool bHasMeshUpdateLeft = ProcessedStaticMeshes.Num() <= MaxMeshUpdatesPerFrame;
					if (bHasMeshUpdateLeft && StaticMesh->IsAsyncTaskComplete())
					{
						FinishStaticMeshCompilation(StaticMesh);
						ProcessedStaticMeshes.Add(StaticMesh);
					}
					else
					{
						StaticMeshesToPostpone.Emplace(StaticMesh);
					}
				}
			}

			RegisteredStaticMesh = MoveTemp(StaticMeshesToPostpone);

			PostStaticMeshesCompilation(ProcessedStaticMeshes);
		}
	}
}

void FStaticMeshCompilingManager::ProcessAsyncTasks(bool bLimitExecutionTime)
{
	FObjectCacheContextScope ObjectCacheScope;
	FinishCompilationsForGame();

	Reschedule();

	ProcessStaticMeshes(bLimitExecutionTime);

	UpdateCompilationNotification();
}

#endif // #if WITH_EDITOR

#undef LOCTEXT_NAMESPACE