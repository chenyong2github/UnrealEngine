// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/BmpImageWrapper.h"
#include "ImageWrapperPrivate.h"

#include "BmpImageSupport.h"


/**
 * BMP image wrapper class.
 * This code was adapted from UTextureFactory::ImportTexture, but has not been throughly tested.
 */

FBmpImageWrapper::FBmpImageWrapper(bool bInHasHeader, bool bInHalfHeight)
	: FImageWrapperBase()
	, bHasHeader(bInHasHeader)
	, bHalfHeight(bInHalfHeight)
{
}

void FBmpImageWrapper::Uncompress(const ERGBFormat InFormat, const int32 InBitDepth)
{
	RawData.Empty();
	if ( CompressedData.IsEmpty() )
	{
		return;
	}

	const uint8* Buffer = CompressedData.GetData();

	if (!bHasHeader || ((CompressedData.Num()>=sizeof(FBitmapFileHeader)+sizeof(FBitmapInfoHeader)) && Buffer[0]=='B' && Buffer[1]=='M'))
	{
		UncompressBMPData(InFormat, InBitDepth);
	}
}


void FBmpImageWrapper::UncompressBMPData(const ERGBFormat InFormat, const int32 InBitDepth)
{
	// always writes BGRA8 :
	check( InFormat == ERGBFormat::BGRA );
	check( InBitDepth == 8 );
	check( ! CompressedData.IsEmpty() );

	const uint8* Buffer = CompressedData.GetData();
	const FBitmapInfoHeader* bmhdr = nullptr;
	const uint8* Bits = nullptr;
	EBitmapHeaderVersion HeaderVersion = EBitmapHeaderVersion::BHV_BITMAPINFOHEADER;

	if (bHasHeader)
	{
		bmhdr = (FBitmapInfoHeader *)(Buffer + sizeof(FBitmapFileHeader));
		Bits = Buffer + ((FBitmapFileHeader *)Buffer)->bfOffBits;
		HeaderVersion = ((FBitmapFileHeader *)Buffer)->GetHeaderVersion();
	}
	else
	{
		bmhdr = (FBitmapInfoHeader *)Buffer;
		Bits = Buffer + sizeof(FBitmapInfoHeader);
	}

	if (bmhdr->biCompression != BCBI_RGB && bmhdr->biCompression != BCBI_BITFIELDS)
	{
		UE_LOG(LogImageWrapper, Error, TEXT("RLE compression of BMP images not supported"));
		return;
	}

	if (bmhdr->biPlanes==1 && bmhdr->biBitCount==8)
	{
		// Do palette.
		const uint8* bmpal = (uint8*)CompressedData.GetData() + sizeof(FBitmapFileHeader) + sizeof(FBitmapInfoHeader);

		// Set texture properties.
		Width = bmhdr->biWidth;
		const bool bNegativeHeight = (bmhdr->biHeight < 0);
		Height = FMath::Abs(bHalfHeight ? bmhdr->biHeight / 2 : bmhdr->biHeight);
		Format = ERGBFormat::BGRA;
		RawData.Empty(Height * Width * 4);
		RawData.AddUninitialized(Height * Width * 4);

		FColor* ImageData = (FColor*)RawData.GetData();

		// If the number for color palette entries is 0, we need to default to 2^biBitCount entries.  In this case 2^8 = 256
		int32 clrPaletteCount = bmhdr->biClrUsed ? bmhdr->biClrUsed : 256;
		TArray<FColor>	Palette;

		for (int32 i = 0; i < clrPaletteCount; i++)
		{
			Palette.Add(FColor(bmpal[i * 4 + 2], bmpal[i * 4 + 1], bmpal[i * 4 + 0], 255));
		}

		while (Palette.Num() < 256)
		{
			Palette.Add(FColor(0, 0, 0, 255));
		}

		// Copy scanlines, accounting for scanline direction according to the Height field.
		const int32 SrcStride = Align(Width, 4);
		const int32 SrcPtrDiff = bNegativeHeight ? SrcStride : -SrcStride;
		const uint8* SrcPtr = Bits + (bNegativeHeight ? 0 : Height - 1) * SrcStride;

		for (int32 Y = 0; Y < Height; Y++)
		{
			for (int32 X = 0; X < Width; X++)
			{
				*ImageData++ = Palette[SrcPtr[X]];
			}

			SrcPtr += SrcPtrDiff;
		}
	}
	else if (bmhdr->biPlanes==1 && bmhdr->biBitCount==24)
	{
		// Set texture properties.
		Width = bmhdr->biWidth;
		const bool bNegativeHeight = (bmhdr->biHeight < 0);
		Height = FMath::Abs(bHalfHeight ? bmhdr->biHeight / 2 : bmhdr->biHeight);
		Format = ERGBFormat::BGRA;
		RawData.Empty(Height * Width * 4);
		RawData.AddUninitialized(Height * Width * 4);

		uint8* ImageData = RawData.GetData();

		// Copy scanlines, accounting for scanline direction according to the Height field.
		const int32 SrcStride = Align(Width * 3, 4);
		const int32 SrcPtrDiff = bNegativeHeight ? SrcStride : -SrcStride;
		const uint8* SrcPtr = Bits + (bNegativeHeight ? 0 : Height - 1) * SrcStride;

		for (int32 Y = 0; Y < Height; Y++)
		{
			const uint8* SrcRowPtr = SrcPtr;
			for (int32 X = 0; X < Width; X++)
			{
				*ImageData++ = *SrcRowPtr++;
				*ImageData++ = *SrcRowPtr++;
				*ImageData++ = *SrcRowPtr++;
				*ImageData++ = 0xFF;
			}

			SrcPtr += SrcPtrDiff;
		}
	}
	else if (bmhdr->biPlanes==1 && bmhdr->biBitCount==32)
	{
		// Set texture properties.
		Width = bmhdr->biWidth;
		const bool bNegativeHeight = (bmhdr->biHeight < 0);
		Height = FMath::Abs(bHalfHeight ? bmhdr->biHeight / 2 : bmhdr->biHeight);
		Format = ERGBFormat::BGRA;
		RawData.Empty(Height * Width * 4);
		RawData.AddUninitialized(Height * Width * 4);

		uint8* ImageData = RawData.GetData();

		// Copy scanlines, accounting for scanline direction according to the Height field.
		const int32 SrcStride = Width * 4;
		const int32 SrcPtrDiff = bNegativeHeight ? SrcStride : -SrcStride;
		const uint8* SrcPtr = Bits + (bNegativeHeight ? 0 : Height - 1) * SrcStride;

		// Getting the bmiColors member from the BITMAPINFO, which is used as a mask on BitFields compression.
		const FBmiColorsMask* ColorMask = (FBmiColorsMask*)(CompressedData.GetData() + sizeof(FBitmapFileHeader) + sizeof(FBitmapInfoHeader));
		// Header version 4 introduced the option to declare custom color space, so we can't just assume sRGB past that version.
		const bool bAssumeRGBCompression = bmhdr->biCompression == BCBI_RGB
			|| (bmhdr->biCompression == BCBI_BITFIELDS && ColorMask->IsMaskRGB8() && HeaderVersion < EBitmapHeaderVersion::BHV_BITMAPV4HEADER);
		
		if (bAssumeRGBCompression)
		{
			for (int32 Y = 0; Y < Height; Y++)
			{
				const uint8* SrcRowPtr = SrcPtr;
				for (int32 X = 0; X < Width; X++)
				{
					*ImageData++ = *SrcRowPtr++;
					*ImageData++ = *SrcRowPtr++;
					*ImageData++ = *SrcRowPtr++;
					*ImageData++ = 0xFF; //In BCBI_RGB compression the last 8 bits of the pixel are not used.
					SrcRowPtr++;
				}

				SrcPtr += SrcPtrDiff;
			}
		}
		else if (bmhdr->biCompression == BCBI_BITFIELDS)
		{
			//If the header version is V4 or higher we need to make sure we are still using sRGB format
			if (HeaderVersion >= EBitmapHeaderVersion::BHV_BITMAPV4HEADER)
			{
				const FBitmapInfoHeaderV4* bmhdrV4 = (FBitmapInfoHeaderV4*)(Buffer + sizeof(FBitmapFileHeader));
				
				if (bmhdrV4->biCSType != (uint32)EBitmapCSType::BCST_LCS_sRGB && bmhdrV4->biCSType != (uint32)EBitmapCSType::BCST_LCS_WINDOWS_COLOR_SPACE)
				{
					UE_LOG(LogImageWrapper, Error, TEXT("BMP uses an unsupported custom color space definition, sRGB color space will be used instead."));
				}
			}

			//Calculating the bit mask info needed to remap the pixels' color values.
			uint32 TrailingBits[4];
			float MappingRatio[4];
			for (uint32 MaskIndex = 0; MaskIndex < 4; MaskIndex++)
			{
				TrailingBits[MaskIndex] = FMath::CountTrailingZeros(ColorMask->RGBAMask[MaskIndex]);
				const uint32 NumberOfBits = 32 - (TrailingBits[MaskIndex] + FMath::CountLeadingZeros(ColorMask->RGBAMask[MaskIndex]));
				MappingRatio[MaskIndex] = NumberOfBits == 0 ? 0 : (FMath::Exp2(8.f) - 1) / (FMath::Exp2(static_cast<float>(NumberOfBits)) - 1);
			}

			//In header pre-version 4, we should ignore the last 32bit (alpha) content.
			const bool bHasAlphaChannel = ColorMask->RGBAMask[3] != 0 && HeaderVersion >= EBitmapHeaderVersion::BHV_BITMAPV4HEADER;

			for (int32 Y = 0; Y < Height; Y++)
			{
				const uint32* SrcPixel = (uint32*)SrcPtr;
				for (int32 X = 0; X < Width; X++)
				{
					// Set the color values in BGRA order.
					for (int32 ColorIndex = 2; ColorIndex >= 0; ColorIndex--)
					{
						*ImageData++ = FMath::RoundToInt(((*SrcPixel & ColorMask->RGBAMask[ColorIndex]) >> TrailingBits[ColorIndex]) * MappingRatio[ColorIndex]);
					}

					*ImageData++ = bHasAlphaChannel ? FMath::RoundToInt(((*SrcPixel & ColorMask->RGBAMask[3]) >> TrailingBits[3]) * MappingRatio[3]) : 0xFF;

					SrcPixel++;
				}

				SrcPtr += SrcPtrDiff;
			}
		}
		else
		{
			UE_LOG(LogImageWrapper, Error, TEXT("BMP uses an unsupported compression format (%i)"), bmhdr->biCompression)
		}
	}
	else if (bmhdr->biPlanes==1 && bmhdr->biBitCount==16)
	{
		UE_LOG(LogImageWrapper, Error, TEXT("BMP 16 bit format no longer supported. Use terrain tools for importing/exporting heightmaps."));
	}
	else
	{
		UE_LOG(LogImageWrapper, Error, TEXT("BMP uses an unsupported format (%i/%i)"), bmhdr->biPlanes, bmhdr->biBitCount);
	}
}


bool FBmpImageWrapper::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
	bool bResult = FImageWrapperBase::SetCompressed(InCompressedData, InCompressedSize);

	bResult = bResult && (bHasHeader ? LoadBMPHeader() : LoadBMPInfoHeader());	// Fetch the variables from the header info

	if ( ! bResult )
	{
		CompressedData.Reset();
	}

	return bResult;
}


bool FBmpImageWrapper::LoadBMPHeader()
{
	//note: not Endian correct
	const FBitmapInfoHeader* bmhdr = (FBitmapInfoHeader *)(CompressedData.GetData() + sizeof(FBitmapFileHeader));
	const FBitmapFileHeader* bmf   = (FBitmapFileHeader *)(CompressedData.GetData() + 0);
	if ((CompressedData.Num() >= sizeof(FBitmapFileHeader) + sizeof(FBitmapInfoHeader)) && CompressedData.GetData()[0] == 'B' && CompressedData.GetData()[1] == 'M')
	{
		if (bmhdr->biCompression != BCBI_RGB && bmhdr->biCompression != BCBI_BITFIELDS)
		{
			UE_LOG(LogImageWrapper, Error, TEXT("RLE compression of BMP images not supported"));
			return false;
		}

		if (bmhdr->biPlanes==1 && (bmhdr->biBitCount==8 || bmhdr->biBitCount==24 || bmhdr->biBitCount==32))
		{
			// Set texture properties.
			Width = bmhdr->biWidth;
			Height = FMath::Abs(bmhdr->biHeight);
			Format = ERGBFormat::BGRA;
			//  YUCK
			//  BmpImageWrapper was reporting BitDepth *total* not per-channel (eg. 24)
			//  some legacy code knew that and expected to see the bad values, beware
			//BitDepth = bmhdr->biBitCount;
			BitDepth = 8;

			return true;
		}
		
		if (bmhdr->biPlanes == 1 && bmhdr->biBitCount == 16)
		{
			UE_LOG(LogImageWrapper, Error, TEXT("BMP 16 bit format no longer supported. Use terrain tools for importing/exporting heightmaps."));
		}
		else
		{
			UE_LOG(LogImageWrapper, Error, TEXT("BMP uses an unsupported format (%i/%i)"), bmhdr->biPlanes, bmhdr->biBitCount);
		}
	}

	return false;
}


bool FBmpImageWrapper::LoadBMPInfoHeader()
{
	//note: not Endian correct
	const FBitmapInfoHeader* bmhdr = (FBitmapInfoHeader *)CompressedData.GetData();

	if (bmhdr->biCompression != BCBI_RGB && bmhdr->biCompression != BCBI_BITFIELDS)
	{
		UE_LOG(LogImageWrapper, Error, TEXT("RLE compression of BMP images not supported"));
		return false;
	}

	if (bmhdr->biPlanes==1 && (bmhdr->biBitCount==8 || bmhdr->biBitCount==24 || bmhdr->biBitCount==32))
	{
		// Set texture properties.
		Width = bmhdr->biWidth;
		Height = FMath::Abs(bmhdr->biHeight);
		Format = ERGBFormat::BGRA;
		//  BmpImageWrapper was reporting BitDepth *total* not per-channel (eg. 24)
		//BitDepth = bmhdr->biBitCount;
		BitDepth = 8;

		return true;
	}
	
	if (bmhdr->biPlanes == 1 && bmhdr->biBitCount == 16)
	{
		UE_LOG(LogImageWrapper, Error, TEXT("BMP 16 bit format no longer supported. Use terrain tools for importing/exporting heightmaps."));
	}
	else
	{
		UE_LOG(LogImageWrapper, Error, TEXT("BMP uses an unsupported format (%i/%i)"), bmhdr->biPlanes, bmhdr->biBitCount);
	}

	return false;
}

bool FBmpImageWrapper::CanSetRawFormat(const ERGBFormat InFormat, const int32 InBitDepth) const
{
	return ((InFormat == ERGBFormat::BGRA || InFormat == ERGBFormat::Gray) && InBitDepth == 8);
}

ERawImageFormat::Type FBmpImageWrapper::GetSupportedRawFormat(const ERawImageFormat::Type InFormat) const
{
	switch(InFormat)
	{
	case ERawImageFormat::G8:
	case ERawImageFormat::BGRA8:
		return InFormat; // directly supported
	case ERawImageFormat::G16:
		return ERawImageFormat::G8; // needs conversion
	case ERawImageFormat::BGRE8:
	case ERawImageFormat::RGBA16:
	case ERawImageFormat::RGBA16F:
	case ERawImageFormat::RGBA32F:
	case ERawImageFormat::R16F:
	case ERawImageFormat::R32F:
		return ERawImageFormat::BGRA8; // needs conversion
	default:
		check(0);
		return ERawImageFormat::BGRA8;
	};
}


void FBmpImageWrapper::Compress(int32 Quality)
{
	check( Format == ERGBFormat::BGRA || Format == ERGBFormat::Gray );
	check( BitDepth == 8 );

	// write 8,24, or 32 bit bmp

	int64 NumPixels = Width*Height;
	int64 RawDataSize = RawData.Num();

	int RawBytesPerPel = (Format == ERGBFormat::BGRA) ? 4 : 1;

	check( RawDataSize == NumPixels*RawBytesPerPel );

	int OutputBytesPerPel = RawBytesPerPel;

	if ( RawBytesPerPel == 4 )
	{
		// scan for A to choose 24 bit output

		const FColor * RawColors = (const FColor *)RawData.GetData();
		bool bHasAnyAlpha = false;
		for(int64 i=0;i<NumPixels;i++)
		{
			if ( RawColors[i].A != 255 )
			{
				bHasAnyAlpha = true;
				break;
			}
		}

		if ( ! bHasAnyAlpha )
		{
			OutputBytesPerPel = 3;
		}
	}

	bool bWritePal = (RawBytesPerPel == 1);

	int OutputRowBytes = (Width*OutputBytesPerPel + 3)&(~3);
	int OutputPalBytes = bWritePal ? 1024 : 0;
	int64 OutputImageBytes = OutputRowBytes * Height;

	CompressedData.Empty( OutputImageBytes + OutputPalBytes + 1024 );

	// scope to write headers:
	{
		// copied from FFileHelper::CreateBitmap
		// 
		// Types.
		#if PLATFORM_SUPPORTS_PRAGMA_PACK
			#pragma pack (push,1)
		#endif
		struct BITMAPFILEHEADER
		{
			uint16 bfType GCC_PACK(1);
			uint32 bfSize GCC_PACK(1);
			uint16 bfReserved1 GCC_PACK(1); 
			uint16 bfReserved2 GCC_PACK(1);
			uint32 bfOffBits GCC_PACK(1);
		} FH = { };
		struct BITMAPINFOHEADER
		{
			uint32 biSize GCC_PACK(1); 
			int32  biWidth GCC_PACK(1);
			int32  biHeight GCC_PACK(1);
			uint16 biPlanes GCC_PACK(1);
			uint16 biBitCount GCC_PACK(1);
			uint32 biCompression GCC_PACK(1);
			uint32 biSizeImage GCC_PACK(1);
			int32  biXPelsPerMeter GCC_PACK(1); 
			int32  biYPelsPerMeter GCC_PACK(1);
			uint32 biClrUsed GCC_PACK(1);
			uint32 biClrImportant GCC_PACK(1); 
		} IH = { };
		struct BITMAPV4HEADER
		{
			uint32 bV4RedMask GCC_PACK(1);
			uint32 bV4GreenMask GCC_PACK(1);
			uint32 bV4BlueMask GCC_PACK(1);
			uint32 bV4AlphaMask GCC_PACK(1);
			uint32 bV4CSType GCC_PACK(1);
			uint32 bV4EndpointR[3] GCC_PACK(1);
			uint32 bV4EndpointG[3] GCC_PACK(1);
			uint32 bV4EndpointB[3] GCC_PACK(1);
			uint32 bV4GammaRed GCC_PACK(1);
			uint32 bV4GammaGreen GCC_PACK(1);
			uint32 bV4GammaBlue GCC_PACK(1);
		} IHV4 = { };
		#if PLATFORM_SUPPORTS_PRAGMA_PACK
			#pragma pack (pop)
		#endif

		bool bInWriteAlpha = ( OutputBytesPerPel == 4 );
		uint32 InfoHeaderSize = sizeof(BITMAPINFOHEADER) + (bInWriteAlpha ? sizeof(BITMAPV4HEADER) : 0);

		// File header.
		FH.bfType       		= INTEL_ORDER16((uint16) ('B' + 256*'M'));
		FH.bfSize       		= INTEL_ORDER32((uint32) (sizeof(BITMAPFILEHEADER) + InfoHeaderSize + OutputImageBytes + OutputPalBytes));
		FH.bfOffBits    		= INTEL_ORDER32((uint32) (sizeof(BITMAPFILEHEADER) + InfoHeaderSize));
		CompressedData.Append( (const uint8 *) &FH, sizeof(FH) );

		// Info header.
		IH.biSize               = INTEL_ORDER32((uint32) InfoHeaderSize);
		IH.biWidth              = INTEL_ORDER32((uint32) Width);
		IH.biHeight             = INTEL_ORDER32((uint32) Height);
		IH.biPlanes             = INTEL_ORDER16((uint16) 1);
		IH.biBitCount           = INTEL_ORDER16((uint16) OutputBytesPerPel * 8);
		if(bInWriteAlpha)
		{
			IH.biCompression    = INTEL_ORDER32((uint32) 3); //BI_BITFIELDS
		}
		else
		{
			IH.biCompression    = INTEL_ORDER32((uint32) 0); //BI_RGB
		}
		IH.biSizeImage          = INTEL_ORDER32((uint32) OutputImageBytes);
		if ( bWritePal )
		{
			IH.biClrUsed            = INTEL_ORDER32((uint32) 256);
			IH.biClrImportant       = INTEL_ORDER32((uint32) 256);
		}
		CompressedData.Append( (const uint8 *) &IH, sizeof(IH) );

		// If we're writing alpha, we need to write the extra portion of the V4 header
		if (bInWriteAlpha)
		{
			IHV4.bV4RedMask     = INTEL_ORDER32((uint32) 0x00ff0000);
			IHV4.bV4GreenMask   = INTEL_ORDER32((uint32) 0x0000ff00);
			IHV4.bV4BlueMask    = INTEL_ORDER32((uint32) 0x000000ff);
			IHV4.bV4AlphaMask   = INTEL_ORDER32((uint32) 0xff000000);
			IHV4.bV4CSType      = INTEL_ORDER32((uint32) 'Win ');
			CompressedData.Append( (const uint8 *) &IHV4, sizeof(IHV4) );
		}
	}

	if ( bWritePal )
	{
		// write palette for G8 :
		FColor Palette[256];

		for(int i=0;i<256;i++)
		{
			Palette[i] = FColor(i,i,i,255);
		}
		
		check( sizeof(Palette) == OutputPalBytes );
		CompressedData.Append( (const uint8 *)Palette,1024 );
	}

	int64 HeaderBytes = CompressedData.Num();
	CompressedData.SetNum( HeaderBytes + OutputImageBytes );
	uint8 * PayloadPtr = CompressedData.GetData() + HeaderBytes;
	
	int OutputRowPadBytes = OutputRowBytes - Width*OutputBytesPerPel;
	check( OutputRowPadBytes < 4 );

	// write rows :
	switch(OutputBytesPerPel)
	{
	case 1:
	{
		const uint8 * RawPtr = RawData.GetData();
		for(int y=Height-1;y>=0;y--)
		{
			memcpy(PayloadPtr,RawPtr + y * Width,Width);
			PayloadPtr += Width;
			memset(PayloadPtr,0,OutputRowPadBytes);
			PayloadPtr += OutputRowPadBytes;
		}
		break;
	}

	case 3:
	{
		const FColor * RawColors = (const FColor *) RawData.GetData();
		for(int y=Height-1;y>=0;y--)
		{
			const FColor * RawRow = RawColors + y * Width;
			for(int x=0;x<Width;x++)
			{
				*PayloadPtr++ = RawRow[x].B;
				*PayloadPtr++ = RawRow[x].G;
				*PayloadPtr++ = RawRow[x].R;
			}
			memset(PayloadPtr,0,OutputRowPadBytes);
			PayloadPtr += OutputRowPadBytes;
		}
		break;
	}

	case 4:
	{
		check( OutputRowBytes == Width * 4 );
		check( OutputRowPadBytes == 0 );

		const uint8 * RawPtr = RawData.GetData();
		for(int y=Height-1;y>=0;y--)
		{
			memcpy(PayloadPtr,RawPtr + y * OutputRowBytes,OutputRowBytes);
			PayloadPtr += OutputRowBytes;
		}
		break;
	}

	default:
		check(0);
		break;
	}
	
	check( PayloadPtr == CompressedData.GetData() + CompressedData.Num() );
}
