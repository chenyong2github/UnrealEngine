// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFImageConverters.h"
#include "Converters/GLTFImageUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Misc/FileHelper.h"

FGLTFJsonImageIndex FGLTFImageConverter::Convert(TGLTFSuperfluous<FString> Name, EGLTFTextureType Type, bool bIgnoreAlpha, FIntPoint Size, TGLTFSharedArray<FColor> Pixels)
{
	TArray64<uint8> CompressedData;

	const EGLTFJsonMimeType MimeType = GetMimeType(Pixels->GetData(), Size, bIgnoreAlpha, Type);
	switch (MimeType)
	{
		case EGLTFJsonMimeType::None:
			return FGLTFJsonImageIndex(INDEX_NONE);

		case EGLTFJsonMimeType::PNG:
			FGLTFImageUtility::CompressToPNG(Pixels->GetData(), Size, CompressedData);
			break;

		case EGLTFJsonMimeType::JPEG:
			FGLTFImageUtility::CompressToJPEG(Pixels->GetData(), Size, Builder.ExportOptions->TextureImageQuality, CompressedData);
			break;

		default:
			checkNoEntry();
			break;
	}

	FGLTFJsonImage JsonImage;

	if (Builder.bIsGlbFile)
	{
		JsonImage.Name = Name;
		JsonImage.MimeType = MimeType;
		JsonImage.BufferView = Builder.AddBufferView(CompressedData.GetData(), CompressedData.Num());
	}
	else
	{
		JsonImage.Uri = SaveToFile(CompressedData.GetData(), CompressedData.Num(), MimeType, Name);
	}

	return Builder.AddImage(JsonImage);
}

EGLTFJsonMimeType FGLTFImageConverter::GetMimeType(const FColor* Pixels, FIntPoint Size, bool bIgnoreAlpha, EGLTFTextureType Type) const
{
	switch (Builder.ExportOptions->TextureImageFormat)
	{
		case EGLTFTextureImageFormat::None:
			return EGLTFJsonMimeType::None;

		case EGLTFTextureImageFormat::PNG:
			return EGLTFJsonMimeType::PNG;

		case EGLTFTextureImageFormat::JPEG:
			return
				(Type == EGLTFTextureType::None || !EnumHasAllFlags(static_cast<EGLTFTextureType>(Builder.ExportOptions->NoLossyImageFormatFor), Type)) &&
				(bIgnoreAlpha || FGLTFImageUtility::NoAlphaNeeded(Pixels, Size)) ?
				EGLTFJsonMimeType::JPEG : EGLTFJsonMimeType::PNG;

		default:
			checkNoEntry();
		return EGLTFJsonMimeType::None;
	}
}

FString FGLTFImageConverter::SaveToFile(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType, const FString& Name)
{
	const TCHAR* Extension = FGLTFImageUtility::GetFileExtension(MimeType);
	const FString ImageUri = FGLTFImageUtility::GetUniqueFilename(Name, Extension, UniqueImageUris);

	const TArrayView<const uint8> ImageData(static_cast<const uint8*>(CompressedData), CompressedByteLength);
	const FString ImagePath = FPaths::Combine(Builder.DirPath, ImageUri);

	if (!FFileHelper::SaveArrayToFile(ImageData, *ImagePath))
	{
		Builder.LogError(FString::Printf(TEXT("Failed to save image %s to file: %s"), *Name, *ImagePath));
		return TEXT("");
	}

	UniqueImageUris.Add(ImageUri);
	return ImageUri;
}
