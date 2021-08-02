// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangeEXRTranslator.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureNode.h"
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

bool UInterchangeEXRTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	return UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(GetSourceData(), BaseNodeContainer);
}

TOptional<UE::Interchange::FImportImage> UInterchangeEXRTranslator::GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(PayloadSourceData == GetSourceData());

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import EXR, bad source data."));
		return TOptional<UE::Interchange::FImportImage>();
	}

	TArray64<uint8> SourceDataBuffer;
	FString Filename = GetSourceData()->GetFilename();
	
	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import EXR, wrong payload key. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import EXR, cannot open file. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(SourceDataBuffer, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import EXR, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
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
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import EXR, unsupported format. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	int32 Width = ExrImageWrapper->GetWidth();
	int32 Height = ExrImageWrapper->GetHeight();

	if (!UE::Interchange::FImportImageHelper::IsImportResolutionValid(Width, Height, bAllowNonPowerOfTwo))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import EXR, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), Width, Height, bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	// Currently the only texture source format compatible with EXR image formats is TSF_RGBA16F.
	// EXR decoder automatically converts imported image channels into the requested float format.
	// In case if the imported image is a gray scale image, its content will be stored in the green channel of the created texture.
	ETextureSourceFormat TextureFormat = TSF_RGBA16F;
	ERGBFormat Format = ERGBFormat::RGBAF;
	int32 BitDepth = 16;

	UE::Interchange::FImportImage PayloadData;
	PayloadData.Init2DWithParams(
		Width,
		Height,
		TextureFormat,
		false
	);
	PayloadData.CompressionSettings = TC_HDR;

	if (!ExrImageWrapper->GetRaw(Format, BitDepth, PayloadData.GetArrayViewOfRawData()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to decode EXR. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	return PayloadData;
}