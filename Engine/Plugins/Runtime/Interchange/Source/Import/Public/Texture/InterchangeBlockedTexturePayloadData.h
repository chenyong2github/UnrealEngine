// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Texture/InterchangeTexturePayloadData.h"

#include "CoreMinimal.h"
#include "Engine/Texture.h"

namespace UE
{
	namespace Interchange
	{
		// Also known as a UDIMs texture
		struct FImportBlockedImage
		{
			// The first image data will be used for the default compression setting 
			// The first image will also be use for the pixel format and gamma (bsRGB)

			TArray<FTextureSourceBlock> BlocksData;
			TArray<FImportImage> ImagesData;

			void InitZeroed(uint32 Count)
			{
				BlocksData.AddZeroed(Count);
				ImagesData.AddZeroed(Count);
			}

			void RemoveBlock(int32 Index)
			{
				BlocksData.RemoveAtSwap(Index);
				ImagesData.RemoveAtSwap(Index);
			}

			bool HasData() const
			{
				return ImagesData.Num() > 0;
			}

			bool IsBlockedData() const
			{
				return BlocksData.Num() > 0;
			}
		};
	}
}