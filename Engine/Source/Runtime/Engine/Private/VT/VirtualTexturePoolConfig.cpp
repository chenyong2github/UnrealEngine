// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTexturePoolConfig.h"

#include "VT/VirtualTextureScalability.h"

UVirtualTexturePoolConfig::UVirtualTexturePoolConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVirtualTexturePoolConfig::FindPoolConfig(TEnumAsByte<EPixelFormat> const* InFormats, int32 InNumLayers, int32 InTileSize, FVirtualTextureSpacePoolConfig& OutConfig) const
{
	bool bFoundDefaultConfig = false;
	FVirtualTextureSpacePoolConfig DefaultConfig;
	DefaultConfig.SizeInMegabyte = DefaultSizeInMegabyte;

	// Reverse iterate so that project config can override base config
	for (int32 Id = Pools.Num() - 1; Id >= 0 ; Id--)
	{
		const FVirtualTextureSpacePoolConfig& Config = Pools[Id];
		if (Config.MinTileSize <= InTileSize && (Config.MaxTileSize == 0 || Config.MaxTileSize >= InTileSize) && InNumLayers == Config.Formats.Num())
		{
			bool bAllFormatsMatch = true;
			for (int Layer = 0; Layer < InNumLayers && bAllFormatsMatch; ++Layer)
			{
				if (InFormats[Layer] != Config.Formats[Layer])
				{
					bAllFormatsMatch = false;
				}
			}

			if (bAllFormatsMatch)
			{
				OutConfig = Config;
				const float Scale = Config.bAllowSizeScale ? VirtualTextureScalability::GetPoolSizeScale(Config.ScalabilityGroup) : 1.f;
				OutConfig.SizeInMegabyte = (int32)(Scale * (float)OutConfig.SizeInMegabyte);
				if (Scale < 1.f && OutConfig.MinScaledSizeInMegabyte > 0)
				{
					OutConfig.SizeInMegabyte = FMath::Max(OutConfig.SizeInMegabyte, OutConfig.MinScaledSizeInMegabyte);
				}
				if (Scale > 1.f && OutConfig.MaxScaledSizeInMegabyte > 0)
				{
					OutConfig.SizeInMegabyte = FMath::Min(OutConfig.SizeInMegabyte, OutConfig.MaxScaledSizeInMegabyte);
				}
				return;
			}
		}

		if (!bFoundDefaultConfig && Config.IsDefault())
		{
			DefaultConfig = Config;
			bFoundDefaultConfig = true;
		}
	}

	OutConfig = DefaultConfig;
}
