// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFImageBuilder.h"
#include "Builders/GLTFBuilderUtility.h"
#include "IImageWrapper.h"
#include "Engine/Texture.h"

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const void* RawData, int64 ByteLength, int32 Width, int32 Height, ERGBFormat RawFormat, int32 BitDepth, bool bFloatFormat, const FString& Name, EGLTFJsonMimeType MimeType, int32 Quality)
{
	TArray64<uint8> ImageData;

	if (bFloatFormat)
	{
		// TODO: implement support
		return FGLTFJsonImageIndex(INDEX_NONE);
	}
	else
	{
		switch (MimeType)
		{
		case EGLTFJsonMimeType::PNG:
			if (!FGLTFBuilderUtility::CompressImage(RawData, ByteLength, Width, Height, RawFormat, BitDepth, ImageData, EImageFormat::PNG, Quality))
			{
				return FGLTFJsonImageIndex(INDEX_NONE);
			}
			break;
		case EGLTFJsonMimeType::JPEG:
			if (!FGLTFBuilderUtility::CompressImage(RawData, ByteLength, Width, Height, RawFormat, BitDepth, ImageData, EImageFormat::JPEG, Quality))
			{
				return FGLTFJsonImageIndex(INDEX_NONE);
			}
			break;
		default:
			// TODO: report error
			return FGLTFJsonImageIndex(INDEX_NONE);
		}
	}

	FGLTFJsonImage Image;
	Image.Name = Name;
	Image.MimeType = MimeType;

	const FGLTFJsonImageIndex ImageIndex = FGLTFJsonBuilder::AddImage(Image);
	ImageDataLookup.Add(ImageIndex, ImageData);
	return ImageIndex;
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const TArray<FColor>& Pixels, int32 Width, int32 Height, EPixelFormat PixelFormat, const FString& Name, EGLTFJsonMimeType MimeType, int32 Quality)
{
	ERGBFormat RawFormat;
	int32 BitDepth;
	bool bFloatFormat;

	switch (PixelFormat)
	{
		case PF_B8G8R8A8: RawFormat = ERGBFormat::BGRA; BitDepth = 8;  bFloatFormat = false; break;
		case PF_R8G8B8A8: RawFormat = ERGBFormat::RGBA; BitDepth = 8;  bFloatFormat = false; break;
		case PF_G8:       RawFormat = ERGBFormat::Gray; BitDepth = 8;  bFloatFormat = false; break;
		default:
			// TODO: report error
			return FGLTFJsonImageIndex(INDEX_NONE);
	}

	return AddImage(Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize(), Width, Height, RawFormat, BitDepth, bFloatFormat, Name, MimeType, Quality);
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const FTextureSource& Image, const FString& Name, EGLTFJsonMimeType MimeType, int32 Quality)
{
	// TODO: are these always zero?
	const int32 BlockIndex = 0;
	const int32 LayerIndex = 0;
	const int32 MipIndex = 0;

	ERGBFormat RawFormat;
	int32 BitDepth;
	bool bFloatFormat;

	const ETextureSourceFormat SourceFormat = Image.GetFormat(LayerIndex);
	switch (SourceFormat)
	{
		case TSF_BGRA8:   RawFormat = ERGBFormat::BGRA; BitDepth = 8;  bFloatFormat = false; break;
		case TSF_RGBA8:   RawFormat = ERGBFormat::RGBA; BitDepth = 8;  bFloatFormat = false; break;
		case TSF_BGRE8:   RawFormat = ERGBFormat::BGRA; BitDepth = 8;  bFloatFormat = false; break;
		case TSF_RGBE8:   RawFormat = ERGBFormat::RGBA; BitDepth = 8;  bFloatFormat = false; break;
		case TSF_RGBA16:  RawFormat = ERGBFormat::RGBA; BitDepth = 16; bFloatFormat = false; break;
		case TSF_RGBA16F: RawFormat = ERGBFormat::RGBA; BitDepth = 16; bFloatFormat = true;  break;
		case TSF_G8:      RawFormat = ERGBFormat::Gray; BitDepth = 8;  bFloatFormat = false; break;
		case TSF_G16:     RawFormat = ERGBFormat::Gray; BitDepth = 16; bFloatFormat = false; break;
		default:
			// TODO: report error
			return FGLTFJsonImageIndex(INDEX_NONE);
	}

	TArray64<uint8> RawData;
	if (!const_cast<FTextureSource&>(Image).GetMipData(RawData, BlockIndex, LayerIndex, MipIndex))
	{
		// TODO: report error
		return FGLTFJsonImageIndex(INDEX_NONE);
	}

	return AddImage(RawData.GetData(), RawData.Num(), Image.GetSizeX(), Image.GetSizeY(), RawFormat, BitDepth, bFloatFormat, Name, MimeType, Quality);
}

bool FGLTFImageBuilder::Serialize(FArchive& Archive, const FString& FilePath)
{
	const FString ImageDir = FPaths::GetPath(FilePath);

	for (const auto& DataPair : ImageDataLookup)
	{
		const TArray64<uint8>& ImageData = DataPair.Value;
		FGLTFJsonImage& JsonImage = JsonRoot.Images[DataPair.Key];

		const FString ImageUri = JsonImage.Name + FGLTFBuilderUtility::GetFileExtension(JsonImage.MimeType);
		const FString ImagePath = FPaths::Combine(ImageDir, ImageUri);

		if (!FFileHelper::SaveArrayToFile(ImageData, *ImagePath))
		{
			// TODO: report error
			continue;
		}

		JsonImage.Uri = ImageUri;
		JsonImage.Name.Empty(); // URI already contains name
		JsonImage.MimeType = EGLTFJsonMimeType::None; // Not required if external file
	}

	return FGLTFBufferBuilder::Serialize(Archive, FilePath);
}
