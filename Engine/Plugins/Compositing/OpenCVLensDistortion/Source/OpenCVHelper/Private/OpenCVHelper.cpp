// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenCVHelper.h"

#if WITH_OPENCV

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

OPENCV_INCLUDES_START
#undef check 
#include "opencv2/opencv.hpp"
OPENCV_INCLUDES_END


UTexture2D* FOpenCVHelper::TextureFromCvMat(cv::Mat& Mat, const FString* PackagePath, const FName* TextureName)
{
	// Currently we only support the pixel format below
	if (Mat.depth() != CV_8U)
	{
		return nullptr;
	}

	EPixelFormat PixelFormat;

	switch (Mat.channels())
	{
	case 1:
		PixelFormat = PF_G8;
		break;

	case 4:
		PixelFormat = PF_B8G8R8A8;
		break;

	default:
		return nullptr;
	}

	UTexture2D* Texture = nullptr;

	if (PackagePath && TextureName)
	{
		do
		{
			if ((Mat.cols <= 0) || (Mat.rows <= 0)
				|| (Mat.cols % GPixelFormats[PixelFormat].BlockSizeX)
				|| (Mat.rows % GPixelFormats[PixelFormat].BlockSizeY))
			{
				break;
			}

			Texture = NewObject<UTexture2D>(CreatePackage(**PackagePath), *TextureName, RF_Standalone | RF_Public);

			if (!Texture)
			{
				break;
			}

			Texture->PlatformData = new FTexturePlatformData();
			Texture->PlatformData->SizeX = Mat.cols;
			Texture->PlatformData->SizeY = Mat.rows;
			Texture->PlatformData->PixelFormat = PixelFormat;

			// Allocate first mipmap.
			const int32 NumBlocksX = Mat.cols / GPixelFormats[PixelFormat].BlockSizeX;
			const int32 NumBlocksY = Mat.rows / GPixelFormats[PixelFormat].BlockSizeY;

			FTexture2DMipMap* Mip = new FTexture2DMipMap();

			Texture->PlatformData->Mips.Add(Mip);

			Mip->SizeX = Mat.cols;
			Mip->SizeY = Mat.rows;
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			Mip->BulkData.Realloc(NumBlocksX * NumBlocksY * GPixelFormats[PixelFormat].BlockBytes);
			Mip->BulkData.Unlock();

		} while (false);
	}
	else
	{
		Texture = UTexture2D::CreateTransient(Mat.cols, Mat.rows, PixelFormat);
	}

	if (!Texture)
	{
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
	Texture->NeverStream = true;
	Texture->SRGB = 0;

	if (Mat.channels() == 1)
	{
		Texture->CompressionSettings = TextureCompressionSettings::TC_Grayscale;
#if WITH_EDITORONLY_DATA
		Texture->CompressionNoAlpha = true;
#endif
	}

	FTexture2DMipMap& Mip0 = Texture->PlatformData->Mips[0];
	void* TextureData = Mip0.BulkData.Lock(LOCK_READ_WRITE);

	const int32 PixelStride = Mat.channels();
	FMemory::Memcpy(TextureData, Mat.data, SIZE_T(Mat.cols * Mat.rows * PixelStride));

	Mip0.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

#endif // WITH_OPENCV
