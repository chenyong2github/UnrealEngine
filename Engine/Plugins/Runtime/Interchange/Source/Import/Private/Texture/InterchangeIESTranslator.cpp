// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Texture/InterchangeIESTranslator.h"

#include "Engine/Texture.h"
#include "Engine/TextureLightProfile.h"
#include "IESConverter.h"
#include "InterchangeImportLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Texture/TextureTranslatorUtilities.h"

bool UInterchangeIESTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString IESExtension = (TEXT("ies"));
	return IESExtension.StartsWith(Extension);
}

bool UInterchangeIESTranslator::Translate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::GenericTextureLightProfileTranslate(SourceData, BaseNodeContainer);
}

TOptional<UE::Interchange::FImportLightProfile> UInterchangeIESTranslator::GetLightProfilePayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	if (!SourceData)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import IES, bad source data."));
		return {};
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = SourceData->GetFilename();

	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import IES, wrong payload key. [%s]"), *Filename);
		return {};
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import IES, cannot open file. [%s]"), *Filename);
		return {};
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import IES, cannot load file content into an array. [%s]"), *Filename);
		return {};
	}

	const uint8* Buffer = SourceDataBuffer.GetData();

	FIESConverter IESConverter(Buffer, SourceDataBuffer.Num());

	if(IESConverter.IsValid())
	{
		UE::Interchange::FImportLightProfile Payload;

		Payload.Init2DWithParams(
			IESConverter.GetWidth(),
			IESConverter.GetHeight(),
			TSF_RGBA16F,
			false
			);

		Payload.CompressionSettings = TC_HDR;
		Payload.Brightness = IESConverter.GetBrightness();
		Payload.TextureMultiplier = IESConverter.GetMultiplier();

		Payload.RawData = IESConverter.GetRawData();

		return Payload;
	}

	return {};
}
