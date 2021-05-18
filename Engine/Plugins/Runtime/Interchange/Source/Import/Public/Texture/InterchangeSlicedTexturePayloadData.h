// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"

namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGEIMPORT_API FImportSlicedImage
		{
			TArray64<uint8> RawData;
			ETextureSourceFormat Format = TSF_Invalid;
			TextureCompressionSettings CompressionSettings = TC_Default;
			int32 NumMips = 0;
			int32 SizeX = 0;
			int32 SizeY = 0;
			int32 NumSlice = 0;
			bool bSRGB = true;
			TOptional<TextureMipGenSettings> MipGenSettings;

			void Init(int32 InSizeX, int32 InSizeY, int32 InNumSlice, int32 InNumMips, ETextureSourceFormat InFormat , bool InSRGB);

			/** Return the size of a mip for one slice */
			int64 GetMipSize(int32 InMipIndex) const;

			const uint8* GetMipData(int32 InMipIndex, int32 InSliceIndex = INDEX_NONE) const;
			uint8* GetMipData(int32 InMipIndex, int32 InSliceIndex = INDEX_NONE);
		};
	}
}
