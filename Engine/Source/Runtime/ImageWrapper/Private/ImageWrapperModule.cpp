// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageWrapperPrivate.h"

#include "CoreTypes.h"
#include "Modules/ModuleManager.h"

#include "Formats/BmpImageWrapper.h"
#include "Formats/ExrImageWrapper.h"
#include "Formats/HdrImageWrapper.h"
#include "Formats/IcnsImageWrapper.h"
#include "Formats/IcoImageWrapper.h"
#include "Formats/JpegImageWrapper.h"
#include "Formats/PngImageWrapper.h"
#include "Formats/TgaImageWrapper.h"
#include "Formats/TiffImageWrapper.h"

#include "IImageWrapperModule.h"


DEFINE_LOG_CATEGORY(LogImageWrapper);


namespace
{
	static const uint8 IMAGE_MAGIC_PNG[]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
	static const uint8 IMAGE_MAGIC_JPEG[] = {0xFF, 0xD8, 0xFF};
	static const uint8 IMAGE_MAGIC_BMP[]  = {0x42, 0x4D};
	static const uint8 IMAGE_MAGIC_ICO[]  = {0x00, 0x00, 0x01, 0x00};
	static const uint8 IMAGE_MAGIC_EXR[]  = {0x76, 0x2F, 0x31, 0x01};
	static const uint8 IMAGE_MAGIC_ICNS[] = {0x69, 0x63, 0x6E, 0x73};

	// Binary for #?RADIANCE
	static const uint8 IMAGE_MAGIC_HDR[] = {0x23, 0x3f, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4e, 0x43, 0x45, 0x0a};

	// Tiff has two magic bytes sequence
	static const uint8 IMAGE_MAGIC_TIFF_LITTLE_ENDIAN[] = {0x49, 0x49, 0x2A, 0x00};
	static const uint8 IMAGE_MAGIC_TIFF_BIG_ENDIAN[] = {0x4D, 0x4D, 0x00, 0x2A};

	/** Internal helper function to verify image signature. */
	template <int32 MagicCount> bool StartsWith(const uint8* Content, int64 ContentSize, const uint8 (&Magic)[MagicCount])
	{
		if (ContentSize < MagicCount)
		{
			return false;
		}

		for (int32 I = 0; I < MagicCount; ++I)
		{
			if (Content[I] != Magic[I])
			{
				return false;
			}
		}

		return true;
	}
}


/**
 * Image Wrapper module.
 */
class FImageWrapperModule
	: public IImageWrapperModule
{
public:

	//~ IImageWrapperModule interface

	virtual TSharedPtr<IImageWrapper> CreateImageWrapper(const EImageFormat InFormat) override
	{
		TSharedPtr<IImageWrapper> ImageWrapper;

		// Allocate a helper for the format type
		switch(InFormat)
		{
#if WITH_UNREALPNG
		case EImageFormat::PNG:
			ImageWrapper = MakeShared<FPngImageWrapper>();
			break;
#endif	// WITH_UNREALPNG

#if WITH_UNREALJPEG
		case EImageFormat::JPEG:
			ImageWrapper = MakeShared<FJpegImageWrapper>();
			break;

		case EImageFormat::GrayscaleJPEG:
			ImageWrapper = MakeShared<FJpegImageWrapper>(1);
			break;
#endif	//WITH_UNREALJPEG

		case EImageFormat::BMP:
			ImageWrapper = MakeShared<FBmpImageWrapper>();
			break;

		case EImageFormat::ICO:
			ImageWrapper = MakeShared<FIcoImageWrapper>();
			break;

#if WITH_UNREALEXR
		case EImageFormat::EXR:
			ImageWrapper = MakeShared<FExrImageWrapper>();
			break;
#endif
		case EImageFormat::ICNS:
			ImageWrapper = MakeShared<FIcnsImageWrapper>();
			break;

		case EImageFormat::TGA:
			ImageWrapper = MakeShared<FTgaImageWrapper>();
			break;
		case EImageFormat::HDR:
			ImageWrapper = MakeShared<FHdrImageWrapper>();
			break;

#if WITH_LIBTIFF
		case EImageFormat::TIFF:
			ImageWrapper = MakeShared<UE::ImageWrapper::Private::FTiffImageWrapper>();
			break;
#endif // WITH_LIBTIFF

		default:
			break;
		}

		return ImageWrapper;
	}

	virtual EImageFormat DetectImageFormat(const void* CompressedData, int64 CompressedSize) override
	{
		EImageFormat Format = EImageFormat::Invalid;
		if (StartsWith((uint8*) CompressedData, CompressedSize, IMAGE_MAGIC_PNG))
		{
			Format = EImageFormat::PNG;
		}
		else if (StartsWith((uint8*) CompressedData, CompressedSize, IMAGE_MAGIC_JPEG))
		{
			Format = EImageFormat::JPEG; // @Todo: Should we detect grayscale vs non-grayscale?
		}
		else if (StartsWith((uint8*) CompressedData, CompressedSize, IMAGE_MAGIC_BMP))
		{
			Format = EImageFormat::BMP;
		}
		else if (StartsWith((uint8*) CompressedData, CompressedSize, IMAGE_MAGIC_ICO))
		{
			Format = EImageFormat::ICO;
		}
		else if (StartsWith((uint8*) CompressedData, CompressedSize, IMAGE_MAGIC_EXR))
		{
			Format = EImageFormat::EXR;
		}
		else if (StartsWith((uint8*) CompressedData, CompressedSize, IMAGE_MAGIC_ICNS))
		{
			Format = EImageFormat::ICNS;
		}
		else if (StartsWith((uint8*)CompressedData, CompressedSize, IMAGE_MAGIC_HDR))
		{
			Format = EImageFormat::HDR;
		}
		else if (StartsWith((uint8*)CompressedData, CompressedSize, IMAGE_MAGIC_TIFF_LITTLE_ENDIAN))
		{
			Format = EImageFormat::TIFF;
		}
		else if (StartsWith((uint8*)CompressedData, CompressedSize, IMAGE_MAGIC_TIFF_BIG_ENDIAN))
		{
			Format = EImageFormat::TIFF;
		}

		return Format;
	}

public:

	//~ IModuleInterface interface

	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }
};


IMPLEMENT_MODULE(FImageWrapperModule, ImageWrapper);
