// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureThumbnailRenderer.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "MaterialShared.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureRender.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Find a matching component for this URuntimeVirtualTexture. */
	URuntimeVirtualTextureComponent* FindComponent(URuntimeVirtualTexture* RuntimeVirtualTexture)
	{
		for (TObjectIterator<URuntimeVirtualTextureComponent> It; It; ++It)
		{
			URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = *It;
			if (RuntimeVirtualTextureComponent->GetVirtualTexture() == RuntimeVirtualTexture)
			{
				return RuntimeVirtualTextureComponent;
			}
		}

		return nullptr;
	}
}

URuntimeVirtualTextureThumbnailRenderer::URuntimeVirtualTextureThumbnailRenderer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool URuntimeVirtualTextureThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	URuntimeVirtualTexture* RuntimeVirtualTexture = Cast<URuntimeVirtualTexture>(Object);
	if (RuntimeVirtualTexture->GetEnabled())
	{
		// We need a matching URuntimeVirtualTextureComponent in a Scene to be able to render a thumbnail
		URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = FindComponent(RuntimeVirtualTexture);
		if (RuntimeVirtualTextureComponent != nullptr)
		{
			FSceneInterface* Scene = RuntimeVirtualTextureComponent->GetScene();
			if (Scene != nullptr && RuntimeVirtualTexture::IsSceneReadyToRender(Scene->GetRenderScene()))
			{
				return true;
			}
		}
	}

	return false;
}

void URuntimeVirtualTextureThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas)
{
	URuntimeVirtualTexture* RuntimeVirtualTexture = Cast<URuntimeVirtualTexture>(Object);
	URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = FindComponent(RuntimeVirtualTexture);
	FSceneInterface* Scene = RuntimeVirtualTextureComponent != nullptr ? RuntimeVirtualTextureComponent->GetScene() : nullptr;
	check(Scene != nullptr);

	const FBox2D DestBox = FBox2D(FVector2D(X, Y), FVector2D(Width, Height));
	const FTransform Transform = RuntimeVirtualTextureComponent->GetVirtualTextureTransform();
	const uint32 VirtualTextureSceneIndex = RuntimeVirtualTexture::GetRuntimeVirtualTextureSceneIndex_GameThread(RuntimeVirtualTextureComponent);
	const ERuntimeVirtualTextureMaterialType MaterialType = RuntimeVirtualTexture->GetMaterialType();

	FVTProducerDescription VTDesc;
	RuntimeVirtualTexture->GetProducerDescription(VTDesc, Transform);
	const int32 MaxLevel = (int32)FMath::CeilLogTwo(FMath::Max(VTDesc.BlockWidthInTiles, VTDesc.BlockHeightInTiles));

	ENQUEUE_RENDER_COMMAND(BakeStreamingTextureTileCommand)(
		[Scene, VirtualTextureSceneIndex, MaterialType, RenderTarget, DestBox, Transform, MaxLevel](FRHICommandListImmediate& RHICmdList)
	{
		FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

		RuntimeVirtualTexture::FRenderPageBatchDesc Desc;
		Desc.Scene = Scene->GetRenderScene();
		Desc.RuntimeVirtualTextureMask = 1 << VirtualTextureSceneIndex;
		Desc.UVToWorld = Transform;
		Desc.MaterialType = MaterialType;
		Desc.MaxLevel = MaxLevel;
		Desc.bClearTextures = true;
		Desc.bIsThumbnails = true;
		Desc.DebugType = ERuntimeVirtualTextureDebugType::None;
		Desc.NumPageDescs = 1;
		Desc.Targets[0].Texture = RenderTarget->GetRenderTargetTexture();
		Desc.PageDescs[0].DestBox[0] = DestBox;
		Desc.PageDescs[0].UVRange = FBox2D(FVector2D(0, 0), FVector2D(1, 1));
		Desc.PageDescs[0].vLevel = MaxLevel;

		RuntimeVirtualTexture::RenderPages(RHICmdList, Desc);
	});
}
