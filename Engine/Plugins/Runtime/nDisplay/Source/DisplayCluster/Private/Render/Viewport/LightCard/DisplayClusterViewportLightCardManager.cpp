// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportLightCardManager.h"
#include "DisplayClusterViewportLightCardManagerProxy.h"
#include "DisplayClusterViewportLightCardResource.h"

#include "DisplayClusterLightCardActor.h"
#include "Blueprints/DisplayClusterBlueprintLib.h"

#include "IDisplayClusterShaders.h"

#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"

#include "PreviewScene.h"
#include "SceneInterface.h"
#include "UObject/Package.h"

///////////////////////////////////////////////////////////////////////////////////////////////
/** Console variable used to control the size of the UV light card map texture */
static TAutoConsoleVariable<int32> CVarUVLightCardTextureSize(
	TEXT("nDisplay.render.uvlightcards.UVTextureSize"),
	512,
	TEXT("The size of the texture UV light cards are rendered to.")
);

///////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewportLightCardManager
///////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterViewportLightCardManager::FDisplayClusterViewportLightCardManager(FDisplayClusterViewportManager& InViewportManager)
	: ViewportManager(InViewportManager)
{
	LightCardManagerProxy = MakeShared<FDisplayClusterViewportLightCardManagerProxy, ESPMode::ThreadSafe>();
}

FDisplayClusterViewportLightCardManager::~FDisplayClusterViewportLightCardManager()
{
	Release();

	LightCardManagerProxy.Reset();
}

void FDisplayClusterViewportLightCardManager::Release()
{
	// The destructor is usually called from the rendering thread, so Release() must be called first from the game thread.
	const bool bIsInRenderingThread = IsInRenderingThread();
	check(!bIsInRenderingThread || (bIsInRenderingThread && PreviewWorld == nullptr));

	// Release UVLightCard
	ReleaseUVLightCardData();
	ReleaseUVLightCardResource();

	// Deleting PreviewScene is only called from the game thread
	DestroyPreviewWorld();
}

///////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterViewportLightCardManager::UpdateConfiguration()
{

}

void FDisplayClusterViewportLightCardManager::HandleStartScene()
{
	InitializePreviewWorld();
}

void FDisplayClusterViewportLightCardManager::HandleEndScene()
{
	DestroyPreviewWorld();

	ReleaseUVLightCardData();
}

void FDisplayClusterViewportLightCardManager::RenderFrame()
{
	UpdateUVLightCardData();
	RenderUVLightCard();
}

///////////////////////////////////////////////////////////////////////////////////////////////
FIntPoint FDisplayClusterViewportLightCardManager::GetUVLightCardResourceSize() const
{
	return UVLightCardResource.IsValid() ? UVLightCardResource->GetSizeXY() : FIntPoint(0, 0);
}

bool FDisplayClusterViewportLightCardManager::IsUVLightCardEnabled() const
{
	return !UVLightCardPrimitiveComponents.IsEmpty();
}

void FDisplayClusterViewportLightCardManager::ReleaseUVLightCardData()
{
	UVLightCardPrimitiveComponents.Empty();
}

void FDisplayClusterViewportLightCardManager::UpdateUVLightCardData()
{
	ReleaseUVLightCardData();

	/** The list of UV light card actors that are referenced by the root actor */
	TArray<ADisplayClusterLightCardActor*> UVLightCardActors;

	if (ADisplayClusterRootActor* RootActorPtr = ViewportManager.GetRootActor())
	{
		TSet<ADisplayClusterLightCardActor*> LightCards;
		UDisplayClusterBlueprintLib::FindLightCardsForRootActor(RootActorPtr, LightCards);

		for (ADisplayClusterLightCardActor* LightCard : LightCards)
		{
			if (LightCard->bIsUVLightCard)
			{
				UVLightCardActors.Add(LightCard);
			}
		}
	}

	TArray<UMeshComponent*> LightCardMeshComponents;
	for (ADisplayClusterLightCardActor* LightCard : UVLightCardActors)
	{
		if (LightCard->IsHidden() || LightCard->IsActorBeingDestroyed() || LightCard->GetWorld() == nullptr)
		{
			continue;
		}

		LightCardMeshComponents.Empty(LightCardMeshComponents.Num());
		LightCard->GetLightCardMeshComponents(LightCardMeshComponents);

		for (UMeshComponent* LightCardMeshComp : LightCardMeshComponents)
		{
			if (LightCardMeshComp && LightCardMeshComp->SceneProxy == nullptr)
			{
				UVLightCardPrimitiveComponents.Add(LightCardMeshComp);
			}
		}
	}
}

void FDisplayClusterViewportLightCardManager::CreateUVLightCardResource(const FIntPoint& InResourceSize)
{
	UVLightCardResource = MakeShared<FDisplayClusterViewportLightCardResource>(InResourceSize);
	LightCardManagerProxy->UpdateUVLightCardResource(UVLightCardResource);
}

void FDisplayClusterViewportLightCardManager::ReleaseUVLightCardResource()
{
	if (UVLightCardResource.IsValid())
	{
		LightCardManagerProxy->ReleaseUVLightCardResource();
	}

	UVLightCardResource.Reset();
}

void FDisplayClusterViewportLightCardManager::UpdateUVLightCardResource()
{
	const uint32 UVLightCardTextureSize = CVarUVLightCardTextureSize.GetValueOnGameThread();
	const FIntPoint UVLightCardResourceSize = FIntPoint(UVLightCardTextureSize, UVLightCardTextureSize);

	if (UVLightCardResource.IsValid())
	{
		if (UVLightCardResource->GetSizeXY() != UVLightCardResourceSize)
		{
			ReleaseUVLightCardResource();
		}
	}

	if (!UVLightCardResource.IsValid())
	{
		CreateUVLightCardResource(UVLightCardResourceSize);
	}
}

void FDisplayClusterViewportLightCardManager::RenderUVLightCard()
{
	// Render UV LightCard:
	if (IsUVLightCardEnabled() && PreviewWorld && LightCardManagerProxy.IsValid())
	{
		UpdateUVLightCardResource();

		if (UVLightCardResource.IsValid())
		{
			for (UPrimitiveComponent* LoadedComponent : UVLightCardPrimitiveComponents)
			{
				PreviewWorld->Scene->AddPrimitive(LoadedComponent);
			}

			LightCardManagerProxy->RenderUVLightCard(PreviewWorld->Scene, ADisplayClusterLightCardActor::UVPlaneDefaultSize, ViewportManager.ShouldRenderFinalColor());

			for (UPrimitiveComponent* LoadedComponent : UVLightCardPrimitiveComponents)
			{
				PreviewWorld->Scene->RemovePrimitive(LoadedComponent);
			}
		}
	}
	else
	{
		ReleaseUVLightCardResource();
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterViewportLightCardManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PreviewWorld);
}

///////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterViewportLightCardManager::InitializePreviewWorld()
{
	if (!PreviewWorld)
	{
		FName UniqueWorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), FName(TEXT("DisplayClusterLightCardManager_PreviewWorld")));
		PreviewWorld = NewObject<UWorld>(GetTransientPackage(), UniqueWorldName);
		PreviewWorld->WorldType = EWorldType::GamePreview;

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(PreviewWorld->WorldType);
		WorldContext.SetCurrentWorld(PreviewWorld);

		PreviewWorld->InitializeNewWorld(UWorld::InitializationValues()
			.AllowAudioPlayback(false)
			.CreatePhysicsScene(false)
			.RequiresHitProxies(false)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false));
	}
}

void FDisplayClusterViewportLightCardManager::DestroyPreviewWorld()
{
	if (PreviewWorld)
	{
		// Hack to avoid issue where the engine considers this world a leaked object; When UEngine loads a new map, it checks to see if there are any UWorlds
		// still in memory that aren't what it considers "persistent" worlds, worlds with type Inactive or EditorPreview. Even if the UWorld object has been marked for
		// GC and has no references to it, UEngine will still flag it as "leaked" unless it is one of these two types.
		PreviewWorld->WorldType = EWorldType::Inactive;

		GEngine->DestroyWorldContext(PreviewWorld);

		PreviewWorld->DestroyWorld(false);
		PreviewWorld->MarkObjectsPendingKill();
		PreviewWorld = nullptr;
	}
}
