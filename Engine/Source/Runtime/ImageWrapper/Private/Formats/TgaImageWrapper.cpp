// Copyright Epic Games, Inc. All Rights Reserved.

#include "TgaImageWrapper.h"
#include "ImageWrapperPrivate.h"

#include "TgaImageSupport.h"


void FTgaImageWrapper::Compress(int32 Quality)
{
	checkf(false, TEXT("TGA compression not supported"));
}

bool FTgaImageWrapper::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
	bool bResult = FImageWrapperBase::SetCompressed(InCompressedData, InCompressedSize);

	return bResult && LoadTGAHeader();
}

void FTgaImageWrapper::Uncompress(const ERGBFormat InFormat, const int32 InBitDepth)
{
	const int32 BytesPerPixel = ( InFormat == ERGBFormat::Gray ? 1 : 4 );
	RawData.AddUninitialized( (int64)Width * Height * BytesPerPixel );

	const int32 TextureDataSize = RawData.Num();
	uint32* TextureData = reinterpret_cast< uint32* >( RawData.GetData() );

	FTGAFileHeader* TgaHeader = reinterpret_cast< FTGAFileHeader* >( CompressedData.GetData() );
	if ( !DecompressTGA_helper( TgaHeader, CompressedData.Num(), TextureData, TextureDataSize ) )
	{
		SetError(TEXT("Error while decompressing a TGA"));
	}
}

bool FTgaImageWrapper::LoadTGAHeader()
{
	check( CompressedData.Num() );

	const FTGAFileHeader* TgaHeader = (FTGAFileHeader*)CompressedData.GetData();

	if ( CompressedData.Num() >= sizeof( FTGAFileHeader ) &&
		( (TgaHeader->ColorMapType == 0 && TgaHeader->ImageTypeCode == 2) ||
		  ( TgaHeader->ColorMapType == 0 && TgaHeader->ImageTypeCode == 3 ) || // ImageTypeCode 3 is greyscale
		  ( TgaHeader->ColorMapType == 0 && TgaHeader->ImageTypeCode == 10 ) ||
		  ( TgaHeader->ColorMapType == 1 && TgaHeader->ImageTypeCode == 1 && TgaHeader->BitsPerPixel == 8 ) ) )
	{
		Width = TgaHeader->Width;
		Height = TgaHeader->Height;
		ColorMapType = TgaHeader->ColorMapType;
		ImageTypeCode = TgaHeader->ImageTypeCode;
		BitDepth = TgaHeader->BitsPerPixel;

		return true;
	}
	else
	{
		return false;
	}
}
