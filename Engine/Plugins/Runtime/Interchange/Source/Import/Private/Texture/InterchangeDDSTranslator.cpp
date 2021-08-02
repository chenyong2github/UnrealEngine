// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangeDDSTranslator.h"

#include "DDSLoader.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureNode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/TextureTranslatorUtilities.h"

#include "HAL/FileManager.h"
#include "Serialization/Archive.h"

namespace UE::Interchange::Private::InterchangeDDSTranslator
{ 
	bool LoadDDSHeaderFromFile(TArray64<uint8>& OutHeader, const FString& Filename)
	{
		// The file is close when the archive is destroyed
		TUniquePtr<FArchive> FileReaderArchive(IFileManager::Get().CreateFileReader(*Filename));

		if (FileReaderArchive)
		{
			const int64 SizeOfFile = FileReaderArchive->TotalSize();
			const int64 MinimalHeaderSize = FDDSLoadHelper::GetDDSHeaderMinimalSize();

			// If the file is not bigger then the smallest header possible then clearly the file is not valid as a dds file.
			if (SizeOfFile > MinimalHeaderSize)
			{
				const int64 MaximalHeaderSize = FDDSLoadHelper::GetDDSHeaderMaximalSize();
				const int64 BytesToRead = SizeOfFile >= MaximalHeaderSize ? MaximalHeaderSize : MinimalHeaderSize;

				OutHeader.Reset(BytesToRead);
				OutHeader.AddUninitialized(BytesToRead);
				FileReaderArchive->Serialize(OutHeader.GetData(), OutHeader.Num());

				return true;
			}
		}

		return false;
	}
}

bool UInterchangeDDSTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename());
	FString DDSExtension = (TEXT("dds;Texture"));
	if (!DDSExtension.StartsWith(Extension))
	{
		return false;
	}

	/*
	 * DDS file can also be a texture array so we have to open the file and see if its a valid 2D texture.
	 */
	TArray64<uint8> HeaderDataBuffer;
	FString Filename = InSourceData->GetFilename();

	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	if (!UE::Interchange::Private::InterchangeDDSTranslator::LoadDDSHeaderFromFile(HeaderDataBuffer, Filename))
	{
		return false;
	}

	FDDSLoadHelper  DDSLoadHelper(HeaderDataBuffer.GetData(), HeaderDataBuffer.Num());
	return DDSLoadHelper.IsValid2DTexture() || DDSLoadHelper.IsValidCubemapTexture() || DDSLoadHelper.IsValidArrayTexture();
}

bool UInterchangeDDSTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	/*
	 * DDS file can also be a cube map so we have to open the file and see if its a valid 2D texture.
	 */
	FString Filename = GetSourceData()->GetFilename();
	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	TArray64<uint8> HeaderDataBuffer;
	if (!UE::Interchange::Private::InterchangeDDSTranslator::LoadDDSHeaderFromFile(HeaderDataBuffer, Filename))
	{
		return false;
	}


	FDDSLoadHelper DDSLoadHelper(HeaderDataBuffer.GetData(), HeaderDataBuffer.Num());
	if (DDSLoadHelper.IsValid2DTexture())
	{
		return UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(GetSourceData(), BaseNodeContainer);
	}

	if (DDSLoadHelper.IsValidCubemapTexture())
	{
		return UE::Interchange::FTextureTranslatorUtilities::GenericTextureCubeTranslate(GetSourceData(), BaseNodeContainer);
	}

	if (DDSLoadHelper.IsValidArrayTexture())
	{
		return UE::Interchange::FTextureTranslatorUtilities::GenericTexture2DArrayTranslate(GetSourceData(), BaseNodeContainer);
	}

	return false;
}

TOptional<UE::Interchange::FImportImage> UInterchangeDDSTranslator::GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(PayloadSourceData == GetSourceData());

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, bad source data."));
		return TOptional<UE::Interchange::FImportImage>();
	}

	FString Filename = GetSourceData()->GetFilename();
	
	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, wrong payload key. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	TArray64<uint8> DDSSourceData;

	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, cannot open file. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	if (!FFileHelper::LoadFileToArray(DDSSourceData, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}


	//
	// DDS Texture
	//
	FDDSLoadHelper  DDSLoadHelper(DDSSourceData.GetData(), DDSSourceData.Num());
	if (!DDSLoadHelper.IsValid2DTexture())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, unsupported format. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);

	// DDS 2d texture
	if (!UE::Interchange::FImportImageHelper::IsImportResolutionValid(DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PCX, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	ETextureSourceFormat SourceFormat = DDSLoadHelper.ComputeSourceFormat();

	// Invalid DDS format
	if (SourceFormat == TSF_Invalid)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("DDS file [%s] contains data in an unsupported format"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	uint32 MipMapCount = DDSLoadHelper.ComputeMipMapCount();
	if (MipMapCount <= 0)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("DDS file [%s] do not have any mipmap"), *Filename);
		return TOptional<UE::Interchange::FImportImage>();
	}

	UE::Interchange::FImportImage PayloadData;
	PayloadData.Init2DWithMips(
		DDSLoadHelper.DDSHeader->dwWidth,
		DDSLoadHelper.DDSHeader->dwHeight,
		MipMapCount,
		SourceFormat,
		DDSLoadHelper.GetDDSDataPointer()
	);

	if (MipMapCount > 1)
	{
		PayloadData.MipGenSettings = TMGS_LeaveExistingMips;
	}
	if (FTextureSource::IsHDR(SourceFormat))
	{
		// the loader can suggest a compression setting
		PayloadData.CompressionSettings = TC_HDR;
	}

	return PayloadData;
}

TOptional<UE::Interchange::FImportSlicedImage> UInterchangeDDSTranslator::GetSlicedTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const
{
	check(PayloadSourceData == GetSourceData());

	if (!GetSourceData())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, bad source data."));
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	FString Filename = GetSourceData()->GetFilename();
	
	//Make sure the key fit the filename, The key should always be valid
	if (!Filename.Equals(PayLoadKey))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, wrong payload key. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	TArray64<uint8> DDSSourceData;
	if (!FPaths::FileExists(Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, cannot open file. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	if (!FFileHelper::LoadFileToArray(DDSSourceData, *Filename))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, cannot load file content into an array. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	//
	// DDS Texture
	//
	FDDSLoadHelper DDSLoadHelper(DDSSourceData.GetData(), DDSSourceData.Num());

	if (!DDSLoadHelper.IsValidCubemapTexture() && !DDSLoadHelper.IsValidArrayTexture() )
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import DDS, unsupported format. [%s]"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool(TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni);

	if (!UE::Interchange::FImportImageHelper::IsImportResolutionValid(DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Failed to import PCX, invalid resolution. Resolution[%d, %d], AllowPowerOfTwo[%s], [%s]"), DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo ? TEXT("True") : TEXT("false"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	ETextureSourceFormat SourceFormat = DDSLoadHelper.ComputeSourceFormat();

	// Invalid DDS format
	if (SourceFormat == TSF_Invalid)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("DDS file [%s] contains data in an unsupported format"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	uint32 MipMapCount = DDSLoadHelper.ComputeMipMapCount();
	if (MipMapCount <= 0)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("DDS file [%s] do not have any mipmap"), *Filename);
		return TOptional<UE::Interchange::FImportSlicedImage>();
	}

	UE::Interchange::FImportSlicedImage PayloadData;
	PayloadData.Init(
		DDSLoadHelper.GetSizeX(),
		DDSLoadHelper.GetSizeY(),
		DDSLoadHelper.GetSliceCount(),
		MipMapCount,
		SourceFormat,
		!FTextureSource::IsHDR(SourceFormat)
	);

	uint8* DestMipData[MAX_TEXTURE_MIP_COUNT] = { 0 };
	int64 MipsSize[MAX_TEXTURE_MIP_COUNT] = { 0 };

	for (uint32 MipIndex = 0; MipIndex < MipMapCount; ++MipIndex)
	{
		int64 MipSize = PayloadData.GetMipSize(MipIndex);
		MipsSize[MipIndex] = MipSize;
	}

	for (uint32 MipIndex = 0; MipIndex < MipMapCount; ++MipIndex)
	{
		DestMipData[MipIndex] = PayloadData.GetMipData(MipIndex);
	}

	for (uint32 SliceIndex = 0; SliceIndex < DDSLoadHelper.GetSliceCount(); ++SliceIndex)
	{
		const uint8* SrcMipData = DDSLoadHelper.GetDDSDataPointer((ECubeFace)SliceIndex);
		for (uint32 MipIndex = 0; MipIndex < MipMapCount; ++MipIndex)
		{
			FMemory::Memcpy(DestMipData[MipIndex] + MipsSize[MipIndex] * SliceIndex, SrcMipData, MipsSize[MipIndex]);
			SrcMipData += MipsSize[MipIndex];
		}
	}

	if (MipMapCount > 1)
	{
		PayloadData.MipGenSettings = TMGS_LeaveExistingMips;
	}

	if (FTextureSource::IsHDR(SourceFormat))
	{
		// the loader can suggest a compression setting
		PayloadData.CompressionSettings = TC_HDR;
	}

	return PayloadData;
}


