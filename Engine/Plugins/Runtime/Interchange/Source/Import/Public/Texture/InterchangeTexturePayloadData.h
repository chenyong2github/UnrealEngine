// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"

namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGEIMPORTPLUGIN_API FImportImage
		{
			TArray64<uint8> RawData;
			ETextureSourceFormat Format = TSF_Invalid;
			TextureCompressionSettings CompressionSettings = TC_Default;
			int32 NumMips = 0;
			int32 SizeX = 0;
			int32 SizeY = 0;
			bool SRGB = true;
			TOptional<TextureMipGenSettings> MipGenSettings;

			void Init2DWithParams(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, bool InSRGB);
			void Init2DWithOneMip(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, const void* InData = nullptr);
			void Init2DWithMips(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, const void* InData = nullptr);

			int64 GetMipSize(int32 InMipIndex) const;
			void* GetMipData(int32 InMipIndex);
		};

		struct INTERCHANGEIMPORTPLUGIN_API FImportImageHelper
		{
			/**
			 * Tests if the given height and width specify a supported texture resolution to import; Can optionally check if the height/width are powers of two
			 *
			 * @param Width - The width of an imported texture whose validity should be checked
			 * @param Height - The height of an imported texture whose validity should be checked
			 * @param bAllowNonPowerOfTwo - Whether or not non-power-of-two textures are allowed
			 *
			 * @return bool true if the given height/width represent a supported texture resolution, false if not
			 */
			static bool IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo);
		};
	}//ns Interchange
}//ns UE


