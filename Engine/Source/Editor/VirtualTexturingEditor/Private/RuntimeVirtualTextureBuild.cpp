// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureBuild.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/ScopedSlowTask.h"
#include "RendererInterface.h"
#include "RenderTargetPool.h"
#include "SceneInterface.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureRender.h"

namespace
{
	/** Container for render resources needed to render the runtime virtual texture. */
	class FRenderTileResources : public FRenderResource
	{
	public:
		FRenderTileResources(int32 InNumLayers, int32 InTileSize, EPixelFormat InFormat)
			: NumLayers(InNumLayers)
			, TileSize(InTileSize)
			, Format(InFormat)
		{
		}

		//~ Begin FRenderResource Interface.
		virtual void InitRHI() override
		{
			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

			RenderTargets.Init(nullptr, NumLayers);
			StagingTextures.Init(nullptr, NumLayers);

			for (int32 Layer = 0; Layer < NumLayers; ++Layer)
			{
				FRHIResourceCreateInfo CreateInfo;
				RenderTargets[Layer] = RHICmdList.CreateTexture2D(TileSize, TileSize, Format, 1, 1, TexCreate_RenderTargetable, CreateInfo);
				StagingTextures[Layer] = RHICmdList.CreateTexture2D(TileSize, TileSize, Format, 1, 1, TexCreate_CPUReadback, CreateInfo);
			}

			Fence = RHICmdList.CreateGPUFence(TEXT("Runtime Virtual Texture Build"));
		}

		virtual void ReleaseRHI() override
		{
			RenderTargets.Empty();
			StagingTextures.Empty();
			Fence.SafeRelease();
		}
		//~ End FRenderResource Interface.

		FRHITexture2D* GetRenderTarget(int32 Index) const { return Index < NumLayers ? RenderTargets[Index] : nullptr; }
		FRHITexture2D* GetStagingTexture(int32 Index) const { return Index < NumLayers ? StagingTextures[Index] : nullptr; }
		FRHIGPUFence* GetFence() const { return Fence; }

	private:
		int32 NumLayers;
		int32 TileSize;
		EPixelFormat Format;

		TArray<FTexture2DRHIRef> RenderTargets;
		TArray<FTexture2DRHIRef> StagingTextures;
		FGPUFenceRHIRef Fence;
	};

	/** Templatized helper function for copying a rendered tile to the final composited image data. */
	template<typename T>
	void TCopyTile(T* TilePixels, int32 TileSize, T* DestPixels, int32 DestStride, int32 DestLayerStride, FIntVector const& DestPos)
	{
		for (int32 y = 0; y < TileSize; y++)
		{
			memcpy(
				DestPixels + DestLayerStride * DestPos[2] + DestStride * (DestPos[1] + y) + DestPos[0],
				TilePixels + TileSize * y,
				TileSize * sizeof(T));
		}
	}

	/** Function for copying a rendered tile to the final composited image data. Needs ERuntimeVirtualTextureMaterialType to know what type of data is being copied. */
	void CopyTile(void* TilePixels, int32 TileSize, void* DestPixels, int32 DestStride, int32 DestLayerStride, FIntVector const& DestPos, ERuntimeVirtualTextureMaterialType MaterialType)
	{
		if (MaterialType == ERuntimeVirtualTextureMaterialType::WorldHeight)
		{
			TCopyTile((uint16*)TilePixels, TileSize, (uint16*)DestPixels, DestStride, DestLayerStride, DestPos);
		}
		else
		{
			TCopyTile((FColor*)TilePixels, TileSize, (FColor*)DestPixels, DestStride, DestLayerStride, DestPos);
		}
	}
}


namespace RuntimeVirtualTexture
{
	bool HasStreamedMips(URuntimeVirtualTextureComponent* InComponent)
	{
		if (InComponent == nullptr)
		{
			return false;
		}

		URuntimeVirtualTexture* RuntimeVirtualTexture = InComponent->GetVirtualTexture();
		if (RuntimeVirtualTexture == nullptr)
		{
			return false;
		}

		if (RuntimeVirtualTexture->GetStreamLowMips() <= 0)
		{
			return false;
		}

		return true;
	}

	bool BuildStreamedMips(URuntimeVirtualTextureComponent* InComponent, ERuntimeVirtualTextureDebugType DebugType)
	{
		if (!HasStreamedMips(InComponent))
		{
			return true;
		}

		URuntimeVirtualTexture* RuntimeVirtualTexture = InComponent->GetVirtualTexture();
		FSceneInterface* Scene = InComponent->GetScene();
		const uint32 VirtualTextureSceneIndex = RuntimeVirtualTexture::GetRuntimeVirtualTextureSceneIndex_GameThread(InComponent);
		const FTransform Transform = InComponent->GetVirtualTextureTransform();

		FVTProducerDescription VTDesc;
		RuntimeVirtualTexture->GetProducerDescription(VTDesc, Transform);

		const int32 TileSize = VTDesc.TileSize;
		const int32 TileBorderSize = VTDesc.TileBorderSize;
		const int32 TextureSizeX = VTDesc.WidthInBlocks * VTDesc.BlockWidthInTiles * TileSize;
		const int32 TextureSizeY = VTDesc.HeightInBlocks * VTDesc.BlockHeightInTiles * TileSize;
		const int32 MaxLevel = (int32)FMath::CeilLogTwo(FMath::Max(VTDesc.BlockWidthInTiles, VTDesc.BlockHeightInTiles));
		const int32 RenderLevel = FMath::Max(MaxLevel - RuntimeVirtualTexture->GetStreamLowMips() + 1, 0);
		const int32 ImageSizeX = FMath::Max(TileSize, TextureSizeX >> RenderLevel);
		const int32 ImageSizeY = FMath::Max(TileSize, TextureSizeY >> RenderLevel);
		const int32 NumTilesX = ImageSizeX / TileSize;
		const int32 NumTilesY = ImageSizeY / TileSize;
		const int32 NumLayers = RuntimeVirtualTexture->GetLayerCount();

		const ERuntimeVirtualTextureMaterialType MaterialType = RuntimeVirtualTexture->GetMaterialType();
		const EPixelFormat RenderTargetFormat = (MaterialType == ERuntimeVirtualTextureMaterialType::WorldHeight) ? PF_G16 : PF_B8G8R8A8;
		const int32 BytesPerPixel = (MaterialType == ERuntimeVirtualTextureMaterialType::WorldHeight) ? 2 : 4;

		// Spin up slow task UI
		const float TaskWorkRender = NumTilesX * NumTilesY;
		const float TaskWorkBuildBulkData = NumTilesX * NumTilesY / 2;
		FScopedSlowTask Task(TaskWorkRender + TaskWorkBuildBulkData, FText::AsCultureInvariant(RuntimeVirtualTexture->GetName()));
		Task.MakeDialog(true);

		// Allocate render targets for rendering out the runtime virtual texture tiles
		FRenderTileResources RenderTileResources(NumLayers, TileSize, RenderTargetFormat);
		BeginInitResource(&RenderTileResources);

		// Final pixels will contain image data for each virtual texture layer in order
		TArray64<uint8> FinalPixels;
		FinalPixels.InsertUninitialized(0, ImageSizeX * ImageSizeY * NumLayers * BytesPerPixel);

		// Iterate over all tiles and render/store each one to the final image
		for (int32 TileY = 0; TileY < NumTilesY && !Task.ShouldCancel(); TileY++)
		{
			for (int32 TileX = 0; TileX < NumTilesX && !Task.ShouldCancel(); TileX++)
			{
				Task.EnterProgressFrame();

				// Render tile
				const FBox2D UVRange = FBox2D(
					FVector2D((float)TileX / (float)NumTilesX, (float)TileY / (float)NumTilesY),
					FVector2D((float)(TileX + 1) / (float)NumTilesX, (float)(TileY + 1) / (float)NumTilesY));

				ENQUEUE_RENDER_COMMAND(BakeStreamingTextureTileCommand)([
					Scene, VirtualTextureSceneIndex, 
					&RenderTileResources,
					MaterialType, NumLayers,
					Transform, UVRange,
					RenderLevel, MaxLevel, 
					TileX, TileY,
					TileSize, ImageSizeX, ImageSizeY, 
					&FinalPixels,
					DebugType
				](FRHICommandListImmediate& RHICmdList)
				{
					const FBox2D TileBox(FVector2D(0, 0), FVector2D(TileSize, TileSize));
					const FIntRect TileRect(0, 0, TileSize, TileSize);

					// Transition render targets for writing
					for (int32 Layer = 0; Layer < NumLayers; Layer++)
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RenderTileResources.GetRenderTarget(Layer));
					}

					RuntimeVirtualTexture::FRenderPageBatchDesc Desc;
					Desc.Scene = Scene->GetRenderScene();
					Desc.RuntimeVirtualTextureMask = 1 << VirtualTextureSceneIndex;
					Desc.UVToWorld = Transform;
					Desc.MaterialType = MaterialType;
					Desc.MaxLevel = MaxLevel;
					Desc.bClearTextures = true;
					Desc.bIsThumbnails = false;
					Desc.DebugType = DebugType;
					Desc.NumPageDescs = 1;
					Desc.Targets[0].Texture = RenderTileResources.GetRenderTarget(0);
					Desc.Targets[1].Texture = RenderTileResources.GetRenderTarget(1);
					Desc.Targets[2].Texture = RenderTileResources.GetRenderTarget(2);
					Desc.PageDescs[0].DestBox[0] = TileBox;
					Desc.PageDescs[0].DestBox[1] = TileBox;
					Desc.PageDescs[0].DestBox[2] = TileBox;
					Desc.PageDescs[0].UVRange = UVRange;
					Desc.PageDescs[0].vLevel = RenderLevel;

					RuntimeVirtualTexture::RenderPages(RHICmdList, Desc);

					// Transition render targets for copying
					for (int32 Layer = 0; Layer < NumLayers; Layer++)
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTileResources.GetRenderTarget(Layer));
					}

					// Copy to staging
					for (int32 Layer = 0; Layer < NumLayers; Layer++)
					{
						RHICmdList.CopyTexture(RenderTileResources.GetRenderTarget(Layer), RenderTileResources.GetStagingTexture(Layer), FRHICopyTextureInfo());
					}

					//todo[vt]: Insert fence for immediate read back. But is there no API to wait on it?
					RHICmdList.WriteGPUFence(RenderTileResources.GetFence());
					RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

					// Read back tile data and copy into final destination
					for (int32 Layer = 0; Layer < NumLayers; Layer++)
					{
						void* TilePixels = nullptr;
						int32 OutWidth, OutHeight;
						RHICmdList.MapStagingSurface(RenderTileResources.GetStagingTexture(Layer), TilePixels, OutWidth, OutHeight);
						check(TilePixels != nullptr);
						check(OutWidth == TileSize && OutHeight == TileSize);

						const FIntVector DestPos(TileX * TileSize, TileY * TileSize, Layer);
						CopyTile(TilePixels, TileSize, FinalPixels.GetData(), ImageSizeX, ImageSizeX * ImageSizeY, DestPos, MaterialType);

						RHICmdList.UnmapStagingSurface(RenderTileResources.GetStagingTexture(Layer));
					}
				});
			}
		}

		ReleaseResourceAndFlush(&RenderTileResources);

		if (Task.ShouldCancel())
		{
			return false;
		}

		// Place final pixel data into the runtime virtual texture
		Task.EnterProgressFrame(TaskWorkBuildBulkData);
		RuntimeVirtualTexture->Modify();
		RuntimeVirtualTexture->InitializeStreamingTexture(ImageSizeX, ImageSizeY, (uint8*)FinalPixels.GetData());
		RuntimeVirtualTexture->PostEditChange();

		return true;
	}
}
