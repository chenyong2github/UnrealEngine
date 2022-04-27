// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureImportUtils.h"

#include "ImageCore.h"

namespace UE
{
	namespace TextureUtilitiesCommon
	{
		/**
		 * Detect the existence of gray scale image in some formats and convert those to a gray scale equivalent image
		 * 
		 * @return true if the image was converted
		 */
		bool AutoDetectAndChangeGrayScale(FImage& Image)
		{
			if (Image.Format != ERawImageFormat::BGRA8)
			{
				return false;
			}

			// auto-detect gray BGRA8 and change to G8

			const FColor* Colors = (const FColor*)Image.RawData.GetData();
			int64 NumPixels = Image.GetNumPixels();

			for (int64 i = 0; i < NumPixels; i++)
			{
				if (Colors[i].A != 255 ||
					Colors[i].R != Colors[i].B ||
					Colors[i].G != Colors[i].B)
				{
					return false ;
				}
			}

			// yes, it's gray, do it :
			Image.ChangeFormat(ERawImageFormat::G8, Image.GammaSpace);

			return true;
		}
	}
}