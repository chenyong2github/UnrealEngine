// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTexturePoolConfig.h"

UVirtualTexturePoolConfig::UVirtualTexturePoolConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UVirtualTexturePoolConfig::~UVirtualTexturePoolConfig()
{
}

void UVirtualTexturePoolConfig::PostLoad()
{
	DefaultConfig.SizeInMegabyte = DefaultSizeInMegabyte;
}

const FVirtualTextureSpacePoolConfig *UVirtualTexturePoolConfig::FindPoolConfig(TEnumAsByte<EPixelFormat> const* Formats, int32 NumLayers, int32 TileSize)
{
	// Reverse iterate so that project config can override base config
	for (int32 Id = Pools.Num() - 1; Id >= 0 ; Id--)
	{
		const FVirtualTextureSpacePoolConfig& Config = Pools[Id];
		if (Config.MinTileSize <= TileSize && Config.MaxTileSize >= TileSize && NumLayers == Config.Formats.Num())
		{
			bool bAllFormatsMatch = true;
			for (int Layer = 0; Layer < NumLayers && bAllFormatsMatch; ++Layer)
			{
				if (Formats[Layer] != Config.Formats[Layer])
				{
					bAllFormatsMatch = false;
				}
			}

			if (bAllFormatsMatch)
			{
				return &Config;
			}
		}
	}

	DefaultConfig.SizeInMegabyte = DefaultSizeInMegabyte;
	return &DefaultConfig;
}
