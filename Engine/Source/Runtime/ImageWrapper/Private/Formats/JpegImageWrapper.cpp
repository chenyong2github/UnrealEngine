// Copyright Epic Games, Inc. All Rights Reserved.

#include "JpegImageWrapper.h"

#include "Math/Color.h"
#include "Misc/ScopeLock.h"

#if WITH_UNREALJPEG

#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wshadow"

	#if PLATFORM_UNIX || PLATFORM_MAC
		#pragma clang diagnostic ignored "-Wshift-negative-value"	// clang 3.7.0
	#endif
#endif

PRAGMA_DISABLE_SHADOW_VARIABLE_WARNINGS
#include "jpgd.h"
#include "jpgd.cpp"
#include "jpge.h"
#include "jpge.cpp"
PRAGMA_ENABLE_SHADOW_VARIABLE_WARNINGS

#if WITH_LIBJPEGTURBO
THIRD_PARTY_INCLUDES_START
#pragma push_macro("DLLEXPORT")
#undef DLLEXPORT // libjpeg-turbo defines DLLEXPORT as well
#include "turbojpeg.h"
#pragma pop_macro("DLLEXPORT")
THIRD_PARTY_INCLUDES_END
#endif	// WITH_LIBJPEGTURBO

#ifdef __clang__
	#pragma clang diagnostic pop
#endif

DEFINE_LOG_CATEGORY_STATIC(JPEGLog, Log, All);

#if WITH_LIBJPEGTURBO
namespace
{
	int ConvertTJpegPixelFormat(ERGBFormat InFormat)
	{
		switch (InFormat)
		{
			case ERGBFormat::BGRA:	return TJPF_BGRA;
			case ERGBFormat::Gray:	return TJPF_GRAY;
			case ERGBFormat::RGBA:	return TJPF_RGBA;
			default:				return TJPF_RGBA;
		}
	}
}
#endif	// WITH_LIBJPEGTURBO

// Only allow one thread to use JPEG decoder at a time (it's not thread safe)
FCriticalSection GJPEGSection; 


/* FJpegImageWrapper structors
 *****************************************************************************/

FJpegImageWrapper::FJpegImageWrapper(int32 InNumComponents)
	: FImageWrapperBase()
	, NumComponents(InNumComponents)
#if WITH_LIBJPEGTURBO
	, Compressor(tjInitCompress())
	, Decompressor(tjInitDecompress())
#endif	// WITH_LIBJPEGTURBO
{ }


#if WITH_LIBJPEGTURBO
FJpegImageWrapper::~FJpegImageWrapper()
{
	FScopeLock JPEGLock(&GJPEGSection);

	if (Compressor)
	{
		tjDestroy(Compressor);
	}
	if (Decompressor)
	{
		tjDestroy(Decompressor);
	}
}
#endif	// WITH_LIBJPEGTURBO

/* FImageWrapperBase interface
 *****************************************************************************/

bool FJpegImageWrapper::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
#if WITH_LIBJPEGTURBO
	return SetCompressedTurbo(InCompressedData, InCompressedSize);
#else
	// jpgd doesn't support 64-bit sizes.
	if (InCompressedSize < 0 || InCompressedSize > MAX_uint32)
	{
		return false;
	}

	jpgd::jpeg_decoder_mem_stream jpeg_memStream((uint8*)InCompressedData, (uint32)InCompressedSize);

	jpgd::jpeg_decoder decoder(&jpeg_memStream);
	if (decoder.get_error_code() != jpgd::JPGD_SUCCESS)
		return false;

	bool bResult = FImageWrapperBase::SetCompressed(InCompressedData, InCompressedSize);

	// We don't support 16 bit jpegs
	BitDepth = 8;

	Width = decoder.get_width();
	Height = decoder.get_height();

	switch (decoder.get_num_components())
	{
	case 1:
		Format = ERGBFormat::Gray;
		break;
	case 3:
		Format = ERGBFormat::RGBA;
		break;
	default:
		return false;
	}

	return bResult;
#endif
}


bool FJpegImageWrapper::SetRaw(const void* InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth)
{
	check((InFormat == ERGBFormat::RGBA || InFormat == ERGBFormat::BGRA || InFormat == ERGBFormat::Gray) && InBitDepth == 8);

	bool bResult = FImageWrapperBase::SetRaw(InRawData, InRawSize, InWidth, InHeight, InFormat, InBitDepth);

	return bResult;
}


void FJpegImageWrapper::Compress(int32 Quality)
{
#if WITH_LIBJPEGTURBO
	CompressTurbo(Quality);
#else
	if (CompressedData.Num() == 0)
	{
		FScopeLock JPEGLock(&GJPEGSection);
		
		if (Quality == 0) {Quality = 85;}
		ensure(Quality >= 1 && Quality <= 100);
		Quality = FMath::Clamp(Quality, 1, 100);

		check(RawData.Num());
		check(Width > 0);
		check(Height > 0);

		// re-order components if required - JPEGs expect RGBA
		if(RawFormat == ERGBFormat::BGRA)
		{
			FColor* Colors = (FColor*)RawData.GetData();
			const int32 NumColors = RawData.Num() / 4;
			for(int32 ColorIndex = 0; ColorIndex < NumColors; ColorIndex++)
			{
				Colors[ColorIndex].B = Colors[ColorIndex].B ^ Colors[ColorIndex].R;
				Colors[ColorIndex].R = Colors[ColorIndex].R ^ Colors[ColorIndex].B;
				Colors[ColorIndex].B = Colors[ColorIndex].B ^ Colors[ColorIndex].R;
			}
		}

		CompressedData.Reset(RawData.Num());
		CompressedData.AddUninitialized(RawData.Num());

		// Note: OutBufferSize intentionally uses int64_t type as that's what jpge::compress_image_to_jpeg_file_in_memory expects.
		// UE4 int64 type is not compatible with int64_t on all compilers (int64_t may be `long`, while int64 is `long long`).
		int64_t OutBufferSize = CompressedData.Num();

		jpge::params Parameters;
		Parameters.m_quality = Quality;
		bool bSuccess = jpge::compress_image_to_jpeg_file_in_memory(
			CompressedData.GetData(), OutBufferSize, Width, Height, NumComponents, RawData.GetData(), Parameters);
		
		check(bSuccess);

		CompressedData.RemoveAt((int64)OutBufferSize, CompressedData.Num() - (int64)OutBufferSize);
	}
#endif
}


void FJpegImageWrapper::Uncompress(const ERGBFormat InFormat, int32 InBitDepth)
{
#if WITH_LIBJPEGTURBO
	UncompressTurbo(InFormat, InBitDepth);
#else
	// Ensure we haven't already uncompressed the file.
	if (RawData.Num() != 0)
	{
		return;
	}

	// Get the number of channels we need to extract
	int Channels = 0;
	if ((InFormat == ERGBFormat::RGBA || InFormat == ERGBFormat::BGRA) && InBitDepth == 8)
	{
		Channels = 4;
	}
	else if (InFormat == ERGBFormat::Gray && InBitDepth == 8)
	{
		Channels = 1;
	}
	else
	{
		check(false);
	}

	FScopeLock JPEGLock(&GJPEGSection);

	check(CompressedData.Num());

	int32 NumColors;
	uint8* OutData = jpgd::decompress_jpeg_image_from_memory(
		CompressedData.GetData(), CompressedData.Num(), &Width, &Height, &NumColors, Channels, (int)InFormat);


	RawData.Reset(Width * Height * Channels);
	RawData.AddUninitialized(Width * Height * Channels);
	if (OutData)
	{
		FMemory::Memcpy(RawData.GetData(), OutData, RawData.Num());
		FMemory::Free(OutData);
	}
#endif
}

#if WITH_LIBJPEGTURBO
bool FJpegImageWrapper::SetCompressedTurbo(const void* InCompressedData, int64 InCompressedSize)
{
	FScopeLock JPEGLock(&GJPEGSection);

	check(Decompressor);

	int ImageWidth;
	int ImageHeight;
	int SubSampling;
	int ColorSpace;
	if (tjDecompressHeader3(Decompressor, reinterpret_cast<const uint8*>(InCompressedData), InCompressedSize, &ImageWidth, &ImageHeight, &SubSampling, &ColorSpace) != 0)
	{
		return false;
	}

	const bool bResult = FImageWrapperBase::SetCompressed(InCompressedData, InCompressedSize);

	// set after call to base SetCompressed as it will reset members
	Width = ImageWidth;
	Height = ImageHeight;
	BitDepth = 8; // We don't support 16 bit jpegs
	Format = SubSampling == TJSAMP_GRAY ? ERGBFormat::Gray : ERGBFormat::RGBA;

	return bResult;
}

void FJpegImageWrapper::CompressTurbo(int32 Quality)
{
	if (CompressedData.Num() == 0)
	{
		FScopeLock JPEGLock(&GJPEGSection);

		check(Compressor);

		if (Quality == 0) { Quality = 85; }
		ensure(Quality >= 1 && Quality <= 100);
		Quality = FMath::Clamp(Quality, 1, 100);

		check(RawData.Num());
		check(Width > 0);
		check(Height > 0);

		CompressedData.Reset(RawData.Num());
		CompressedData.AddUninitialized(RawData.Num());

		const int PixelFormat = ConvertTJpegPixelFormat(RawFormat);
		unsigned char* OutBuffer = CompressedData.GetData();
		unsigned long OutBufferSize = static_cast<unsigned long>(CompressedData.Num());
		const int Flags = TJFLAG_NOREALLOC | TJFLAG_FASTDCT;

		const bool bSuccess = tjCompress2(Compressor, RawData.GetData(), Width, 0, Height, PixelFormat, &OutBuffer, &OutBufferSize, TJSAMP_420, Quality, Flags) == 0;
		check(bSuccess);

		CompressedData.RemoveAt((int64)OutBufferSize, CompressedData.Num() - (int64)OutBufferSize);
	}
}

void FJpegImageWrapper::UncompressTurbo(const ERGBFormat InFormat, int32 InBitDepth)
{
	// Ensure we haven't already uncompressed the file.
	if (RawData.Num() != 0)
	{
		return;
	}

	// Get the number of channels we need to extract
	int Channels = 0;
	if ((InFormat == ERGBFormat::RGBA || InFormat == ERGBFormat::BGRA) && InBitDepth == 8)
	{
		Channels = 4;
	}
	else if (InFormat == ERGBFormat::Gray && InBitDepth == 8)
	{
		Channels = 1;
	}
	else
	{
		check(false);
	}

	FScopeLock JPEGLock(&GJPEGSection);

	check(Decompressor);
	check(CompressedData.Num());

	RawData.Reset(Width * Height * Channels);
	RawData.AddUninitialized(Width * Height * Channels);
	const int PixelFormat = ConvertTJpegPixelFormat(InFormat);
	const int Flags = TJFLAG_NOREALLOC | TJFLAG_FASTDCT;

	if (tjDecompress2(Decompressor, CompressedData.GetData(), CompressedData.Num(), RawData.GetData(), Width, 0, Height, PixelFormat, Flags) != 0)
	{
		return;
	}
}
#endif	// WITH_LIBJPEGTURBO


#endif //WITH_JPEG
