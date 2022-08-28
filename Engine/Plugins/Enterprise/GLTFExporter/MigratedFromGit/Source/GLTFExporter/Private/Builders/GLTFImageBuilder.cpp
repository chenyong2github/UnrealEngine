// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFImageBuilder.h"
#include "Builders/GLTFBuilderUtility.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/FileHelper.h"

FGLTFImageBuilder::FGLTFImageBuilder()
{
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType, const FString& Name)
{
	FGLTFJsonImage Image;
	Image.Name = Name;
	Image.MimeType = MimeType;
	const FGLTFJsonImageIndex ImageIndex = FGLTFJsonBuilder::AddImage(Image);

	TArray64<uint8>& ImageData = ImageDataLookup.Add(ImageIndex);
	ImageData.Append(static_cast<const uint8*>(CompressedData), CompressedByteLength);
	return ImageIndex;
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const void* RawData, int64 ByteLength, FIntPoint Size, ERGBFormat Format, int32 BitDepth, const FString& Name)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		// TODO: report error
		return FGLTFJsonImageIndex(INDEX_NONE);
	}

	const bool bFormatSupported = ImageWrapper->SetRaw(RawData, ByteLength, Size.X, Size.Y, Format, BitDepth);

	if (!bFormatSupported)
	{
		// TODO: report error
		return FGLTFJsonImageIndex(INDEX_NONE);
	}

	const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed();
	return AddImage(ImageData.GetData(), ImageData.Num(), EGLTFJsonMimeType::PNG, Name);
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const FColor* Pixels, FIntPoint Size, const FString& Name)
{
	const int64 ByteLength = Size.X * Size.Y * sizeof(FColor);
	return AddImage(Pixels, ByteLength, Size, ERGBFormat::BGRA, 8, Name);
}

bool FGLTFImageBuilder::Serialize(FArchive& Archive, const FString& FilePath)
{
	const FString ImageDir = FPaths::GetPath(FilePath);
	TSet<FString> UniqueImageUris;

	for (const auto& DataPair : ImageDataLookup)
	{
		const TArray64<uint8>& ImageData = DataPair.Value;
		FGLTFJsonImage& JsonImage = GetImage(DataPair.Key);

		const TCHAR* Extension = FGLTFBuilderUtility::GetFileExtension(JsonImage.MimeType);
		const FString ImageUri = FGLTFBuilderUtility::GetUniqueFilename(JsonImage.Name, Extension, UniqueImageUris);

		const FString ImagePath = FPaths::Combine(ImageDir, ImageUri);
		if (!FFileHelper::SaveArrayToFile(ImageData, *ImagePath))
		{
			// TODO: report error
			continue;
		}

		UniqueImageUris.Add(ImageUri);

		JsonImage.Uri = ImageUri;
		JsonImage.Name.Empty(); // URI already contains name
		JsonImage.MimeType = EGLTFJsonMimeType::None; // Not required if external file
	}

	return FGLTFBufferBuilder::Serialize(Archive, FilePath);
}
