// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureThumbnailRenderer.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureRender.h"

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
		//todo[vt]: Support thumbnails for ERuntimeVirtualTextureMaterialType::WorldHeight (which requires render to PF_G16)
		if (RuntimeVirtualTexture->GetMaterialType() != ERuntimeVirtualTextureMaterialType::WorldHeight)
		{
			// We need a matching URuntimeVirtualTextureComponent in a Scene to be able to render a thumbnail
			URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent = FindComponent(RuntimeVirtualTexture);
			FSceneInterface* Scene = RuntimeVirtualTextureComponent != nullptr ? RuntimeVirtualTextureComponent->GetScene() : nullptr;
			return Scene != nullptr;
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

	const FBox2D DestRect = FBox2D(FVector2D(X, Y), FVector2D(Width, Height));
	const FTransform Transform = RuntimeVirtualTextureComponent->GetVirtualTextureTransform();
	const uint32 VirtualTextureSceneIndex = RuntimeVirtualTexture::GetRuntimeVirtualTextureSceneIndex_GameThread(RuntimeVirtualTextureComponent);
	const ERuntimeVirtualTextureMaterialType MaterialType = RuntimeVirtualTexture->GetMaterialType();

	FVTProducerDescription VTDesc;
	RuntimeVirtualTexture->GetProducerDescription(VTDesc, Transform);
	const int32 MaxLevel = (int32)FMath::CeilLogTwo(FMath::Max(VTDesc.BlockWidthInTiles, VTDesc.BlockHeightInTiles));

	ENQUEUE_RENDER_COMMAND(BakeStreamingTextureTileCommand)(
		[Scene, VirtualTextureSceneIndex, MaterialType, RenderTarget, DestRect, Transform, MaxLevel](FRHICommandListImmediate& RHICmdList)
	{
		RuntimeVirtualTexture::RenderPage(
			RHICmdList,
			Scene->GetRenderScene(),
			1 << VirtualTextureSceneIndex,
			MaterialType,
			RenderTarget->GetRenderTargetTexture(), DestRect,
			nullptr, DestRect,
			Transform,
			FBox2D(FVector2D(0, 0), FVector2D(1, 1)),
			MaxLevel,
			MaxLevel,
			ERuntimeVirtualTextureDebugType::None);
	});
}
