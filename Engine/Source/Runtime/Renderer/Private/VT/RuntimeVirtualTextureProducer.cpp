// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureProducer.h"

#include "RendererInterface.h"
#include "RuntimeVirtualTextureRender.h"
#include "ScenePrivate.h"

FRuntimeVirtualTextureFinalizer::FRuntimeVirtualTextureFinalizer(FVTProducerDescription const& InDesc, ERuntimeVirtualTextureMaterialType InMaterialType, FSceneInterface* InScene, FTransform const& InUVToWorld)
	: Desc(InDesc)
	, MaterialType(InMaterialType)
	, Scene(InScene)
	, UVToWorld(InUVToWorld)
{
}

bool FRuntimeVirtualTextureFinalizer::IsReady()
{
	//todo[vt]: 
	// Test if we have everything we need to render (shaders loaded etc).
	// Current test for GPUScene.PrimitiveBuffer is a nasty thing to prevent a checkf triggering if no PrimitiveBuffer is bound. It feels like it requires too much knowledge of the renderer internals...
	return Scene != nullptr && Scene->GetRenderScene() != nullptr && Scene->GetRenderScene()->GPUScene.PrimitiveBuffer.Buffer != nullptr;
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

		const uint32 X = FMath::ReverseMortonCode2(Entry.vAddress);
		const uint32 Y = FMath::ReverseMortonCode2(Entry.vAddress >> 1);
		const uint32 DivisorX = Desc.BlockWidthInTiles >> Entry.vLevel;
		const uint32 DivisorY = Desc.BlockHeightInTiles >> Entry.vLevel;

		const FVector2D UV((float)X / (float)DivisorX, (float)Y / (float)DivisorY);
		const FVector2D UVSize(1.f / (float)DivisorX, 1.f / (float)DivisorY);
		const FVector2D UVBorder = UVSize * ((float)Desc.TileBorderSize / (float)Desc.TileSize);
		const FBox2D UVRange(UV - UVBorder, UV + UVSize + UVBorder);

		RuntimeVirtualTexture::RenderPage(
			RHICmdList, 
			Scene->GetRenderScene(),
			MaterialType,
			Entry.Texture0, 
			DestinationBox0, 
			Entry.Texture1,
			DestinationBox1,
			UVToWorld,
			UVRange);
	}

	Tiles.SetNumUnsafeInternal(0);
}

FRuntimeVirtualTextureProducer::FRuntimeVirtualTextureProducer(FVTProducerDescription const& InDesc, ERuntimeVirtualTextureMaterialType InMaterialType, FSceneInterface* InScene, FTransform const& InUVToWorld)
	: Finalizer(InDesc, InMaterialType, InScene, InUVToWorld)
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
	if (LayerMask & 1)
	{
		Tile.Texture0 = TargetLayers[0].TextureRHI->GetTexture2D();
		Tile.DestX0 = TargetLayers[0].pPageLocation.X;
		Tile.DestY0 = TargetLayers[0].pPageLocation.Y;
	}

	if (LayerMask & 2)
	{
		Tile.Texture1 = TargetLayers[1].TextureRHI->GetTexture2D();
		Tile.DestX1 = TargetLayers[1].pPageLocation.X;
		Tile.DestY1 = TargetLayers[1].pPageLocation.Y;
	}

	Finalizer.AddTile(Tile);

	return &Finalizer;
}
