// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformStackWalk.h"
#include "Misc/Paths.h"
#include "ImageCore.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "TextureCompressorModule.h"
#include "PixelFormat.h"
#include "TextureConverter.h"
#include "HAL/PlatformProcess.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatETC2, Log, All);


/**
 * Macro trickery for supported format names.
 */
#define ENUM_SUPPORTED_FORMATS(op) \
	op(ETC2_RGB) \
	op(ETC2_RGBA) \
	op(AutoETC2)

#define DECL_FORMAT_NAME(FormatName) static FName GTextureFormatName##FormatName = FName(TEXT(#FormatName));
ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME);
#undef DECL_FORMAT_NAME

#define DECL_FORMAT_NAME_ENTRY(FormatName) GTextureFormatName##FormatName ,
static FName GSupportedTextureFormatNames[] =
{
	ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME_ENTRY)
};
#undef DECL_FORMAT_NAME_ENTRY

#undef ENUM_SUPPORTED_FORMATS


/**
 * Compresses an image using Qonvert.
 * @param SourceData			Source texture data to compress, in BGRA 8bit per channel unsigned format.
 * @param PixelFormat			Texture format
 * @param SizeX					Number of texels along the X-axis
 * @param SizeY					Number of texels along the Y-axis
 * @param OutCompressedData		Compressed image data output by Qonvert.
 */
static bool CompressImageUsingQonvert(
	const void* SourceData,
	EPixelFormat PixelFormat,
	int32 SizeX,
	int32 SizeY,
	TArray64<uint8>& OutCompressedData
	)
{
	// Avoid dependency on GPixelFormats in RenderCore.
	// If block size changes, please update in AndroidETC.cpp in DecompressTexture
	const int32 BlockSizeX = 4;
	const int32 BlockSizeY = 4;
	const int32 BlockBytes = (PixelFormat == PF_ETC2_RGBA) ? 16 : 8;
	const int32 ImageBlocksX = FMath::Max(SizeX / BlockSizeX, 1);
	const int32 ImageBlocksY = FMath::Max(SizeY / BlockSizeY, 1);

	// The converter doesn't support 64-bit sizes.
	const int64 SourceDataSize = (int64)SizeX * SizeY * 4;
	const int64 OutDataSize = (int64)ImageBlocksX * ImageBlocksY * BlockBytes;
	if (SourceDataSize != (uint32)SourceDataSize || OutDataSize != (uint32)OutDataSize)
	{
		return false;
	}

	// Allocate space to store compressed data.
	OutCompressedData.Empty(OutDataSize);
	OutCompressedData.AddUninitialized(OutDataSize);

	TQonvertImage SrcImg;
	TQonvertImage DstImg;

	FMemory::Memzero(SrcImg);
	FMemory::Memzero(DstImg);

	SrcImg.nWidth    = SizeX;
	SrcImg.nHeight   = SizeY;
	SrcImg.nFormat   = Q_FORMAT_BGRA_8888;
	SrcImg.nDataSize = (uint32)SourceDataSize;
	SrcImg.pData     = (unsigned char*)SourceData;

	DstImg.nWidth    = SizeX;
	DstImg.nHeight   = SizeY;
	DstImg.nDataSize = (uint32)OutDataSize;
	DstImg.pData     = OutCompressedData.GetData();

	switch (PixelFormat)
	{
	case PF_ETC2_RGB:
		DstImg.nFormat = Q_FORMAT_ETC2_RGB8;
		break;
	case PF_ETC2_RGBA:
		DstImg.nFormat = Q_FORMAT_ETC2_RGBA8;
		break;
	default:
		UE_LOG(LogTextureFormatETC2, Fatal, TEXT("Unsupported EPixelFormat for compression: %u"), (uint32)PixelFormat);
		return false;
	}

	if (Qonvert(&SrcImg, &DstImg) != Q_SUCCESS)
	{
		UE_LOG(LogTextureFormatETC2, Fatal, TEXT("CONVERSION FAILED"));
		return false;
	}

	return true;
}


/**
 * ETC2 texture format handler.
 */
class FTextureFormatETC2 : public ITextureFormat
{
	virtual bool AllowParallelBuild() const override
	{
		return !PLATFORM_MAC; // On Mac Qualcomm's TextureConverter library is not thead-safe
	}

	virtual uint16 GetVersion(
		FName Format,
		const struct FTextureBuildSettings* BuildSettings = nullptr
	) const override
	{
		return 0;
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(GSupportedTextureFormatNames); ++i)
		{
			OutFormats.Add(GSupportedTextureFormatNames[i]);
		}
	}

	virtual FTextureFormatCompressorCaps GetFormatCapabilities() const override
	{
		return FTextureFormatCompressorCaps(); // Default capabilities.
	}

	virtual EPixelFormat GetPixelFormatForImage(const struct FTextureBuildSettings& BuildSettings, const struct FImage& Image, bool bImageHasAlphaChannel) const override
	{
		if (BuildSettings.TextureFormatName == GTextureFormatNameETC2_RGB ||
			(BuildSettings.TextureFormatName == GTextureFormatNameAutoETC2 && !bImageHasAlphaChannel))
		{
			return PF_ETC2_RGB;
		}

		if (BuildSettings.TextureFormatName == GTextureFormatNameETC2_RGBA ||
				(BuildSettings.TextureFormatName == GTextureFormatNameAutoETC2 && bImageHasAlphaChannel))
		{
			return PF_ETC2_RGBA;
		}

		UE_LOG(LogTextureFormatETC2, Fatal, TEXT("Unhandled texture format '%s' given to FTextureFormatAndroid::GetPixelFormatForImage()"), *BuildSettings.TextureFormatName.ToString());
		return PF_Unknown;
	}

	virtual bool CompressImage(
		const FImage& InImage,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		FCompressedImage2D& OutCompressedImage
		) const override
	{
		FImage Image;
		InImage.CopyTo(Image, ERawImageFormat::BGRA8, BuildSettings.GetGammaSpace());

		EPixelFormat CompressedPixelFormat = GetPixelFormatForImage(BuildSettings, Image, bImageHasAlphaChannel);

		bool bCompressionSucceeded = true;
		int32 SliceSize = Image.SizeX * Image.SizeY;
		for (int32 SliceIndex = 0; SliceIndex < Image.NumSlices && bCompressionSucceeded; ++SliceIndex)
		{
			TArray64<uint8> CompressedSliceData;
			bCompressionSucceeded = CompressImageUsingQonvert(
				Image.AsBGRA8().GetData() + SliceIndex * SliceSize,
				CompressedPixelFormat,
				Image.SizeX,
				Image.SizeY,
				CompressedSliceData
				);
			OutCompressedImage.RawData.Append(CompressedSliceData);
		}

		if (bCompressionSucceeded)
		{
			OutCompressedImage.SizeX = FMath::Max(Image.SizeX, 4);
			OutCompressedImage.SizeY = FMath::Max(Image.SizeY, 4);
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;
			OutCompressedImage.PixelFormat = CompressedPixelFormat;
		}

		return bCompressionSucceeded;
	}
};

static ITextureFormat* Singleton = NULL;



#if PLATFORM_WINDOWS
	void*	TextureConverterHandle = NULL;
	FString QualCommBinariesRoot = FPaths::EngineDir() / TEXT("Binaries/ThirdParty/QualComm/Win64/");
#endif


class FTextureFormatETC2Module : public ITextureFormatModule
{
public:

	FTextureFormatETC2Module()
	{
#if PLATFORM_WINDOWS
		TextureConverterHandle = FPlatformProcess::GetDllHandle(*(QualCommBinariesRoot + "TextureConverter.dll"));
#endif
	}

	virtual ~FTextureFormatETC2Module()
	{
		delete Singleton;
		Singleton = NULL;

#if PLATFORM_WINDOWS
		FPlatformProcess::FreeDllHandle(TextureConverterHandle);
#endif
	}
	virtual ITextureFormat* GetTextureFormat()
	{
		if (!Singleton)
		{
#if PLATFORM_WINDOWS
			TextureConverterHandle = FPlatformProcess::GetDllHandle(*(QualCommBinariesRoot + "TextureConverter.dll"));
#endif
			Singleton = new FTextureFormatETC2();
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE(FTextureFormatETC2Module, TextureFormatETC2);
