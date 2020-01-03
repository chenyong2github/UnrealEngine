// Copyright Epic Games, Inc. All Rights Reserved.

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
	FVirtualTextureSpacePoolConfig Config;
	UVirtualTexturePoolConfig const* PoolConfig = GetDefault<UVirtualTexturePoolConfig>();
	PoolConfig->FindPoolConfig(InDesc.Format, InDesc.NumLayers, InDesc.TileSize, Config);
	const uint32 PoolSizeInBytes = Config.SizeInMegabyte * 1024u * 1024u;

	const FPixelFormatInfo& FormatInfo = GPixelFormats[InDesc.Format[0]];
	check(InDesc.TileSize % FormatInfo.BlockSizeX == 0);
	check(InDesc.TileSize % FormatInfo.BlockSizeY == 0);
	SIZE_T TileSizeBytes = 0;
	for (int32 Layer = 0; Layer < InDesc.NumLayers; ++Layer)
	{
		TileSizeBytes += CalculateImageBytes(InDesc.TileSize, InDesc.TileSize, 0, InDesc.Format[Layer]);
	}
	const uint32 MaxTiles = FMath::Max((uint32)(PoolSizeInBytes / TileSizeBytes), 1u);
	TextureSizeInTiles = FMath::FloorToInt(FMath::Sqrt((float)MaxTiles));
	
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

EPixelFormat GetUnorderedAccessViewFormat(EPixelFormat InFormat)
{
	// Use alias formats for compressed textures on APIs where that is possible
	// This allows us to compress runtime data directly to the physical texture
	const bool bUAVAliasForCompressedTextures = GRHISupportsUAVFormatAliasing;

	switch (InFormat)
	{
	case PF_DXT1: 
	case PF_BC4: 
		return bUAVAliasForCompressedTextures ? PF_R32G32_UINT : PF_Unknown;
	case PF_DXT3: 
	case PF_DXT5: 
	case PF_BC5: 
		return bUAVAliasForCompressedTextures ? PF_R32G32B32A32_UINT : PF_Unknown;
	}

	return InFormat;
}

void FVirtualTexturePhysicalSpace::InitRHI()
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	for (int32 Layer = 0; Layer < Description.NumLayers; ++Layer)
	{
		const EPixelFormat FormatSRV = Description.Format[Layer];
		const EPixelFormat FormatUAV = GetUnorderedAccessViewFormat(FormatSRV);
		const bool bCreateAliasedUAV = (FormatUAV != PF_Unknown) && (FormatUAV != FormatSRV);

		// Allocate physical texture from the render target pool
		const uint32 TextureSize = GetTextureSize();
		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(TextureSize, TextureSize),
			FormatSRV,
			FClearValueBinding::None,
			TexCreate_None,
			bCreateAliasedUAV ? TexCreate_ShaderResource | TexCreate_UAV : TexCreate_ShaderResource,
			false);

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PooledRenderTarget[Layer], TEXT("PhysicalTexture"));
		FRHITexture* TextureRHI = PooledRenderTarget[Layer]->GetRenderTargetItem().ShaderResourceTexture;

		// Create sRGB and non-sRGB shader resource views into the physical texture
		FRHITextureSRVCreateInfo SRVCreateInfo;
		SRVCreateInfo.Format = FormatSRV;
		TextureSRV[Layer] = RHICreateShaderResourceView(TextureRHI, SRVCreateInfo);

		SRVCreateInfo.SRGBOverride = SRGBO_ForceEnable;
		TextureSRV_SRGB[Layer] = RHICreateShaderResourceView(TextureRHI, SRVCreateInfo);

		if (bCreateAliasedUAV)
		{
			TextureUAV[Layer] = RHICreateUnorderedAccessView(TextureRHI, 0, FormatUAV);
		}
	}
}

void FVirtualTexturePhysicalSpace::ReleaseRHI()
{
	for (int32 Layer = 0; Layer < Description.NumLayers; ++Layer)
	{
		GRenderTargetPool.FreeUnusedResource(PooledRenderTarget[Layer]);
		TextureSRV[Layer].SafeRelease();
		TextureSRV_SRGB[Layer].SafeRelease();
		TextureUAV[Layer].SafeRelease();
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
