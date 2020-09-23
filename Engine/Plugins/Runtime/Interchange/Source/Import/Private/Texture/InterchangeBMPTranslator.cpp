// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Texture/InterchangeBMPTranslator.h"

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



bool UInterchangeBMPTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString BMPExtension = (TEXT("bmp;Texture"));
	return BMPExtension.StartsWith(Extension);
}

bool UInterchangeBMPTranslator::Translate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(SourceData, BaseNodeContainer);
}

TOptional<Interchange::FImportImage> UInterchangeBMPTranslator::GetTexturePayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	if (!SourceData)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import BMP, bad source data."));
		return TOptional<Interchange::FImportImage>();
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = SourceData->GetFilename();

	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import BMP, wrong payload key. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import BMP, cannot open file. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import BMP, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	const uint8* Buffer = SourceDataBuffer.GetData();
	const uint8* BufferEnd = Buffer + SourceDataBuffer.Num();

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);

	// Validate it.
	const int32 Length = BufferEnd - Buffer;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	//
	// BMP
	//
	TSharedPtr<IImageWrapper> BmpImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
	if (!BmpImageWrapper.IsValid() || !BmpImageWrapper->SetCompressed(Buffer, Length))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to decode BMP. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}
	if (!Interchange::FImportImageHelper::IsImportResolutionValid(BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), bAllowNonPowerOfTwo))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import BMP, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	int32 BitDepth = BmpImageWrapper->GetBitDepth();
	ERGBFormat Format = BmpImageWrapper->GetFormat();

	Interchange::FImportImage PayloadData;

	PayloadData.Init2DWithParams(
		BmpImageWrapper->GetWidth(),
		BmpImageWrapper->GetHeight(),
		TSF_BGRA8,
		false
	);

	if (!BmpImageWrapper->GetRaw(Format, BitDepth, PayloadData.RawData))
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to decode BMP. [%s]"), *Filename);
		return TOptional<Interchange::FImportImage>();
	}

	return PayloadData;
}
