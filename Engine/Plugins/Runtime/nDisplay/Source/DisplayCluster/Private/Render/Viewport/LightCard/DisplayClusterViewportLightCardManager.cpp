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

FDisplayClusterViewportLightCardManager::FDisplayClusterViewportLightCardManager(FDisplayClusterViewportManager& InViewportManager)
	: ViewportManager(InViewportManager)
{
}

FDisplayClusterViewportLightCardManager::~FDisplayClusterViewportLightCardManager()
{
	ResetScene();
	ReleaseUVLightCardMap();
	PreviewScene.Reset();
}

void FDisplayClusterViewportLightCardManager::UpdateConfiguration()
{
	ResetScene();

	ADisplayClusterRootActor* RootActor = ViewportManager.GetRootActor();
	TSet<ADisplayClusterLightCardActor*> LightCards;
	UDisplayClusterBlueprintLib::FindLightCardsForRootActor(RootActor, LightCards);

	for (ADisplayClusterLightCardActor* LightCard : LightCards)
	{
		if (LightCard->bIsUVLightCard)
		{
			UVLightCards.Add(LightCard);
		}
	}
}

void FDisplayClusterViewportLightCardManager::ResetScene()
{
	if (PreviewScene.IsValid() && LoadedPrimitiveComponents.Num())
	{
		for (UPrimitiveComponent* PrimitiveComponent : LoadedPrimitiveComponents)
		{
			PreviewScene->GetScene()->RemovePrimitive(PrimitiveComponent);
		}
	}

	LoadedPrimitiveComponents.Empty();
	UVLightCards.Empty();
}

void FDisplayClusterViewportLightCardManager::HandleStartScene()
{
	PreviewScene = MakeShared<FPreviewScene>(FPreviewScene::ConstructionValues()
		.SetEditor(false)
		.SetCreatePhysicsScene(false)
		.SetCreateDefaultLighting(false));

	UpdateConfiguration();
}

void FDisplayClusterViewportLightCardManager::HandleBeginNewFrame()
{
	InitializeUVLightCardMap();
}

void FDisplayClusterViewportLightCardManager::PreRenderFrame()
{
	bool bLoadedPrimitives = false;
	for (ADisplayClusterLightCardActor* LightCard : UVLightCards)
	{
		UStaticMeshComponent* LightCardMeshComp = LightCard->GetLightCardMeshComponent();
		if (LightCardMeshComp && LightCardMeshComp->SceneProxy == nullptr)
		{
			PreviewScene->GetScene()->AddPrimitive(LightCardMeshComp);
			LoadedPrimitiveComponents.Add(LightCardMeshComp);

			bLoadedPrimitives = true;
		}
	}

	ENQUEUE_RENDER_COMMAND(InitializeLightCardRenderFrame)(
		[this, bLoadedPrimitives](FRHICommandListImmediate& RHICmdList)
	{
		bHasUVLightCards = bLoadedPrimitives;
	});
}

void FDisplayClusterViewportLightCardManager::PostRenderFrame()
{
	for (UPrimitiveComponent* LoadedComponent : LoadedPrimitiveComponents)
	{
		PreviewScene->GetScene()->RemovePrimitive(LoadedComponent);
	}

	LoadedPrimitiveComponents.Empty();
}

void FDisplayClusterViewportLightCardManager::RenderLightCardMap_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	if (PreviewScene.IsValid() && bHasUVLightCards)
	{
		IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();
		ShadersAPI.RenderPreprocess_UVLightCards(RHICmdList, PreviewScene->GetScene(), UVLightCardMapProxy, ADisplayClusterLightCardActor::UVPlaneDefaultSize);
	}
}

void FDisplayClusterViewportLightCardManager::InitializeUVLightCardMap()
{
	const uint32 LightCardTextureSize = CVarUVLightCardTextureSize.GetValueOnAnyThread();
	if (UVLightCardMap && UVLightCardMap->GetSizeX() != LightCardTextureSize)
	{
		ReleaseUVLightCardMap();
	}

	if (UVLightCardMap == nullptr)
	{
		UVLightCardMap = new FDisplayClusterLightCardMap(LightCardTextureSize);

		ENQUEUE_RENDER_COMMAND(InitializeLightCardResources)(
			[this, InUVLightCardMap=UVLightCardMap](FRHICommandListImmediate& RHICmdList)
		{
			// Store a copy of the texture's pointer on the render thread and initialize the texture's resources
			UVLightCardMapProxy = InUVLightCardMap;
			UVLightCardMapProxy->InitResource();
		});
	}
}

void FDisplayClusterViewportLightCardManager::ReleaseUVLightCardMap()
{
	if (UVLightCardMap)
	{
		UVLightCardMap = nullptr;

		ENQUEUE_RENDER_COMMAND(DeleteLightCardResources)(
			[InUVLightCardMap=UVLightCardMapProxy](FRHICommandListImmediate& RHICmdList)
		{
			// Release the texture's resources and delete the texture object from the rendering thread. Pointer is copied to ensure it is still
			// valid on the render thread even if the light card manager has been disposed already
			InUVLightCardMap->ReleaseResource();
			delete InUVLightCardMap;
		});
	}
}