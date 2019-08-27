// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureBuild.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/ScopedSlowTask.h"
#include "SceneInterface.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureRender.h"

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
		const int32 RenderLevel = FMath::Max(MaxLevel - RuntimeVirtualTexture->GetStreamLowMips() + 1, 1);
		const int32 StreamingTextureSizeX = FMath::Max(TileSize, TextureSizeX >> RenderLevel);
		const int32 StreamingTextureSizeY = FMath::Max(TileSize, TextureSizeY >> RenderLevel);
		const int32 NumTilesX = StreamingTextureSizeX / TileSize;
		const int32 NumTilesY = StreamingTextureSizeY / TileSize;
		const int32 LayerCount = RuntimeVirtualTexture->GetLayerCount();

		//todo[vt]: Support streaming VT for ERuntimeVirtualTextureMaterialType::WorldHeight (which requires UTextureRenderTarget2D support for PF_G16)
		const ERuntimeVirtualTextureMaterialType MaterialType = RuntimeVirtualTexture->GetMaterialType();
		if (MaterialType == ERuntimeVirtualTextureMaterialType::WorldHeight)
		{
			return false;
		}

		// Spin up slow task UI
		const float TaskWorkRender = NumTilesX * NumTilesY;
		const float TaskWorkBuildBulkData = NumTilesX * NumTilesY / 2;
		FScopedSlowTask Task(TaskWorkRender + TaskWorkBuildBulkData, FText::AsCultureInvariant(RuntimeVirtualTexture->GetName()));
		Task.MakeDialog(true);

		// Final pixels will contain data for each virtual texture layer in order
		TArray64<FColor> FinalPixels;
		FinalPixels.InsertUninitialized(0, StreamingTextureSizeX * StreamingTextureSizeY * LayerCount);

		// Allocate render targets for rendering out the runtime virtual texture tiles
		UTextureRenderTarget2D* RenderTarget[2] = { nullptr };
		FRenderTarget* RenderTargetResource[2] = { nullptr };

		for (int32 Layer = 0; Layer < LayerCount; Layer++)
		{
			RenderTarget[Layer] = NewObject<UTextureRenderTarget2D>();
			RenderTarget[Layer]->AddToRoot();
			RenderTarget[Layer]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			RenderTarget[Layer]->InitCustomFormat(TileSize, TileSize, PF_B8G8R8A8, false);
			RenderTargetResource[Layer] = RenderTarget[Layer]->GameThread_GetRenderTargetResource();
		}

		// Run over all tiles and render/store each one
		for (int32 Y = 0; Y < NumTilesY && !Task.ShouldCancel(); Y++)
		{
			for (int32 X = 0; X < NumTilesX && !Task.ShouldCancel(); X++)
			{
				Task.EnterProgressFrame();

				// Render tile
				const FBox2D TileBox = FBox2D(FVector2D(0, 0), FVector2D(TileSize, TileSize));
				const FBox2D UVRange = FBox2D(
					FVector2D((float)X / (float)NumTilesX, (float)Y / (float)NumTilesY),
					FVector2D((float)(X + 1) / (float)NumTilesX, (float)(Y + 1) / (float)NumTilesY));

				ENQUEUE_RENDER_COMMAND(BakeStreamingTextureTileCommand)(
					[Scene, VirtualTextureSceneIndex, MaterialType, RenderTargetResource, TileBox, Transform, UVRange, RenderLevel, MaxLevel, DebugType](FRHICommandListImmediate& RHICmdList)
				{
					RuntimeVirtualTexture::RenderPage(
						RHICmdList,
						Scene->GetRenderScene(),
						1 << VirtualTextureSceneIndex,
						MaterialType,
						RenderTargetResource[0] != nullptr ? RenderTargetResource[0]->GetRenderTargetTexture() : nullptr, TileBox,
						RenderTargetResource[1] != nullptr ? RenderTargetResource[1]->GetRenderTargetTexture() : nullptr, TileBox,
						Transform,
						UVRange,
						RenderLevel,
						MaxLevel,
						DebugType);
				});

				FlushRenderingCommands();

				// Read back into final pixel data
				for (int32 Layer = 0; Layer < LayerCount; Layer++)
				{
					TArray<FColor> TilePixels;
					RenderTarget[Layer]->GameThread_GetRenderTargetResource()->ReadPixels(TilePixels);

					for (int32 y = 0; y < TileSize; y++)
					{
						for (int32 x = 0; x < TileSize; x++)
						{
							FinalPixels[Layer * StreamingTextureSizeX * StreamingTextureSizeY + (Y * TileSize + y) * StreamingTextureSizeX + (X * TileSize + x)] = TilePixels[y * TileSize + x];
						}
					}
				}
			}
		}

		for (int32 Layer = 0; Layer < LayerCount; Layer++)
		{
			RenderTarget[Layer]->RemoveFromRoot();
		}

		if (Task.ShouldCancel())
		{
			return false;
		}

		// Place final pixel data into the runtime virtual texture
		Task.EnterProgressFrame(TaskWorkBuildBulkData);
		RuntimeVirtualTexture->Modify();
		RuntimeVirtualTexture->InitializeStreamingTexture(StreamingTextureSizeX, StreamingTextureSizeY, (uint8*)FinalPixels.GetData());
		RuntimeVirtualTexture->PostEditChange();

		return true;
	}
}
