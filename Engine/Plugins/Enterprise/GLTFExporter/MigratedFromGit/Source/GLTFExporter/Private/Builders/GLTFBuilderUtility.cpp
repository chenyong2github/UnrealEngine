// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilderUtility.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"

bool FGLTFBuilderUtility::CompressImage(const void* RawData, int64 ByteLength, int32 InWidth, int32 InHeight, ERGBFormat InRawFormat, int32 InBitDepth, TArray64<uint8>& OutCompressedData, EImageFormat OutCompressionFormat, int32 OutCompressionQuality)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(OutCompressionFormat);
	if (!ImageWrapper.IsValid())
	{
		// TODO: report error
		return false;
	}

	const bool bFormatSupported = ImageWrapper->SetRaw(RawData, ByteLength, InWidth, InHeight, InRawFormat, InBitDepth);
	if (!bFormatSupported)
	{
		// TODO: report error
		return false;
	}

	const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(OutCompressionQuality);
	OutCompressedData.Append(CompressedData);
	return true;
}

FString FGLTFBuilderUtility::GetUniqueFilename(const FString& BaseFilename, const FString& FileExtension, const TSet<FString>& UniqueFilenames)
{
	FString Filename = BaseFilename + FileExtension;
	if (!UniqueFilenames.Contains(Filename))
	{
		return Filename;
	}

	int32 Suffix = 1;
	do
	{
		Filename = BaseFilename + TEXT('_') + FString::FromInt(Suffix) + FileExtension;
		Suffix++;
	}
	while (UniqueFilenames.Contains(Filename));

	return Filename;
}
