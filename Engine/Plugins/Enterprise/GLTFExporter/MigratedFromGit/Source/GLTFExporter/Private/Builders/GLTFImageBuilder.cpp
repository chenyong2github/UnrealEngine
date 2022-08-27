// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFImageBuilder.h"
#include "Builders/GLTFBuilderUtility.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/FileHelper.h"

FGLTFImageBuilder::FGLTFImageBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: FGLTFBufferBuilder(FilePath, ExportOptions)
{
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const TArray<FColor>& Pixels, FIntPoint Size, const FString& Name)
{
	check(Pixels.Num() == Size.X * Size.Y);
	return AddImage(Pixels.GetData(), Size, Name);
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const FColor* Pixels, int64 ByteLength, FIntPoint Size, const FString& Name)
{
	check(ByteLength == Size.X * Size.Y * sizeof(FColor));
	return AddImage(Pixels, Size, Name);
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const FColor* Pixels, FIntPoint Size, const FString& Name)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		// TODO: report error
		return FGLTFJsonImageIndex(INDEX_NONE);
	}

	const int64 ByteLength = Size.X * Size.Y * sizeof(FColor);
	if (!ImageWrapper->SetRaw(Pixels, ByteLength, Size.X, Size.Y, ERGBFormat::BGRA, 8))
	{
		// TODO: report error
		return FGLTFJsonImageIndex(INDEX_NONE);
	}

	const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed();
	return AddImage(ImageData.GetData(), ImageData.Num(), EGLTFJsonMimeType::PNG, Name);
}

FGLTFJsonImageIndex FGLTFImageBuilder::AddImage(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType, const FString& Name)
{
	// TODO: should this function be renamed to GetOrAddImage?

	const FGLTFBinaryHashKey HashKey(CompressedData, CompressedByteLength);
	FGLTFJsonImageIndex& ImageIndex = UniqueImageIndices.FindOrAdd(HashKey);

	if (ImageIndex == INDEX_NONE)
	{
		FGLTFJsonImage JsonImage;

		if (bIsGlbFile)
		{
			JsonImage.Name = Name;
			JsonImage.MimeType = MimeType;
			JsonImage.BufferView = AddBufferView(CompressedData, CompressedByteLength);
		}
		else
		{
			JsonImage.Uri = SaveImageToFile(CompressedData, CompressedByteLength, MimeType, Name);
		}

		ImageIndex = FGLTFJsonBuilder::AddImage(JsonImage);
	}

	return ImageIndex;
}

FString FGLTFImageBuilder::SaveImageToFile(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType, const FString& Name)
{
	const TCHAR* Extension = FGLTFBuilderUtility::GetFileExtension(MimeType);
	const FString ImageUri = FGLTFBuilderUtility::GetUniqueFilename(Name, Extension, UniqueImageUris);

	const TArrayView<const uint8> ImageData(static_cast<const uint8*>(CompressedData), CompressedByteLength);
	const FString ImagePath = FPaths::Combine(DirPath, ImageUri);

	if (!FFileHelper::SaveArrayToFile(ImageData, *ImagePath))
	{
		// TODO: report error
		return TEXT("");
	}

	UniqueImageUris.Add(ImageUri);
	return ImageUri;
}
