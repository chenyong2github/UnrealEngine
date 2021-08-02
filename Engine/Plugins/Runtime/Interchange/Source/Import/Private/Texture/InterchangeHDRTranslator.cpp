// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Texture/InterchangeHDRTranslator.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Formats/HdrImageWrapper.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureNode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/TextureTranslatorUtilities.h"


bool UInterchangeHDRTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString HDRExtension = (TEXT("hdr"));
	return HDRExtension.StartsWith(Extension);
}

bool UInterchangeHDRTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(GetSourceData(), BaseNodeContainer);
}

TOptional<UE::Interchange::FImportImage> UInterchangeHDRTranslator::GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(PayloadSourceData == GetSourceData());

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import HDR, bad source data."));
		return TOptional<UE::Interchange::FImportImage>();
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = GetSourceData()->GetFilename();

	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import HDR, wrong payload key. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import HDR, cannot open file. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import HDR, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	const uint8* Buffer = SourceDataBuffer.GetData();
	const uint8* BufferEnd = Buffer + SourceDataBuffer.Num();

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);


	const int32 Length = BufferEnd - Buffer;


	UE::Interchange::FImportImage PayloadData;

	//
	// HDR File
	//	
	FHdrImageWrapper HdrImageWrapper;
	if (HdrImageWrapper.SetCompressedFromView(TArrayView64<const uint8>(Buffer, Length)))
	{
		if (!UE::Interchange::FImportImageHelper::IsImportResolutionValid(HdrImageWrapper.GetWidth(), HdrImageWrapper.GetHeight(), bAllowNonPowerOfTwo))
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import HDR, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), HdrImageWrapper.GetWidth(), HdrImageWrapper.GetHeight(), bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
			return TOptional<UE::Interchange::FImportImage>();
		}

		PayloadData.Init2DWithParams(
			HdrImageWrapper.GetWidth(),
			HdrImageWrapper.GetHeight(),
			TSF_BGRE8,
			false);

		if (!HdrImageWrapper.GetRaw(ERGBFormat::BGRE, 8, PayloadData.GetArrayViewOfRawData()))
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Failed to Import HDR. [%s]"), *Filename);
			UE_LOG(LogInterchangeImport, Error, TEXT("%s"), *( HdrImageWrapper.GetErrorMessage().ToString()));
			return TOptional<UE::Interchange::FImportImage>();
		}

		PayloadData.CompressionSettings = TC_HDR;
	}
	else
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to decode HDR. [%s]"), *Filename);
		UE_LOG(LogInterchangeImport, Error, TEXT("%s"), *( HdrImageWrapper.GetErrorMessage().ToString()));
		return TOptional<UE::Interchange::FImportImage>();
	}

	return PayloadData;
}