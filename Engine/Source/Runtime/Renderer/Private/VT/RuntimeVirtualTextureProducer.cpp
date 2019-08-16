// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureProducer.h"

#include "RendererInterface.h"
#include "RuntimeVirtualTextureRender.h"
#include "ScenePrivate.h"
#include "VT/RuntimeVirtualTextureSceneProxy.h"


FRuntimeVirtualTextureFinalizer::FRuntimeVirtualTextureFinalizer(FVTProducerDescription const& InDesc, uint32 InProducerId, ERuntimeVirtualTextureMaterialType InMaterialType, FSceneInterface* InScene, FTransform const& InUVToWorld)
	: Desc(InDesc)
	, ProducerId(InProducerId)
	, RuntimeVirtualTextureMask(0)
	, MaterialType(InMaterialType)
	, Scene(InScene)
	, UVToWorld(InUVToWorld)
{
}

bool FRuntimeVirtualTextureFinalizer::IsReady()
{
	//todo[vt]: 
	// Test if we have everything we need to render (mips loaded etc).

	// Test scene is loaded and has been updated once by main rendering passes
	return Scene != nullptr && Scene->GetRenderScene() != nullptr && Scene->GetRenderScene()->GetFrameNumber() > 1;
}

void FRuntimeVirtualTextureFinalizer::InitProducer(const FVirtualTextureProducerHandle& ProducerHandle)
{
	if (RuntimeVirtualTextureMask == 0)
	{
		FScene* RenderScene = Scene->GetRenderScene();

		// Initialize the RuntimeVirtualTextureMask by matching this producer with those registered in the scene's runtime virtual textures.
		// We only need to do this once. If the associated scene proxy is removed this finalizer will also be destroyed.
		const uint32 VirtualTextureSceneIndex = RenderScene->GetRuntimeVirtualTextureSceneIndex(ProducerId);
		RuntimeVirtualTextureMask = 1 << VirtualTextureSceneIndex;

		// Store the ProducerHandle in the FRuntimeVirtualTextureSceneProxy object.
		// This is a bit of a hack: the proxy needs to know the producer handle but can't know it on proxy creation because the producer registration is deferred to the render thread.
		check(ProducerHandle.PackedValue != 0);
		RenderScene->RuntimeVirtualTextures[VirtualTextureSceneIndex]->ProducerHandle = ProducerHandle;

		//todo[vt]: 
		// Add a slow render path inside RenderPage() when this check fails. 
		// It will need to iterate the virtual textures on each primitive instead of using the RuntimeVirtualTextureMask.
		// Currently nothing will render for this finalizer when the check fails.
		checkSlow(VirtualTextureSceneIndex < FPrimitiveVirtualTextureFlags::RuntimeVirtualTexture_BitCount);
	}
}

void FRuntimeVirtualTextureFinalizer::AddTile(FTileEntry& Tile)
{
	Tiles.Add(Tile);
}

void FRuntimeVirtualTextureFinalizer::Finalize(FRHICommandListImmediate& RHICmdList)
{
	for (auto Entry : Tiles)
	{
		const int32 TileSize = Desc.TileSize + 2 * Desc.TileBorderSize;
		
		const FVector2D DestinationBoxStart0(Entry.DestX0 * TileSize, Entry.DestY0 * TileSize);
		const FBox2D DestinationBox0(DestinationBoxStart0, DestinationBoxStart0 + FVector2D(TileSize, TileSize));

		const FVector2D DestinationBoxStart1(Entry.DestX1 * TileSize, Entry.DestY1 * TileSize);
		const FBox2D DestinationBox1(DestinationBoxStart1, DestinationBoxStart1 + FVector2D(TileSize, TileSize));

		const float X = (float)FMath::ReverseMortonCode2(Entry.vAddress);
		const float Y = (float)FMath::ReverseMortonCode2(Entry.vAddress >> 1);
		const float DivisorX = (float)Desc.BlockWidthInTiles / (float)(1 << Entry.vLevel);
		const float DivisorY = (float)Desc.BlockHeightInTiles / (float)(1 << Entry.vLevel);

		const FVector2D UV(X / DivisorX, Y / DivisorY);
		const FVector2D UVSize(1.f / DivisorX, 1.f / DivisorY);
		const FVector2D UVBorder = UVSize * ((float)Desc.TileBorderSize / (float)Desc.TileSize);
		const FBox2D UVRange(UV - UVBorder, UV + UVSize + UVBorder);

		RuntimeVirtualTexture::RenderPage(
			RHICmdList,
			Scene->GetRenderScene(),
			RuntimeVirtualTextureMask,
			MaterialType,
			Entry.Texture0, 
			DestinationBox0, 
			Entry.Texture1,
			DestinationBox1,
			UVToWorld,
			UVRange, 
			Entry.vLevel,
			Desc.MaxLevel);
	}

	Tiles.SetNumUnsafeInternal(0);
}

FRuntimeVirtualTextureProducer::FRuntimeVirtualTextureProducer(FVTProducerDescription const& InDesc, uint32 InProducerId, ERuntimeVirtualTextureMaterialType InMaterialType, FSceneInterface* InScene, FTransform const& InUVToWorld)
	: Finalizer(InDesc, InProducerId, InMaterialType, InScene, InUVToWorld)
{
}

FVTRequestPageResult FRuntimeVirtualTextureProducer::RequestPageData(
	const FVirtualTextureProducerHandle& ProducerHandle,
	uint8 LayerMask,
	uint8 vLevel,
	uint32 vAddress,
	EVTRequestPagePriority Priority)
{
	//todo[vt]: 
	// Possibly throttle rendering according to performance and return Saturated here.

	FVTRequestPageResult result;
	result.Handle = 0;
	//todo[vt]:
	// Returning Saturated instead of Pending here because higher level ignores Pending for locked pages. Need to fix that...
	result.Status = Finalizer.IsReady() ? EVTRequestPageStatus::Available : EVTRequestPageStatus::Saturated;
	return result;
}

IVirtualTextureFinalizer* FRuntimeVirtualTextureProducer::ProducePageData(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	EVTProducePageFlags Flags,
	const FVirtualTextureProducerHandle& ProducerHandle,
	uint8 LayerMask,
	uint8 vLevel,
	uint32 vAddress,
	uint64 RequestHandle,
	const FVTProduceTargetLayer* TargetLayers)
{
	FRuntimeVirtualTextureFinalizer::FTileEntry Tile;
	Tile.vAddress = vAddress;
	Tile.vLevel = vLevel;

	//todo[vt]: 
	// Partial layer masks can happen when one layer has more physical space available so that old pages are evicted at different rates.
	// This can be almost always be avoided by setting up the physical pools correctly for the application's needs.
	// If we can't avoid partial layer masks then we could look at ways to handle it more efficiently (right now we render all layers even for these partial requests).

	//todo[vt]: Add support for more than two layers
	if (TargetLayers[0].TextureRHI != nullptr)
	{
		Tile.Texture0 = TargetLayers[0].TextureRHI->GetTexture2D();
		Tile.DestX0 = TargetLayers[0].pPageLocation.X;
		Tile.DestY0 = TargetLayers[0].pPageLocation.Y;
	}

	if (TargetLayers[1].TextureRHI != nullptr)
	{
		Tile.Texture1 = TargetLayers[1].TextureRHI->GetTexture2D();
		Tile.DestX1 = TargetLayers[1].pPageLocation.X;
		Tile.DestY1 = TargetLayers[1].pPageLocation.Y;
	}

	Finalizer.InitProducer(ProducerHandle);
	Finalizer.AddTile(Tile);

	return &Finalizer;
}
