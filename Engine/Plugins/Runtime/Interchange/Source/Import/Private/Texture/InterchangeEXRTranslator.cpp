// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangeEXRTranslator.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InterchangeTextureNode.h"
#include "LogInterchangeImportPlugin.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/TextureTranslatorUtilities.h"

bool UInterchangeEXRTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString EXRExtension = (TEXT("exr;Texture"));
	return EXRExtension.StartsWith(Extension);
}

bool UInterchangeEXRTranslator::Translate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(SourceData, BaseNodeContainer);
}

TOptional<Interchange::FImportImage> UInterchangeEXRTranslator::GetTexturePayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	if (!SourceData)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import EXR, bad source data."));
		return TOptional<Interchange::FImportImage>();
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = SourceData->GetFilename();
	
	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import EXR, wrong payload key. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import EXR, cannot open file. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import EXR, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	const uint8* Buffer = SourceDataBuffer.GetData();
	const uint8* BufferEnd = Buffer + SourceDataBuffer.Num();

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);

	// Validate it.
	const int32 Length = BufferEnd - Buffer;

	//
	// EXR
	//
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	TSharedPtr<IImageWrapper> ExrImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
	if (!ExrImageWrapper.IsValid() || !ExrImageWrapper->SetCompressed(Buffer, Length))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import EXR, unsupported format. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	int32 Width = ExrImageWrapper->GetWidth();
	int32 Height = ExrImageWrapper->GetHeight();

	if (!Interchange::FImportImageHelper::IsImportResolutionValid(Width, Height, bAllowNonPowerOfTwo))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import PCX, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), Width, Height, bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	// Select the texture's source format
	ETextureSourceFormat TextureFormat = TSF_Invalid;
	int32 BitDepth = ExrImageWrapper->GetBitDepth();
	ERGBFormat Format = ExrImageWrapper->GetFormat();

	if (Format == ERGBFormat::RGBA && BitDepth == 16)
	{
		TextureFormat = TSF_RGBA16F;
		Format = ERGBFormat::BGRA;
	}

	if (TextureFormat == TSF_Invalid)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("EXR file [%s] contains data in an unsupported format"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	Interchange::FImportImage PayloadData;
	PayloadData.Init2DWithParams(
		Width,
		Height,
		TextureFormat,
		false
	);
	PayloadData.CompressionSettings = TC_HDR;

	if (!ExrImageWrapper->GetRaw(Format, BitDepth, PayloadData.RawData))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to decode EXR. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	return PayloadData;
}