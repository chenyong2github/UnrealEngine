// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportLightCardManager.h"

#include "DisplayClusterLightCardActor.h"
#include "Blueprints/DisplayClusterBlueprintLib.h"

#include "IDisplayClusterShaders.h"

#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"

#include "PreviewScene.h"

/** Console variable used to control the size of the UV light card map texture */
static TAutoConsoleVariable<int32> CVarUVLightCardTextureSize(
	TEXT("nDisplay.render.uvlightcards.UVTextureSize"),
	512,
	TEXT("The size of the texture UV light cards are rendered to."));

//-----------------------------------------------------------------------------------------------------------------
// FDisplayClusterLightCardMap
//-----------------------------------------------------------------------------------------------------------------
void FDisplayClusterLightCardMap::InitDynamicRHI()
{
	ETextureCreateFlags CreateFlags = TexCreate_Dynamic;
	CreateFlags |= TexCreate_MultiGPUGraphIgnore;

	const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("DisplayClusterLightCardMap"))
		.SetExtent(GetSizeX(), GetSizeY())
		.SetFormat(PF_FloatRGBA)
		.SetFlags(CreateFlags | ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)
		.SetInitialState(ERHIAccess::SRVMask)
		.SetClearValue(FClearValueBinding::Transparent);

	RenderTargetTextureRHI = TextureRHI = RHICreateTexture(Desc);
}

//-----------------------------------------------------------------------------------------------------------------
// FDisplayClusterViewportLightCardManager
//-----------------------------------------------------------------------------------------------------------------
FDisplayClusterViewportLightCardManager::FDisplayClusterViewportLightCardManager(FDisplayClusterViewportManager& InViewportManager)
	: ViewportManager(InViewportManager)
{
	ProxyData = MakeShared<FProxyData, ESPMode::ThreadSafe>();
}

FDisplayClusterViewportLightCardManager::~FDisplayClusterViewportLightCardManager()
{
	Release();

	ProxyData.Reset();
}

void FDisplayClusterViewportLightCardManager::Release()
{
	// The destructor is usually called from the rendering thread, so Release() must be called first from the game thread.
	const bool bIsInRenderingThread = IsInRenderingThread();
	check(!bIsInRenderingThread || (bIsInRenderingThread && PreviewScene.IsValid() == false));

	// Deleting PreviewScene is only called from the game thread
	PreviewScene.Reset();

	UVLightCards.Empty();
	ReleaseUVLightCardMap();
}

FRHITexture* FDisplayClusterViewportLightCardManager::GetUVLightCardMap_RenderThread() const
{ 
	check(IsInRenderingThread());

	return ProxyData.IsValid() ? ProxyData->GetUVLightCardMap_RenderThread() : nullptr;
}

void FDisplayClusterViewportLightCardManager::UpdateConfiguration()
{
	UVLightCards.Empty();

	if (ADisplayClusterRootActor* RootActorPtr = ViewportManager.GetRootActor())
	{
		TSet<ADisplayClusterLightCardActor*> LightCards;
		UDisplayClusterBlueprintLib::FindLightCardsForRootActor(RootActorPtr, LightCards);

		for (ADisplayClusterLightCardActor* LightCard : LightCards)
		{
			if (LightCard->bIsUVLightCard)
			{
				UVLightCards.Add(LightCard);
			}
		}
	}
}

void FDisplayClusterViewportLightCardManager::HandleStartScene()
{
	PreviewScene = MakeShared<FPreviewScene>(FPreviewScene::ConstructionValues()
		.SetEditor(false)
		.SetCreatePhysicsScene(false)
		.SetCreateDefaultLighting(false));
}

void FDisplayClusterViewportLightCardManager::HandleEndScene()
{
	PreviewScene.Reset();
}

void FDisplayClusterViewportLightCardManager::RenderFrame()
{
	if (PreviewScene.IsValid() && ProxyData.IsValid())
	{
		if (FSceneInterface* SceneInterface = PreviewScene->GetScene())
		{
			InitializeUVLightCardMap();

			/** A list of primitive components that have been added to the preview scene for rendering in the current frame */
			TArray<UPrimitiveComponent*> LoadedPrimitiveComponents;

			bool bLoadedPrimitives = false;
			for (ADisplayClusterLightCardActor* LightCard : UVLightCards)
			{
				UStaticMeshComponent* LightCardMeshComp = LightCard->GetLightCardMeshComponent();
				if (LightCardMeshComp && LightCardMeshComp->SceneProxy == nullptr)
				{
					SceneInterface->AddPrimitive(LightCardMeshComp);
					LoadedPrimitiveComponents.Add(LightCardMeshComp);

					bLoadedPrimitives = true;
				}
			}

			ENQUEUE_RENDER_COMMAND(DisplayClusterViewportLightCardManager_InitializeUVLightCardMap)(
				[InProxyData = ProxyData, bLoadedPrimitives, SceneInterface](FRHICommandListImmediate& RHICmdList)
				{
					InProxyData->RenderLightCardMap_RenderThread(RHICmdList, bLoadedPrimitives, SceneInterface);
				});

			for (UPrimitiveComponent* LoadedComponent : LoadedPrimitiveComponents)
			{
				SceneInterface->RemovePrimitive(LoadedComponent);
			}
		}
	}
}

void FDisplayClusterViewportLightCardManager::InitializeUVLightCardMap()
{
	const uint32 LightCardTextureSize = CVarUVLightCardTextureSize.GetValueOnAnyThread();
	if (UVLightCardMap && UVLightCardMap->GetSizeX() != LightCardTextureSize)
	{
		ReleaseUVLightCardMap();
	}

	if (UVLightCardMap == nullptr && ProxyData.IsValid())
	{
		UVLightCardMap = new FDisplayClusterLightCardMap(LightCardTextureSize);

		ENQUEUE_RENDER_COMMAND(DisplayClusterViewportLightCardManager_InitializeUVLightCardMap)(
			[InProxyData = ProxyData, InUVLightCardMap = UVLightCardMap](FRHICommandListImmediate& RHICmdList)
			{
				InProxyData->InitializeUVLightCardMap_RenderThread(InUVLightCardMap);
			});
	}
}

void FDisplayClusterViewportLightCardManager::ReleaseUVLightCardMap()
{
	UVLightCardMap = nullptr;

	if (ProxyData.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(DisplayClusterViewportLightCardManager_ReleaseUVLightCardMap)(
			[InProxyData = ProxyData](FRHICommandListImmediate& RHICmdList)
			{
				InProxyData->ReleaseUVLightCardMap_RenderThread();
			});
	}
}

//-----------------------------------------------------------------------------------------------------------------
// FDisplayClusterViewportLightCardManager::FProxyData
//-----------------------------------------------------------------------------------------------------------------
FDisplayClusterViewportLightCardManager::FProxyData::~FProxyData()
{
	ReleaseUVLightCardMap_RenderThread();
}

void FDisplayClusterViewportLightCardManager::FProxyData::InitializeUVLightCardMap_RenderThread(FDisplayClusterLightCardMap* InUVLightCardMap)
{
	if (UVLightCardMap == nullptr)
	{
		// Store a copy of the texture's pointer on the render thread and initialize the texture's resources
		UVLightCardMap = InUVLightCardMap;
		UVLightCardMap->InitResource();
	}
}

void FDisplayClusterViewportLightCardManager::FProxyData::ReleaseUVLightCardMap_RenderThread()
{
	// Release the texture's resources and delete the texture object from the rendering thread
	if (UVLightCardMap)
	{
		UVLightCardMap->ReleaseResource();

		delete UVLightCardMap;
		UVLightCardMap = nullptr;
	}
}

void FDisplayClusterViewportLightCardManager::FProxyData::RenderLightCardMap_RenderThread(FRHICommandListImmediate& RHICmdList, const bool bLoadedPrimitives, FSceneInterface* InSceneInterface)
{
	bHasUVLightCards = bLoadedPrimitives;

	IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();
	ShadersAPI.RenderPreprocess_UVLightCards(RHICmdList, InSceneInterface, UVLightCardMap, ADisplayClusterLightCardActor::UVPlaneDefaultSize);
}

FRHITexture* FDisplayClusterViewportLightCardManager::FProxyData::GetUVLightCardMap_RenderThread() const
{
	return (bHasUVLightCards && UVLightCardMap != nullptr) ? UVLightCardMap->GetTextureRHI() : nullptr;
}

