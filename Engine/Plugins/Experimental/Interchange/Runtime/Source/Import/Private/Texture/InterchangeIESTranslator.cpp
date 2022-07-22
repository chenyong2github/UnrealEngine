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

static bool GInterchangeEnableIESImport = true;
static FAutoConsoleVariableRef CCvarInterchangeEnableIESImport(
	TEXT("Interchange.FeatureFlags.Import.IES"),
	GInterchangeEnableIESImport,
	TEXT("Whether IES support is enabled."),
	ECVF_Default);

TArray<FString> UInterchangeIESTranslator::GetSupportedFormats() const
{
	if (GInterchangeEnableIESImport)
	{
		TArray<FString> Formats{ TEXT("ies;IES light profile") };
		return Formats;
	}
	else
	{
		return TArray<FString>{};
	}
}

bool UInterchangeIESTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::GenericTextureLightProfileTranslate(GetSourceData(), BaseNodeContainer);
}

TOptional<UE::Interchange::FImportLightProfile> UInterchangeIESTranslator::GetLightProfilePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(GetSourceData() == PayloadSourceData);

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import IES, bad source data."));
		return {};
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = GetSourceData()->GetFilename();

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


		const TArray<uint8>&  RawData = IESConverter.GetRawData();;

		FPlatformMemory::Memcpy(Payload.RawData.GetData(), RawData.GetData(), Payload.RawData.GetSize());

		return Payload;
	}

	return {};
}
