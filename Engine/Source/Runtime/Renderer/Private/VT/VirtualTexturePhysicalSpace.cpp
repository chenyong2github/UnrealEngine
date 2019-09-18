// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VirtualTexturePhysicalSpace.h"
#include "VirtualTextureSystem.h"
#include "RenderTargetPool.h"
#include "VisualizeTexture.h"
#include "Stats/Stats.h"
#include "VT/VirtualTexturePoolConfig.h"

FVirtualTexturePhysicalSpace::FVirtualTexturePhysicalSpace(const FVTPhysicalSpaceDescription& InDesc, uint16 InID)
	: Description(InDesc)
	, NumRefs(0u)
	, ID(InID)
	, bPageTableLimit(false)
	, bGpuTextureLimit(false)
{
	// Find matching physical pool
	UVirtualTexturePoolConfig* PoolConfig = GetMutableDefault<UVirtualTexturePoolConfig>();
	const FVirtualTextureSpacePoolConfig* Config = PoolConfig->FindPoolConfig(InDesc.Format, InDesc.NumLayers, InDesc.TileSize);
	const uint32 PoolSizeInBytes = Config->SizeInMegabyte * 1024u * 1024u;
	const bool bForce16BitPageTable = false;

	const FPixelFormatInfo& FormatInfo = GPixelFormats[InDesc.Format[0]];
	check(InDesc.TileSize % FormatInfo.BlockSizeX == 0);
	check(InDesc.TileSize % FormatInfo.BlockSizeY == 0);
	SIZE_T TileSizeBytes = 0;
	for (int32 Layer = 0; Layer < InDesc.NumLayers; ++Layer)
	{
		TileSizeBytes += CalculateImageBytes(InDesc.TileSize, InDesc.TileSize, 0, InDesc.Format[Layer]);
	}
	const uint32 MaxTiles = (uint32)(PoolSizeInBytes / TileSizeBytes);
	
	TextureSizeInTiles = FMath::CeilToInt(FMath::Sqrt((float)MaxTiles));
	if (bForce16BitPageTable)
	{
		// 16 bit page tables support max size of 64x64 (4096 tiles)
		TextureSizeInTiles = FMath::Min(64u, TextureSizeInTiles);
	}

	if (TextureSizeInTiles * InDesc.TileSize > GetMax2DTextureDimension())
	{
		// A good option to support extremely large caches would be to allow additional slices in an array here for caches...
		// Just try to use the maximum texture size for now
		TextureSizeInTiles = GetMax2DTextureDimension() / InDesc.TileSize;
		bGpuTextureLimit = true;
	}

	Pool.Initialize(GetNumTiles());

#if STATS
	const FString LongName = FString::Printf(TEXT("WorkingSet %s %%"), FormatInfo.Name);
	WorkingSetSizeStatID = FDynamicStats::CreateStatIdDouble<STAT_GROUP_TO_FStatGroup(STATGROUP_VirtualTexturing)>(LongName);
#endif // STATS
}

FVirtualTexturePhysicalSpace::~FVirtualTexturePhysicalSpace()
{
}

void FVirtualTexturePhysicalSpace::InitRHI()
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	for (int32 Layer = 0; Layer < Description.NumLayers; ++Layer)
	{
		const uint32 TextureSize = GetTextureSize();
		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(TextureSize, TextureSize),
			Description.Format[Layer],
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_ShaderResource | (Description.bCreateRenderTarget ? (TexCreate_RenderTargetable | TexCreate_UAV) : 0),
			false);

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PooledRenderTarget[Layer], TEXT("PhysicalTexture"));
		FRHITexture* TextureRHI = PooledRenderTarget[Layer]->GetRenderTargetItem().ShaderResourceTexture;

		// Create sRGB/non-sRGB views into the physical texture
		FRHITextureSRVCreateInfo ViewInfo;
		TextureView[Layer] = RHICreateShaderResourceView(TextureRHI, ViewInfo);

		ViewInfo.SRGBOverride = SRGBO_ForceEnable;
		TextureSRGBView[Layer] = RHICreateShaderResourceView(TextureRHI, ViewInfo);
	}
}

void FVirtualTexturePhysicalSpace::ReleaseRHI()
{
	for (int32 Layer = 0; Layer < Description.NumLayers; ++Layer)
	{
		GRenderTargetPool.FreeUnusedResource(PooledRenderTarget[Layer]);
		TextureView[Layer].SafeRelease();
		TextureSRGBView[Layer].SafeRelease();
	}
}

uint32 FVirtualTexturePhysicalSpace::GetSizeInBytes() const
{
	SIZE_T TileSizeBytes = 0;
	for (int32 Layer = 0; Layer < Description.NumLayers; ++Layer)
	{
		TileSizeBytes += CalculateImageBytes(Description.TileSize, Description.TileSize, 0, Description.Format[Layer]);
	}
	return GetNumTiles() * TileSizeBytes;
}

#if STATS
void FVirtualTexturePhysicalSpace::UpdateWorkingSetStat()
{
	const double Value = (double)WorkingSetSize.GetValue() / (double)GetNumTiles() * 100.0;
	FThreadStats::AddMessage(WorkingSetSizeStatID.GetName(), EStatOperation::Set, Value);
}
#endif // STATS
