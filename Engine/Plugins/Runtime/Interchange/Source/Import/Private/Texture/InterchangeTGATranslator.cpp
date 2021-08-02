// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangeTGATranslator.h"

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
#include "TgaImageSupport.h"

namespace TGATranslatorImpl
{
	bool DecompressTGA( const FTGAFileHeader* TGA, UE::Interchange::FImportImage& OutImage )
	{
		if (TGA->ColorMapType == 1 && TGA->ImageTypeCode == 1 && TGA->BitsPerPixel == 8)
		{
			// Notes: The Scaleform GFx exporter (dll) strips all font glyphs into a single 8-bit texture.
			// The targa format uses this for a palette index; GFx uses a palette of (i,i,i,i) so the index
			// is also the alpha value.
			//
			// We store the image as PF_G8, where it will be used as alpha in the Glyph shader.
			OutImage.Init2DWithOneMip(
				TGA->Width,
				TGA->Height,
				TSF_G8);
			OutImage.CompressionSettings = TC_Grayscale;
		}
		else if(TGA->ColorMapType == 0 && TGA->ImageTypeCode == 3 && TGA->BitsPerPixel == 8)
		{
			// standard grayscale images
			OutImage.Init2DWithOneMip(
				TGA->Width,
				TGA->Height,
				TSF_G8);
			OutImage.CompressionSettings = TC_Grayscale;
		}
		else
		{
			if(TGA->ImageTypeCode == 10) // 10 = RLE compressed 
			{
				if( TGA->BitsPerPixel != 32 &&
					TGA->BitsPerPixel != 24 &&
					TGA->BitsPerPixel != 16 )
				{
					UE_LOG( LogInterchangeImport, Error, TEXT("TGA uses an unsupported rle-compressed bit-depth: %u"), TGA->BitsPerPixel );
					return false;
				}
			}
			else
			{
				if( TGA->BitsPerPixel != 32 &&
					TGA->BitsPerPixel != 16 &&
					TGA->BitsPerPixel != 24)
				{
					UE_LOG( LogInterchangeImport, Error, TEXT("TGA uses an unsupported bit-depth: %u"), TGA->BitsPerPixel );
					return false;
				}
			}

			OutImage.Init2DWithOneMip(
				TGA->Width,
				TGA->Height,
				TSF_BGRA8);
		}

		return true;
	}
}

bool UInterchangeTGATranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString TGAExtension = (TEXT("tga;Texture"));
	return TGAExtension.StartsWith(Extension);
}

bool UInterchangeTGATranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(GetSourceData(), BaseNodeContainer);
}

TOptional<UE::Interchange::FImportImage> UInterchangeTGATranslator::GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(PayloadSourceData == GetSourceData());

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TGA, bad source data."));
		return {};
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = GetSourceData()->GetFilename();
	
	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TGA, wrong payload key. [%s]"), *Filename);
		return {};
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TGA, cannot open file. [%s]"), *Filename);
		return {};
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import TGA, cannot load file content into an array. [%s]"), *Filename);
		return {};
	}

	const uint8* Buffer = SourceDataBuffer.GetData();
	const uint8* BufferEnd = Buffer + SourceDataBuffer.Num();

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);

	const int32 Length = BufferEnd - Buffer;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	TSharedPtr<IImageWrapper> TgaImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::TGA);
	if ( TgaImageWrapper.IsValid() && TgaImageWrapper->SetCompressed( Buffer, Length ) )
	{
		// Check the resolution of the imported texture to ensure validity
		if ( !UE::Interchange::FImportImageHelper::IsImportResolutionValid( TgaImageWrapper->GetWidth(), TgaImageWrapper->GetHeight(), bAllowNonPowerOfTwo ) )
		{
			return {};
		}

		const FTGAFileHeader* TGA = (FTGAFileHeader *)Buffer;

		UE::Interchange::FImportImage PayloadData;

		TGATranslatorImpl::DecompressTGA( TGA, PayloadData );

		const bool bResult = TgaImageWrapper->GetRaw( TgaImageWrapper->GetFormat(), TgaImageWrapper->GetBitDepth(), PayloadData.GetArrayViewOfRawData() );

		if ( bResult && PayloadData.CompressionSettings == TC_Grayscale && TGA->ImageTypeCode == 3 )
		{
			// default grayscales to linear as they wont get compression otherwise and are commonly used as masks
			PayloadData.bSRGB = false;
		}

		return PayloadData;
	}

	return {};
}