// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureCompiler.h"

#if WITH_EDITOR

#include "Engine/Texture.h"
#include "ObjectCacheContext.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/QueuedThreadPoolWrapper.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "TextureDerivedDataTask.h"
#include "Misc/IQueuedWork.h"
#include "Components/PrimitiveComponent.h"
#include "AssetCompilingManager.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "TextureCompiler"

static TAutoConsoleVariable<int32> CVarAsyncTextureCompilation(
	TEXT("Editor.AsyncTextureCompilation"),
	0,
	TEXT("0 - Async texture compilation is disabled.\n")
	TEXT("1 - Async texture compilation is enabled.\n")
	TEXT("2 - Async texture compilation is enabled but on pause (for debugging).\n")
	TEXT("When enabled, textures will be replaced by placeholders until they are ready\n")
	TEXT("to reduce stalls on the game thread and improve overall editor performance."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsyncTextureCompilationMaxConcurrency(
	TEXT("Editor.AsyncTextureCompilationMaxConcurrency"),
	-1,
	TEXT("Set the maximum number of concurrent texture compilation, -1 for unlimited."),
	ECVF_Default);

static FAutoConsoleCommand CVarAsyncTextureCompilationFinishAll(
	TEXT("Editor.AsyncTextureCompilationFinishAll"),
	TEXT("Finish all texture compilations"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FTextureCompilingManager::Get().FinishAllCompilation();
	})
);

static TAutoConsoleVariable<int32> CVarAsyncTextureCompilationResume(
	TEXT("Editor.AsyncTextureCompilationResume"),
	0,
	TEXT("Number of queued work to resume while paused."),
	ECVF_Default);

namespace TextureCompilingManagerImpl
{
	static FString GetLODGroupName(UTexture* Texture)
	{
		return StaticEnum<TextureGroup>()->GetMetaData(TEXT("DisplayName"), Texture->LODGroup);
	}

	static EQueuedWorkPriority GetBasePriority(UTexture* InTexture)
	{
		switch (InTexture->LODGroup)
		{
		case TEXTUREGROUP_UI:
			return EQueuedWorkPriority::High;
		case TEXTUREGROUP_Terrain_Heightmap:
			return EQueuedWorkPriority::Normal;
		default:
			return EQueuedWorkPriority::Lowest;
		}
	}

	static EQueuedWorkPriority GetBoostPriority(UTexture* InTexture)
	{
		return (EQueuedWorkPriority)(FMath::Max((uint8)1, (uint8)GetBasePriority(InTexture)) - 1);
	}

	static const TCHAR* GetPriorityName(EQueuedWorkPriority Priority)
	{
		switch (Priority)
		{
			case EQueuedWorkPriority::Highest:
				return TEXT("Highest");
			case EQueuedWorkPriority::High:
				return TEXT("High");
			case EQueuedWorkPriority::Normal:
				return TEXT("Normal");
			case EQueuedWorkPriority::Low:
				return TEXT("Low");
			case EQueuedWorkPriority::Lowest:
				return TEXT("Lowest");
			default:
				return TEXT("Unknown");
		}
	}

	static void EnsureInitializedCVars()
	{
		static bool bIsInitialized = false;

		if (!bIsInitialized)
		{
			bIsInitialized = true;
			GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().AddLambda(
				[](FName Name)
				{
					if (Name == TEXT("bEnableAsyncTextureCompilation"))
					{
						CVarAsyncTextureCompilation->Set(GetDefault<UEditorExperimentalSettings>()->bEnableAsyncTextureCompilation ? 1 : 0, ECVF_SetByProjectSetting);
					}
				}
			);

			CVarAsyncTextureCompilation->Set(GetDefault<UEditorExperimentalSettings>()->bEnableAsyncTextureCompilation ? 1 : 0, ECVF_SetByProjectSetting);

			FString Value;
			if (FParse::Value(FCommandLine::Get(), TEXT("-asynctexturecompilation="), Value))
			{
				int32 AsyncTextureCompilationValue = 0;
				if (Value == TEXT("1") || Value == TEXT("on"))
				{
					AsyncTextureCompilationValue = 1;
				}

				if (Value == TEXT("2") || Value == TEXT("paused"))
				{
					AsyncTextureCompilationValue = 2;
				}

				CVarAsyncTextureCompilation->Set(AsyncTextureCompilationValue, ECVF_SetByCommandline);
			}

			int32 MaxConcurrency = -1;
			if (FParse::Value(FCommandLine::Get(), TEXT("-asynctexturecompilationmaxconcurrency="), MaxConcurrency))
			{
				CVarAsyncTextureCompilationMaxConcurrency->Set(MaxConcurrency, ECVF_SetByCommandline);
			}
		}
	}
}

EQueuedWorkPriority FTextureCompilingManager::GetBasePriority(UTexture* InTexture) const
{
	return TextureCompilingManagerImpl::GetBasePriority(InTexture);
}

void FTextureCompilingManager::Shutdown()
{
	bHasShutdown = true;
	if (GetNumRemainingTextures())
	{
		TArray<UTexture*> PendingTextures;
		PendingTextures.Reserve(GetNumRemainingTextures());

		for (TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
		{
			for (TWeakObjectPtr<UTexture>& WeakTexture : Bucket)
			{
				if (WeakTexture.IsValid())
				{
					UTexture* Texture = WeakTexture.Get();
					
					if (!Texture->TryCancelCachePlatformData())
					{
						PendingTextures.Add(Texture);
					}
				}
			}
		}

		// Wait on textures already in progress we couldn't cancel
		FinishCompilation(PendingTextures);
	}
}

FQueuedThreadPool* FTextureCompilingManager::GetThreadPool() const
{
	static FQueuedThreadPoolWrapper* GTextureThreadPool = nullptr;
	if (GTextureThreadPool == nullptr)
	{
		TextureCompilingManagerImpl::EnsureInitializedCVars();

		const auto TexturePriorityMapper = [](EQueuedWorkPriority TexturePriority) { return FMath::Max(TexturePriority, EQueuedWorkPriority::Low); };
		const int32 MaxConcurrency = CVarAsyncTextureCompilationMaxConcurrency.GetValueOnAnyThread();

		// Textures will be scheduled on the asset thread pool, where concurrency limits might by dynamically adjusted depending on memory constraints.
		GTextureThreadPool = new FQueuedThreadPoolWrapper(FAssetCompilingManager::Get().GetThreadPool(), MaxConcurrency, TexturePriorityMapper);

		CVarAsyncTextureCompilation->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					if (Variable->GetInt() == 2)
					{
						GTextureThreadPool->Pause();
					}
					else
					{
						GTextureThreadPool->Resume();
					}
				}
				)
			);

		CVarAsyncTextureCompilationResume->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					if (Variable->GetInt() > 0)
					{
						GTextureThreadPool->Resume(Variable->GetInt());
					}
				}
				)
			);

		CVarAsyncTextureCompilationMaxConcurrency->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda(
				[](IConsoleVariable* Variable)
				{
					GTextureThreadPool->SetMaxConcurrency(Variable->GetInt());
				}
				)
			);

		if (CVarAsyncTextureCompilation->GetInt() == 2)
		{
			GTextureThreadPool->Pause();
		}
	}

	return GTextureThreadPool;
}

bool FTextureCompilingManager::IsAsyncTextureCompilationEnabled() const
{
	if (bHasShutdown)
	{
		return false;
	}

	TextureCompilingManagerImpl::EnsureInitializedCVars();

	return CVarAsyncTextureCompilation.GetValueOnAnyThread() != 0;
}

void FTextureCompilingManager::UpdateCompilationNotification()
{
	check(IsInGameThread());
	static TWeakPtr<SNotificationItem> TextureCompilationPtr;

	TSharedPtr<SNotificationItem> NotificationItem = TextureCompilationPtr.Pin();

	const int32 NumRemainingCompilations = GetNumRemainingTextures();
	if (NumRemainingCompilations == 0)
	{
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetText(NSLOCTEXT("TextureBuild", "TextureBuildFinished", "Textures are ready!"));
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
			NotificationItem->ExpireAndFadeout();

			TextureCompilationPtr.Reset();
		}
	}
	else
	{
		if (!NotificationItem.IsValid())
		{
			FNotificationInfo Info(NSLOCTEXT("TextureBuild", "TextureBuildInProgress", "Preparing Textures"));
			Info.bFireAndForget = false;

			// Setting fade out and expire time to 0 as the expire message is currently very obnoxious
			Info.FadeOutDuration = 0.0f;
			Info.ExpireDuration = 0.0f;

			NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
			TextureCompilationPtr = NotificationItem;
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("BuildTasks"), FText::AsNumber(NumRemainingCompilations));
		FText ProgressMessage = FText::Format(NSLOCTEXT("TextureBuild", "TextureBuildInProgressFormat", "Preparing Textures ({BuildTasks})"), Args);

		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		NotificationItem->SetVisibility(EVisibility::HitTestInvisible);
		NotificationItem->SetText(ProgressMessage);
	}
}

void FTextureCompilingManager::FinishTextureCompilation(UTexture* Texture)
{
	using namespace TextureCompilingManagerImpl;

	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(FinishTextureCompilation);

	UE_LOG(LogTexture, Verbose, TEXT("Refreshing texture %s because it is ready"), *Texture->GetName());

	Texture->FinishCachePlatformData();
	Texture->UpdateResource();

	// Generate an empty property changed event, to force the asset registry tag
	// to be refreshed now that pixel format and alpha channels are available.
	FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Texture, EmptyPropertyChangedEvent);
}

bool FTextureCompilingManager::IsAsyncCompilationAllowed(UTexture* Texture) const
{
	return IsAsyncTextureCompilationEnabled();
}

FTextureCompilingManager& FTextureCompilingManager::Get()
{
	static FTextureCompilingManager Singleton;
	return Singleton;
}

int32 FTextureCompilingManager::GetNumRemainingTextures() const
{
	int32 Num = 0;
	for (const TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
	{
		Num += Bucket.Num();
	}

	return Num;
}

void FTextureCompilingManager::AddTextures(const TArray<UTexture*>& InTextures)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCompilingManager::AddTextures)
	check(IsInGameThread());

	// Register new textures after ProcessTextures to avoid
	// potential reentrant calls to CreateResource on the
	// textures being added. This would cause multiple
	// TextureResource to be created and assigned to the same Owner
	// which would obviously be bad and causing leaks including
	// in the RHI.
	for (UTexture* Texture : InTextures)
	{
		int32 TexturePriority = 2;
		switch (Texture->LODGroup)
		{
			case TEXTUREGROUP_UI:
				TexturePriority = 0;
			break;
			case TEXTUREGROUP_Terrain_Heightmap:
				TexturePriority = 1;
			break;
		}

		if (RegisteredTextureBuckets.Num() <= TexturePriority)
		{
			RegisteredTextureBuckets.SetNum(TexturePriority + 1);
		}
		RegisteredTextureBuckets[TexturePriority].Emplace(Texture);
	}
}

void FTextureCompilingManager::FinishCompilation(const TArray<UTexture*>& InTextures)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCompilingManager::FinishCompilation);

	using namespace TextureCompilingManagerImpl;
	check(IsInGameThread());

	TSet<UTexture*> PendingTextures;
	PendingTextures.Reserve(InTextures.Num());

	int32 TextureIndex = 0;
	for (UTexture* Texture : InTextures)
	{
		for (TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
		{
			if (Bucket.Contains(Texture))
			{
				PendingTextures.Add(Texture);
			}
		}
	}

	if (PendingTextures.Num())
	{
		FScopedSlowTask SlowTask((float)PendingTextures.Num(), LOCTEXT("FinishTextureCompilation", "Waiting on texture preparation"), true);
		SlowTask.MakeDialogDelayed(1.0f);

		struct FTextureTask : public IQueuedWork
		{
			TStrongObjectPtr<UTexture> Texture;
			FEvent* Event;
			FTextureTask() { Event = FPlatformProcess::GetSynchEventFromPool(true); }
			~FTextureTask() { FPlatformProcess::ReturnSynchEventToPool(Event); }
			void DoThreadedWork() override { FOptionalTaskTagScope Scope(ETaskTag::EParallelGameThread); Texture->FinishCachePlatformData(); Event->Trigger(); };
			void Abandon() override { }
		};

		// Perform forced compilation on as many thread as possible in high priority since the game-thread is waiting
		TArray<FTextureTask> PendingTasks;
		PendingTasks.SetNum(PendingTextures.Num());
		
		int32 PendingTaskIndex = 0;
		for (UTexture* Texture : PendingTextures)
		{
			PendingTasks[PendingTaskIndex].Texture.Reset(Texture);
			GThreadPool->AddQueuedWork(&PendingTasks[PendingTaskIndex], EQueuedWorkPriority::High);
			PendingTaskIndex++;
		}

		auto UpdateProgress =
			[&SlowTask](float Progress, int32 Done, int32 Total, const FString& CurrentObjectsName)
			{
				return SlowTask.EnterProgressFrame(Progress, FText::FromString(FString::Printf(TEXT("Waiting for textures to be ready %d/%d (%s) ..."), Done, Total, *CurrentObjectsName)));
			};

		for (FTextureTask& PendingTask : PendingTasks)
		{
			UTexture* Texture = PendingTask.Texture.Get();
			const FString TextureName = Texture->GetName();
			// Be nice with the game thread and tick the progress at 60 fps even when no progress is being made...
			while (!PendingTask.Event->Wait(16))
			{
				UpdateProgress(0.0f, TextureIndex, InTextures.Num(), TextureName);
			}
			UE_LOG(LogTexture, Display, TEXT("Waiting for textures to be ready %d/%d (%s) ..."), TextureIndex, InTextures.Num(), *TextureName);
			UpdateProgress(1.f, TextureIndex++, InTextures.Num(), TextureName);
			FinishTextureCompilation(Texture);

			for (TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
			{
				Bucket.Remove(Texture);
			}
		}
	}

	PostTextureCompilation(PendingTextures);
}

void FTextureCompilingManager::PostTextureCompilation(const TSet<UTexture*>& InCompiledTextures)
{
	using namespace TextureCompilingManagerImpl;
	if (InCompiledTextures.Num())
	{
		FObjectCacheContextScope ObjectCacheScope;
		TRACE_CPUPROFILER_EVENT_SCOPE(PostTextureCompilation);
		{
			TSet<UMaterialInterface*> AffectedMaterials;
			for (UTexture* Texture : InCompiledTextures)
			{
				AffectedMaterials.Append(ObjectCacheScope.GetContext().GetMaterialsAffectedByTexture(Texture));
			}

			if (AffectedMaterials.Num())
			{
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(UpdateMaterials);

					for (UMaterialInterface* MaterialToUpdate : AffectedMaterials)
					{
						FMaterialRenderProxy* RenderProxy = MaterialToUpdate->GetRenderProxy();
						if (RenderProxy)
						{
							ENQUEUE_RENDER_COMMAND(TextureCompiler_RecacheUniformExpressions)(
								[RenderProxy](FRHICommandListImmediate& RHICmdList)
								{
									RenderProxy->CacheUniformExpressions(false);
								});
						}
					}
				}

				{
					TRACE_CPUPROFILER_EVENT_SCOPE(UpdatePrimitives);

					TSet<UPrimitiveComponent*> AffectedPrimitives;
					for (UMaterialInterface* MaterialInterface : AffectedMaterials)
					{
						AffectedPrimitives.Append(ObjectCacheScope.GetContext().GetPrimitivesAffectedByMaterial(MaterialInterface));
					}

					for (UPrimitiveComponent* AffectedPrimitive : AffectedPrimitives)
					{
						AffectedPrimitive->MarkRenderStateDirty();
					}
				}
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(OnAssetPostCompileEvent);

			TArray<FAssetCompileData> AssetsData;
			AssetsData.Reserve(InCompiledTextures.Num());

			for (UTexture* Texture : InCompiledTextures)
			{
				AssetsData.Emplace(Texture);
			}

			FAssetCompilingManager::Get().OnAssetPostCompileEvent().Broadcast(AssetsData);
		}
	}
}

void FTextureCompilingManager::FinishAllCompilation()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCompilingManager::FinishAllCompilation)

	if (GetNumRemainingTextures())
	{
		TArray<UTexture*> PendingTextures;
		PendingTextures.Reserve(GetNumRemainingTextures());

		for (TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
		{
			for (TWeakObjectPtr<UTexture>& Texture : Bucket)
			{
				if (Texture.IsValid())
				{
					PendingTextures.Add(Texture.Get());
				}
			}
		}

		FinishCompilation(PendingTextures);
	}
}

bool FTextureCompilingManager::RequestPriorityChange(UTexture* InTexture, EQueuedWorkPriority InPriority)
{
	using namespace TextureCompilingManagerImpl;
	if (InTexture)
	{
		FTexturePlatformData** Data = InTexture->GetRunningPlatformData();
		if (Data && *Data)
		{
			FTextureAsyncCacheDerivedDataTask* AsyncTask = (*Data)->AsyncTask;
			if (AsyncTask)
			{
				EQueuedWorkPriority OldPriority = AsyncTask->GetPriority();
				if (OldPriority != InPriority)
				{
					if (AsyncTask->Reschedule(GetThreadPool(), InPriority))
					{
						UE_LOG(
							LogTexture,
							Verbose,
							TEXT("Changing priority of %s (%s) from %s to %s"),
							*InTexture->GetName(),
							*GetLODGroupName(InTexture),
							GetPriorityName(OldPriority),
							GetPriorityName(InPriority)
						);

						return true;
					}
				}
			}
		}
	}

	return false;
}

void FTextureCompilingManager::ProcessTextures(bool bLimitExecutionTime, int32 MaximumPriority)
{
	using namespace TextureCompilingManagerImpl;
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCompilingManager::ProcessTextures);
	const double MaxSecondsPerFrame = 0.016;

	if (GetNumRemainingTextures())
	{
		FObjectCacheContextScope ObjectCacheScope;
		TSet<UTexture*> ProcessedTextures;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessFinishedTextures);

			double TickStartTime = FPlatformTime::Seconds();

			if (MaximumPriority == -1 || MaximumPriority > RegisteredTextureBuckets.Num())
			{
				MaximumPriority = RegisteredTextureBuckets.Num();
			}
			
			for (int32 PriorityIndex = 0; PriorityIndex < MaximumPriority; ++PriorityIndex)
			{
				TSet<TWeakObjectPtr<UTexture>>& TexturesToProcess = RegisteredTextureBuckets[PriorityIndex];
				if (TexturesToProcess.Num())
				{
					const bool bIsHighestPrio = PriorityIndex == 0;
			
					TSet<TWeakObjectPtr<UTexture>> TexturesToPostpone;
					for (TWeakObjectPtr<UTexture>& Texture : TexturesToProcess)
					{
						if (Texture.IsValid())
						{
							const bool bHasTimeLeft = bLimitExecutionTime ? ((FPlatformTime::Seconds() - TickStartTime) < MaxSecondsPerFrame) : true;
							if ((bIsHighestPrio || bHasTimeLeft) && Texture->IsAsyncCacheComplete())
							{
								FinishTextureCompilation(Texture.Get());
								ProcessedTextures.Add(Texture.Get());
							}
							else
							{
								TexturesToPostpone.Emplace(MoveTemp(Texture));
							}
						}
					}

					RegisteredTextureBuckets[PriorityIndex] = MoveTemp(TexturesToPostpone);
				}
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCompilingManager::Reschedule);

			TSet<UTexture*> ReferencedTextures;
			if (GEngine)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GatherSeenPrimitiveMaterials);

				TSet<UMaterialInterface*> RenderedMaterials;
				for (UPrimitiveComponent* Component : ObjectCacheScope.GetContext().GetPrimitiveComponents())
				{
					if (Component->IsRegistered() && Component->IsRenderStateCreated() && Component->GetLastRenderTimeOnScreen() > 0.0f)
					{
						for (UMaterialInterface* MaterialInterface : ObjectCacheScope.GetContext().GetUsedMaterials(Component))
						{
							if (MaterialInterface)
							{
								RenderedMaterials.Add(MaterialInterface);
							}
						}
					}
				}

				for (UMaterialInterface* MaterialInstance : RenderedMaterials)
				{
					ReferencedTextures.Append(ObjectCacheScope.GetContext().GetUsedTextures(MaterialInstance));
				}
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ApplyPriorityChanges);
				// Reschedule higher priority if they have been rendered
				for (TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
				{
					for (TWeakObjectPtr<UTexture>& WeakPtr : Bucket)
					{
						if (UTexture* Texture = WeakPtr.Get())
						{
							// Reschedule any texture that have been rendered with slightly higher priority 
							// to improve the editor experience for low-core count.
							//
							// Keep in mind that some textures are only accessed once during the construction
							// of a virtual texture, so we can't count on the LastRenderTime to be updated
							// continuously for those even if they're in view.
							if (ReferencedTextures.Contains(Texture) ||
								(Texture->Resource && Texture->Resource->LastRenderTime > 0.0f) ||
								Texture->TextureReference.GetLastRenderTime() > 0.0f)
							{
								RequestPriorityChange(Texture, GetBoostPriority(Texture));
							}
						}
					}
				}
			}
		}

		if (ProcessedTextures.Num())
		{
			PostTextureCompilation(ProcessedTextures);
		}
	}
}

void FTextureCompilingManager::FinishCompilationsForGame()
{
	if (GetNumRemainingTextures())
	{
		// Supports both Game and PIE mode
		const bool bIsPlaying =
			(GWorld && !GWorld->IsEditorWorld()) ||
			(GEditor && GEditor->PlayWorld && !GEditor->IsSimulateInEditorInProgress());

		if (bIsPlaying)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCompilingManager::FinishCompilationsForGame);

			TSet<UTexture*> TexturesRequiredForGame;
			for (TSet<TWeakObjectPtr<UTexture>>& Bucket : RegisteredTextureBuckets)
			{
				for (TWeakObjectPtr<UTexture>& WeakTexture : Bucket)
				{
					if (UTexture* Texture = WeakTexture.Get())
					{
						switch (Texture->LODGroup)
						{
						case TEXTUREGROUP_Terrain_Heightmap:
						case TEXTUREGROUP_Terrain_Weightmap:
							TexturesRequiredForGame.Add(Texture);
							break;
						default:
							break;
						}
					}
				}
			}

			if (TexturesRequiredForGame.Num())
			{
				FinishCompilation(TexturesRequiredForGame.Array());
			}
		}
	}
}

void FTextureCompilingManager::ProcessAsyncTasks(bool bLimitExecutionTime)
{
	FObjectCacheContextScope ObjectCacheScope;
	FinishCompilationsForGame();

	ProcessTextures(bLimitExecutionTime);

	UpdateCompilationNotification();
}

#undef LOCTEXT_NAMESPACE

#endif // #if WITH_EDITOR
