// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeTIFFTranslator.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureNode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/TextureTranslatorUtilities.h"


bool UInterchangeTIFFTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString TIFFExtension = (TEXT("tiff"));
	return TIFFExtension.StartsWith(Extension);
}

bool UInterchangeTIFFTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(GetSourceData(), BaseNodeContainer);
}

TOptional<UE::Interchange::FImportImage> UInterchangeTIFFTranslator::GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	if (!PayloadSourceData)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TIF, bad source data."));
		return TOptional<UE::Interchange::FImportImage>();
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = PayloadSourceData->GetFilename();

	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TIF, wrong payload key. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TIF, cannot open file. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TIF, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	const uint8* Buffer = SourceDataBuffer.GetData();
	const uint8* BufferEnd = Buffer + SourceDataBuffer.Num();

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);


	const int32 Length = BufferEnd - Buffer;

	//
	// TIFF File
	//
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> TiffImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::TIFF);
	if (TiffImageWrapper.IsValid())
	{
		if (TiffImageWrapper->SetCompressed(Buffer, Length))
		{ 
			// Check the resolution of the imported texture to ensure validity
			if (!UE::Interchange::FImportImageHelper::IsImportResolutionValid(TiffImageWrapper->GetWidth(), TiffImageWrapper->GetHeight(), bAllowNonPowerOfTwo))
			{
				UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TIFF, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), TiffImageWrapper->GetWidth(), TiffImageWrapper->GetHeight(), bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
				return TOptional<UE::Interchange::FImportImage>();
			}

			ETextureSourceFormat SourceFormat = TSF_Invalid;
			ERGBFormat TiffFormat = TiffImageWrapper->GetFormat();
			const int32 BitDepth = TiffImageWrapper->GetBitDepth();
			bool bIsSRGB = false;


			if (TiffFormat == ERGBFormat::BGRA)
			{
				SourceFormat = TSF_BGRA8;
				bIsSRGB = true;
			}
			else if (TiffFormat == ERGBFormat::RGBA)
			{
				SourceFormat = TSF_RGBA16;
			}
			else if (TiffFormat == ERGBFormat::RGBAF)
			{
				SourceFormat = TSF_RGBA16F;
			}
			else if (TiffFormat == ERGBFormat::Gray)
			{
				SourceFormat = TSF_G8;
				if (BitDepth == 16)
				{
					SourceFormat = TSF_G16;
				}
			}

			UE::Interchange::FImportImage PayloadData;
			PayloadData.Init2DWithParams(
				TiffImageWrapper->GetWidth(),
				TiffImageWrapper->GetHeight(),
				SourceFormat,
				bIsSRGB
			);

			if (!TiffImageWrapper->GetRaw(TiffFormat, BitDepth, PayloadData.GetArrayViewOfRawData()))
			{
				UE_LOG(LogInterchangeImport, Error, TEXT("Failed to Import Tiff. [%s]"), *Filename);
				return {};
			}

			return PayloadData;
		}

		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to Import Tiff. Unsuported platform."));
		return {};
	}
	else
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to decode Tiff. [%s]"), *Filename);
		return {};
	}

	return {};
}