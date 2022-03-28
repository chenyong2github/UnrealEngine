// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageWrapperBase.h"
#include "ImageWrapperPrivate.h"


/* FImageWrapperBase structors
 *****************************************************************************/

FImageWrapperBase::FImageWrapperBase()
	: Format(ERGBFormat::Invalid)
	, BitDepth(0)
	, Width(0)
	, Height(0)
{ }


/* FImageWrapperBase interface
 *****************************************************************************/

void FImageWrapperBase::Reset()
{
	LastError.Empty();
	
	RawData.Empty();
	CompressedData.Empty();

	Format = ERGBFormat::Invalid;
	BitDepth = 0;
	Width = 0;
	Height = 0;
}


void FImageWrapperBase::SetError(const TCHAR* ErrorMessage)
{
	LastError = ErrorMessage;
}


/* IImageWrapper structors
 *****************************************************************************/

TArray64<uint8> FImageWrapperBase::GetCompressed(int32 Quality)
{
	LastError.Empty();
	Compress(Quality);

	return MoveTemp(CompressedData);
}


bool FImageWrapperBase::GetRaw(const ERGBFormat InFormat, int32 InBitDepth, TArray64<uint8>& OutRawData)
{
	LastError.Empty();
	Uncompress(InFormat, InBitDepth);

	if ( ! LastError.IsEmpty())
	{
		return false;
	}
	
	if ( RawData.IsEmpty() )
	{
		return false;
	}

	OutRawData = MoveTemp(RawData);
	
	return true;
}


bool FImageWrapperBase::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
	Reset();
	RawData.Empty();			// Invalidates the raw data too

	if(InCompressedSize > 0 && InCompressedData != nullptr)
	{
		// this is usually an unnecessary allocation and copy
		// we should decompress directly from the source buffer

		CompressedData.Empty(InCompressedSize);
		CompressedData.AddUninitialized(InCompressedSize);
		FMemory::Memcpy(CompressedData.GetData(), InCompressedData, InCompressedSize);

		return true;
	}

	return false;
}


bool FImageWrapperBase::SetRaw(const void* InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth, const int32 InBytesPerRow)
{
	check(InRawData != NULL);
	check(InRawSize > 0);
	check(InWidth > 0);
	check(InHeight > 0);
	check(InBytesPerRow >= 0);

	Reset();
	CompressedData.Empty();		// Invalidates the compressed data too
	
	if ( ! CanSetRawFormat(InFormat,InBitDepth) )
	{
		UE_LOG(LogImageWrapper, Warning, TEXT("ImageWrapper unsupported format; check CanSetRawFormat; %d x %d"), (int)InFormat,InBitDepth);
		return false;
	}
	
	Format = InFormat;
	BitDepth = InBitDepth;
	Width = InWidth;
	Height = InHeight;

	int BytesPerRow = GetBytesPerRow();
	
	// stride not supported :
	check( InBytesPerRow == 0 || BytesPerRow == InBytesPerRow );

	check( InRawSize == BytesPerRow * Height );

	// this is usually an unnecessary allocation and copy
	// we should compress directly from the source buffer

	RawData.Empty(InRawSize);
	RawData.AddUninitialized(InRawSize);
	FMemory::Memcpy(RawData.GetData(), InRawData, InRawSize);

	return true;
}


int IImageWrapper::GetRGBFormatBytesPerPel(ERGBFormat RGBFormat,int BitDepth)
{
	switch(RGBFormat)
	{
	case ERGBFormat::RGBA:
	case ERGBFormat::BGRA:
		if ( BitDepth == 8 )
		{
			return 4;
		}
		else if ( BitDepth == 16 )
		{
			return 8;
		}
		break;
						
	case ERGBFormat::Gray:
		if ( BitDepth == 8 )
		{
			return 1;
		}
		else if ( BitDepth == 16 )
		{
			return 2;
		}
		break;
			
	case ERGBFormat::BGRE:
		if ( BitDepth == 8 )
		{
			return 4;
		}
		break;
			
	case ERGBFormat::RGBAF:
		if ( BitDepth == 16 )
		{
			return 8;
		}
		else if ( BitDepth == 32 )
		{
			return 16;
		}
		break;
			
	case ERGBFormat::GrayF:
		if ( BitDepth == 16 )
		{
			return 2;
		}
		else if ( BitDepth == 32 )
		{
			return 4;
		}
		break;

	default:
		break;
	}

	UE_LOG(LogImageWrapper, Error, TEXT("GetRGBFormatBytesPerPel not handled : %d,%d"), (int)RGBFormat,BitDepth );
	return 0;
}


ERawImageFormat::Type IImageWrapper::ConvertRGBFormat(ERGBFormat RGBFormat,int BitDepth,bool * bIsExactMatch)
{
	bool bIsExactMatchDummy;
	if ( ! bIsExactMatch )
	{
		bIsExactMatch = &bIsExactMatchDummy;
	}
		
	switch(RGBFormat)
	{
	case ERGBFormat::RGBA:
		if ( BitDepth == 8 )
		{
			*bIsExactMatch = false; // needs RB swap
			return ERawImageFormat::BGRA8;
		}
		else if ( BitDepth == 16 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::RGBA16;
		}
		break;
			
	case ERGBFormat::BGRA:
		if ( BitDepth == 8 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::BGRA8;
		}
		else if ( BitDepth == 16 )
		{
			*bIsExactMatch = false; // needs RB swap
			return ERawImageFormat::RGBA16;
		}
		break;
			
	case ERGBFormat::Gray:
		if ( BitDepth == 8 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::G8;
		}
		else if ( BitDepth == 16 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::G16;
		}
		break;
			
	case ERGBFormat::BGRE:
		if ( BitDepth == 8 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::BGRE8;
		}
		break;
			
	case ERGBFormat::RGBAF:
		if ( BitDepth == 16 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::RGBA16F;
		}
		else if ( BitDepth == 32 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::RGBA32F;
		}
		break;
			
	case ERGBFormat::GrayF:
		if ( BitDepth == 16 )
		{
			*bIsExactMatch = true;
			return ERawImageFormat::R16F;
		}
		else if ( BitDepth == 32 )
		{
			*bIsExactMatch = false; // no single channel F32
			return ERawImageFormat::RGBA32F; // promote F32 to 4xF32 for no quality loss (alternative is 1xF16)
			//@todo Oodle: add ERawImageFormat::R32F ?
		}
		break;

	default:
		break;
	}

	UE_LOG(LogImageWrapper, Warning, TEXT("ConvertRGBFormat not handled : %d,%d"), (int)RGBFormat,BitDepth );
		
	*bIsExactMatch = false;
	return ERawImageFormat::Invalid;
}
	
void IImageWrapper::ConvertRawImageFormat(ERawImageFormat::Type RawFormat, ERGBFormat & OutFormat,int & OutBitDepth)
{
	switch(RawFormat)
	{
	case ERawImageFormat::G8:
		OutFormat = ERGBFormat::Gray;
		OutBitDepth = 8;
		break;
	case ERawImageFormat::BGRA8:
		OutFormat = ERGBFormat::BGRA;
		OutBitDepth = 8;
		break;
	case ERawImageFormat::BGRE8:
		OutFormat = ERGBFormat::BGRE;
		OutBitDepth = 8;
		break;
	case ERawImageFormat::RGBA16:
		OutFormat = ERGBFormat::RGBA;
		OutBitDepth = 16;
		break;
	case ERawImageFormat::RGBA16F:
		OutFormat = ERGBFormat::RGBAF;
		OutBitDepth = 16;
		break;
	case ERawImageFormat::RGBA32F:
		OutFormat = ERGBFormat::RGBAF;
		OutBitDepth = 32;
		break;
	case ERawImageFormat::G16:
		OutFormat = ERGBFormat::Gray;
		OutBitDepth = 16;
		break;
	case ERawImageFormat::R16F:
		OutFormat = ERGBFormat::GrayF;
		OutBitDepth = 16;
		break;
	default:
		check(0);
		break;
	}
}

bool IImageWrapper::GetRawImage(FImage & OutImage)
{
	TArray64<uint8> OutRawData;
	if ( ! GetRaw(OutRawData) )
	{
		return false;
	}

	int Width = GetWidth();
	int Height = GetHeight();
	ERGBFormat RGBFormat = GetFormat();
	int BitDepth = GetBitDepth();

	bool bExactMatch;
	ERawImageFormat::Type RawFormat = GetClosestRawImageFormat(&bExactMatch);
	
	if ( RawFormat == ERawImageFormat::Invalid )
	{
		return false;
	}

	EGammaSpace GammaSpace = EGammaSpace::Linear;
	if ( GetSRGB() )
	{
		GammaSpace = EGammaSpace::sRGB;
	}

	if ( bExactMatch )
	{
		// no conversion required

		OutImage.RawData = MoveTemp(OutRawData);
		OutImage.SizeX = Width;
		OutImage.SizeY = Height;
		OutImage.NumSlices = 1;
		OutImage.Format = RawFormat;
		OutImage.GammaSpace = GammaSpace;
	}
	else
	{
		OutImage.Init( Width, Height, RawFormat, GammaSpace );

		FImageView SrcImage = OutImage;
		SrcImage.RawData = OutRawData.GetData();

		switch(RGBFormat)
		{
		case ERGBFormat::RGBA:
		{
			// RGBA8 -> BGRA8
			check( BitDepth == 8 );
			check( RawFormat == ERawImageFormat::BGRA8 );
			FImageCore::CopyImageRGBABGRA(SrcImage, OutImage );
			break;
		}
			
		case ERGBFormat::BGRA:
		{
			// BGRA16 -> RGBA16
			check( BitDepth == 16 );
			check( RawFormat == ERawImageFormat::RGBA16 );
			FImageCore::CopyImageRGBABGRA(SrcImage, OutImage );
			break;
		}

		case ERGBFormat::GrayF:
		{
			/*
			// 1 channel F32 -> F16 :
			//  because ERawImageFormat has no 1 channel F32 currently
			check( BitDepth == 32 );
			check( RawFormat == ERawImageFormat::R16F );
			const float * Src = (const float *)OutRawData.GetData();
			uint16 * Dst = (uint16 *)OutImage.RawData.GetData();
			int64 NumPixels = OutImage.GetNumPixels();
			for(int64 i=0;i<NumPixels;i++)
			{
				// clamp in F16 range? does StoreHalf do that?
				FPlatformMath::StoreHalf(&Dst[i], Src[i]);
			}
			*/

			// 1 channel F32 -> 4xF32 :
			check( BitDepth == 32 );
			check( RawFormat == ERawImageFormat::RGBA32F );
			const float * Src = (const float *)OutRawData.GetData();
			FLinearColor * Dst = (FLinearColor *)OutImage.RawData.GetData();
			int64 NumPixels = OutImage.GetNumPixels();
			for(int64 i=0;i<NumPixels;i++)
			{
				Dst[i] = FLinearColor( Src[i],Src[i],Src[i],1.f );
			}

			break;
		}

		default:
			check(0);
			return false;			
		}
	}

	return true;
}
