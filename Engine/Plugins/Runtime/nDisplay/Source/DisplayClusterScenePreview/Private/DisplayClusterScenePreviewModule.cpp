// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterScenePreviewModule.h"

#include "Blueprints/DisplayClusterBlueprintLib.h"
#include "CanvasTypes.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterRootActor.h"
#include "Engine/TextureRenderTarget2D.h"

#define LOCTEXT_NAMESPACE "DisplayClusterScenePreview"

static TAutoConsoleVariable<float> CVarDisplayClusterScenePreviewRenderTickDelay(
	TEXT("nDisplay.ScenePreview.RenderTickDelay"),
	0.1f,
	TEXT("The number of seconds to wait between processing queued renders.")
);

void FDisplayClusterScenePreviewModule::StartupModule()
{
}

void FDisplayClusterScenePreviewModule::ShutdownModule()
{
	FTSTicker::GetCoreTicker().RemoveTicker(RenderTickerHandle);
	RenderTickerHandle.Reset();
}

int32 FDisplayClusterScenePreviewModule::CreateRenderer()
{
	FRendererConfig& Config = RendererConfigs.Add(NextRendererId);

	Config.Renderer = MakeShared<FDisplayClusterMeshProjectionRenderer>();

	return NextRendererId++;
}

bool FDisplayClusterScenePreviewModule::DestroyRenderer(int32 RendererId)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		RegisterRootActorEvents(Config->RootActor.Get(), false);
		RendererConfigs.Remove(RendererId);

		RegisterOrUnregisterGlobalActorEvents();
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::SetRendererRootActorPath(int32 RendererId, const FString& ActorPath, bool bAutoUpdateLightcards)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->RootActorPath = ActorPath;

		ADisplayClusterRootActor* RootActor = FindObject<ADisplayClusterRootActor>(nullptr, *ActorPath);
		InternalSetRendererRootActor(*Config, RootActor, bAutoUpdateLightcards);

		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::SetRendererRootActor(int32 RendererId, ADisplayClusterRootActor* Actor, bool bAutoUpdateLightcards)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->RootActorPath.Empty();
		InternalSetRendererRootActor(*Config, Actor, bAutoUpdateLightcards);

		return true;
	}

	return false;
}

ADisplayClusterRootActor* FDisplayClusterScenePreviewModule::GetRendererRootActor(int32 RendererId)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		return InternalGetRendererRootActor(*Config);
	}

	return nullptr;
}

bool FDisplayClusterScenePreviewModule::AddActorToRenderer(int32 RendererId, AActor* Actor)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->Renderer->AddActor(Actor);
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::AddActorToRenderer(int32 RendererId, AActor* Actor, const TFunctionRef<bool(const UPrimitiveComponent*)>& PrimitiveFilter)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->Renderer->AddActor(Actor, PrimitiveFilter);
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::RemoveActorFromRenderer(int32 RendererId, AActor* Actor)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->Renderer->RemoveActor(Actor);
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::ClearRendererScene(int32 RendererId)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->Renderer->ClearScene();
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::SetRendererActorSelectedDelegate(int32 RendererId, FDisplayClusterMeshProjectionRenderer::FSelection ActorSelectedDelegate)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->Renderer->ActorSelectedDelegate = ActorSelectedDelegate;
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::SetRendererRenderSimpleElementsDelegate(int32 RendererId, FDisplayClusterMeshProjectionRenderer::FSimpleElementPass RenderSimpleElementsDelegate)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		Config->Renderer->RenderSimpleElementsDelegate = RenderSimpleElementsDelegate;
		return true;
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::Render(int32 RendererId, FDisplayClusterMeshProjectionRenderSettings& RenderSettings, FCanvas& Canvas)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		return InternalRenderImmediate(*Config, RenderSettings, Canvas);
	}

	return false;
}

bool FDisplayClusterScenePreviewModule::RenderQueued(int32 RendererId, FDisplayClusterMeshProjectionRenderSettings& RenderSettings, const FIntPoint& Size, FRenderResultDelegate ResultDelegate)
{
	if (FRendererConfig* Config = RendererConfigs.Find(RendererId))
	{
		RenderQueue.Enqueue(FPreviewRenderJob(RendererId, RenderSettings, Size, ResultDelegate));

		if (!RenderTickerHandle.IsValid())
		{
			RenderTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FDisplayClusterScenePreviewModule::OnTick),
				CVarDisplayClusterScenePreviewRenderTickDelay.GetValueOnGameThread()
			);
		}

		return true;
	}

	return false;
}

ADisplayClusterRootActor* FDisplayClusterScenePreviewModule::InternalGetRendererRootActor(FRendererConfig& RendererConfig)
{
	if (!RendererConfig.RootActor.IsValid() && !RendererConfig.RootActorPath.IsEmpty())
	{
		// Try to find the actor by its path
		ADisplayClusterRootActor* RootActor = FindObject<ADisplayClusterRootActor>(nullptr, *RendererConfig.RootActorPath);
		if (RootActor)
		{
			InternalSetRendererRootActor(RendererConfig, RootActor, RendererConfig.bAutoUpdateLightcards);
		}
	}

	return RendererConfig.RootActor.Get();
}

void FDisplayClusterScenePreviewModule::InternalSetRendererRootActor(FRendererConfig& RendererConfig, ADisplayClusterRootActor* Actor, bool bAutoUpdateLightcards)
{
	// Determine these values before we update the config's RootActor/bAutoUpdateLightcards
	const bool bRootChanged = RendererConfig.RootActor != Actor;
	const bool bStartAutoUpdating = bAutoUpdateLightcards && (!RendererConfig.bAutoUpdateLightcards || bRootChanged);

	if (bRootChanged)
	{
		// Unregister events for the previous cluster
		if (RendererConfig.bAutoUpdateLightcards && RendererConfig.RootActor.IsValid())
		{
			RegisterRootActorEvents(RendererConfig.RootActor.Get(), false);
		}

		RendererConfig.RootActor = Actor;
		RendererConfig.bAutoUpdateLightcards = bAutoUpdateLightcards;
		AutoPopulateScene(RendererConfig);
	}

	if (bStartAutoUpdating)
	{
		RegisterRootActorEvents(Actor, true);
	}

	RegisterOrUnregisterGlobalActorEvents();
}

bool FDisplayClusterScenePreviewModule::InternalRenderImmediate(FRendererConfig& RendererConfig, FDisplayClusterMeshProjectionRenderSettings& RenderSettings, FCanvas& Canvas)
{
	UWorld* World = RendererConfig.RootActor.IsValid() ? RendererConfig.RootActor->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	if (RendererConfig.bAutoUpdateLightcards && RendererConfig.bIsSceneDirty)
	{
		AutoPopulateScene(RendererConfig);
	}

	RendererConfig.Renderer->Render(&Canvas, World->Scene, RenderSettings);
	return true;
}

void FDisplayClusterScenePreviewModule::RegisterOrUnregisterGlobalActorEvents()
{
	// Check whether any of our configs need actor events
	bool bShouldBeRegistered = false;
	for (const TPair<int32, FRendererConfig>& ConfigPair : RendererConfigs)
	{
		if (ConfigPair.Value.bAutoUpdateLightcards)
		{
			bShouldBeRegistered = true;
			break;
		}
	}

#if WITH_EDITOR
	if (bShouldBeRegistered && !bIsRegisteredForActorEvents)
	{
		// Register for events
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDisplayClusterScenePreviewModule::OnActorPropertyChanged);
		FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FDisplayClusterScenePreviewModule::OnObjectTransacted);

		if (GEngine != nullptr)
		{
			GEngine->OnLevelActorDeleted().AddRaw(this, &FDisplayClusterScenePreviewModule::OnLevelActorDeleted);
		}
	}
	else if (!bShouldBeRegistered && bIsRegisteredForActorEvents)
	{
		// Unregister for events
		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectTransacted.RemoveAll(this);

		if (GEngine != nullptr)
		{
			GEngine->OnLevelActorDeleted().RemoveAll(this);
		}
	}
#endif
}

void FDisplayClusterScenePreviewModule::RegisterRootActorEvents(AActor* Actor, bool bShouldRegister)
{
#if WITH_EDITOR
	if (!Actor)
	{
		return;
	}

	if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Actor->GetClass()))
	{
		if (bShouldRegister)
		{
			Blueprint->OnCompiled().AddRaw(this, &FDisplayClusterScenePreviewModule::OnBlueprintCompiled);
		}
		else
		{
			Blueprint->OnCompiled().RemoveAll(this);
		}
	}
#endif
}

void FDisplayClusterScenePreviewModule::AutoPopulateScene(FRendererConfig& RendererConfig)
{
	RendererConfig.Renderer->ClearScene();

	if (ADisplayClusterRootActor* RootActor = InternalGetRendererRootActor(RendererConfig))
	{
		RendererConfig.Renderer->AddActor(RootActor, [](const UPrimitiveComponent* PrimitiveComponent)
		{
			return !PrimitiveComponent->bHiddenInGame || PrimitiveComponent->IsA<UDisplayClusterScreenComponent>();
		});

		if (RendererConfig.bAutoUpdateLightcards)
		{
			// Automatically add the lightcards found on this actor
			TSet<ADisplayClusterLightCardActor*> LightCards;
			UDisplayClusterBlueprintLib::FindLightCardsForRootActor(RootActor, LightCards);

			for (ADisplayClusterLightCardActor* LightCard : LightCards)
			{
				RendererConfig.Renderer->AddActor(LightCard);
				RendererConfig.AutoLightcards.Add(LightCard);
			}
		}
	}

	RendererConfig.bIsSceneDirty = false;
}

bool FDisplayClusterScenePreviewModule::OnTick(float DeltaTime)
{
	FPreviewRenderJob Job;
	if (!RenderQueue.Dequeue(Job))
	{
		RenderTickerHandle.Reset();
		return false;
	}

	ensure(Job.ResultDelegate.IsBound());

	if (FRendererConfig* Config = RendererConfigs.Find(Job.RendererId))
	{
		if (UWorld* World = Config->RootActor.IsValid() ? Config->RootActor->GetWorld() : nullptr)
		{
			UTextureRenderTarget2D* RenderTarget = Config->RenderTarget.Get();

			if (!RenderTarget)
			{
				// Create a new render target
				RenderTarget = NewObject<UTextureRenderTarget2D>();
				RenderTarget->InitCustomFormat(Job.Size.X, Job.Size.Y, PF_B8G8R8A8, false);

				Config->RenderTarget = TStrongObjectPtr<UTextureRenderTarget2D>(RenderTarget);
			}
			else if (RenderTarget->SizeX != Job.Size.X || RenderTarget->SizeY != Job.Size.Y)
			{
				// Resize to match the new size
				RenderTarget->ResizeTarget(Job.Size.X, Job.Size.Y);
				
				// Flush commands so target is immediately ready to render at the new size
				FlushRenderingCommands();
			}

			FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
			FCanvas Canvas(RenderTargetResource, nullptr, FGameTime::GetTimeSinceAppStart(), World->Scene->GetFeatureLevel());

			InternalRenderImmediate(*Config, Job.Settings, Canvas);

			Job.ResultDelegate.Execute(*RenderTargetResource);
		}
	}

	if (RenderQueue.IsEmpty())
	{
		RenderTickerHandle.Reset();
		return false;
	}

	return true;
}

void FDisplayClusterScenePreviewModule::OnActorPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	for (TPair<int32, FRendererConfig>& ConfigPair : RendererConfigs)
	{
		FRendererConfig& Config = ConfigPair.Value;
		if (Config.bAutoUpdateLightcards && Config.RootActor == ObjectBeingModified)
		{
			Config.bIsSceneDirty = true;
		}
	}
}

void FDisplayClusterScenePreviewModule::OnLevelActorDeleted(AActor* Actor)
{
	for (TPair<int32, FRendererConfig>& ConfigPair : RendererConfigs)
	{
		FRendererConfig& Config = ConfigPair.Value;
		if (Config.AutoLightcards.Contains(Actor))
		{
			Config.bIsSceneDirty = true;
		}
	}
}

void FDisplayClusterScenePreviewModule::OnBlueprintCompiled(UBlueprint* Blueprint)
{
#if WITH_EDITOR
	for (TPair<int32, FRendererConfig>& ConfigPair : RendererConfigs)
	{
		FRendererConfig& Config = ConfigPair.Value;
		if (Config.RootActor.IsValid() && Blueprint == UBlueprint::GetBlueprintFromClass(Config.RootActor->GetClass()))
		{
			Config.bIsSceneDirty = true;
		}
	}
#endif
}

void FDisplayClusterScenePreviewModule::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& TransactionObjectEvent)
{
	if (TransactionObjectEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		for (TPair<int32, FRendererConfig>& ConfigPair : RendererConfigs)
		{
			ConfigPair.Value.bIsSceneDirty = true;
		}
	}
}

IMPLEMENT_MODULE(FDisplayClusterScenePreviewModule, DisplayClusterScenePreview);

#undef LOCTEXT_NAMESPACE
