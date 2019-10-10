// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTexturePoolConfig.h"

#include "VT/VirtualTextureScalability.h"

UVirtualTexturePoolConfig::UVirtualTexturePoolConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVirtualTexturePoolConfig::FindPoolConfig(TEnumAsByte<EPixelFormat> const* InFormats, int32 InNumLayers, int32 InTileSize, FVirtualTextureSpacePoolConfig& OutConfig) const
{
	// Reverse iterate so that project config can override base config
	for (int32 Id = Pools.Num() - 1; Id >= 0 ; Id--)
	{
		const FVirtualTextureSpacePoolConfig& Config = Pools[Id];
		if (Config.MinTileSize <= InTileSize && Config.MaxTileSize >= InTileSize && InNumLayers == Config.Formats.Num())
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
				const float Scale = Config.bAllowSizeScale ? VirtualTextureScalability::GetPoolSizeScale() : 1.f;
				OutConfig.SizeInMegabyte = (int32)(Scale * (float)OutConfig.SizeInMegabyte);
				return;
			}
		}
	}

	OutConfig = FVirtualTextureSpacePoolConfig();
	OutConfig.SizeInMegabyte = DefaultSizeInMegabyte;
}
