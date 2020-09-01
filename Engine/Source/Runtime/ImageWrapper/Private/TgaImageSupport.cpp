// Copyright Epic Games, Inc. All Rights Reserved.

#include "TgaImageSupport.h"
#include "ImageWrapperPrivate.h"

namespace TgaImageSupportImpl
{
	void DecompressTGA_RLE_32bpp( const FTGAFileHeader* TgaHeader, uint32* TextureData )
	{
		uint8*  IdData      = (uint8*)TgaHeader + sizeof(FTGAFileHeader); 
		uint8*  ColorMap    = IdData + TgaHeader->IdFieldLength;
		uint8*  ImageData   = (uint8*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);				
		uint32  Pixel       = 0;
		int32   RLERun      = 0;
		int32   RAWRun      = 0;

		for ( int32 Y = TgaHeader->Height - 1 ; Y >=0; Y-- ) // Y-flipped.
		{					
			for ( int32 X = 0; X < TgaHeader->Width; X++ )
			{						
				if( RLERun > 0 )
				{
					RLERun--;  // reuse current Pixel data.
				}
				else if( RAWRun == 0 ) // new raw pixel or RLE-run.
				{
					uint8 RLEChunk = *(ImageData++);
					if( RLEChunk & 0x80 )
					{
						RLERun = ( RLEChunk & 0x7F ) + 1;
						RAWRun = 1;
					}
					else
					{
						RAWRun = ( RLEChunk & 0x7F ) + 1;
					}
				}							
				// Retrieve new pixel data - raw run or single pixel for RLE stretch.
				if( RAWRun > 0 )
				{
					Pixel = *(uint32*)ImageData; // RGBA 32-bit dword.
					ImageData += 4;
					RAWRun--;
					RLERun--;
				}
				// Store.
				*( (TextureData + Y*TgaHeader->Width)+X ) = Pixel;
			}
		}
	}

	void DecompressTGA_RLE_24bpp( const FTGAFileHeader* TgaHeader, uint32* TextureData )
	{
		uint8*  IdData = (uint8*)TgaHeader + sizeof(FTGAFileHeader); 
		uint8*  ColorMap = IdData + TgaHeader->IdFieldLength;
		uint8*  ImageData = (uint8*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);
		uint8   Pixel[4] = {};
		int32   RLERun = 0;
		int32   RAWRun = 0;

		for(int32 Y = TgaHeader->Height-1; Y >=0; Y--) // Y-flipped.
		{					
			for(int32 X = 0;X < TgaHeader->Width;X++)
			{						
				if( RLERun > 0 )
					RLERun--;  // reuse current Pixel data.
				else if( RAWRun == 0 ) // new raw pixel or RLE-run.
				{
					uint8 RLEChunk = *(ImageData++);
					if( RLEChunk & 0x80 )
					{
						RLERun = ( RLEChunk & 0x7F ) + 1;
						RAWRun = 1;
					}
					else
					{
						RAWRun = ( RLEChunk & 0x7F ) + 1;
					}
				}							
				// Retrieve new pixel data - raw run or single pixel for RLE stretch.
				if( RAWRun > 0 )
				{
					Pixel[0] = *(ImageData++);
					Pixel[1] = *(ImageData++);
					Pixel[2] = *(ImageData++);
					Pixel[3] = 255;
					RAWRun--;
					RLERun--;
				}
				// Store.
				*( (TextureData + Y*TgaHeader->Width)+X ) = *(uint32*)&Pixel;
			}
		}
	}

	void DecompressTGA_RLE_16bpp( const FTGAFileHeader* TgaHeader, uint32* TextureData )
	{
		uint8*  IdData = (uint8*)TgaHeader + sizeof(FTGAFileHeader);
		uint8*  ColorMap = IdData + TgaHeader->IdFieldLength;				
		uint16* ImageData = (uint16*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);
		uint16  FilePixel = 0;
		uint32  TexturePixel = 0;
		int32   RLERun = 0;
		int32   RAWRun = 0;

		for ( int32 Y = TgaHeader->Height-1; Y >=0; Y-- ) // Y-flipped.
		{					
			for ( int32 X=0;X<TgaHeader->Width;X++ )
			{						
				if ( RLERun > 0 )
				{
					RLERun--;  // reuse current Pixel data.
				}
				else if ( RAWRun == 0 ) // new raw pixel or RLE-run.
				{
					uint8 RLEChunk =  *((uint8*)ImageData);
					ImageData = (uint16*)(((uint8*)ImageData)+1);
					if( RLEChunk & 0x80 )
					{
						RLERun = ( RLEChunk & 0x7F ) + 1;
						RAWRun = 1;
					}
					else
					{
						RAWRun = ( RLEChunk & 0x7F ) + 1;
					}
				}

				// Retrieve new pixel data - raw run or single pixel for RLE stretch.
				if ( RAWRun > 0 )
				{ 
					FilePixel = *(ImageData++);
					RAWRun--;
					RLERun--;
				}

				// Convert file format A1R5G5B5 into pixel format B8G8R8B8
				TexturePixel = (FilePixel & 0x001F) << 3;
				TexturePixel |= (FilePixel & 0x03E0) << 6;
				TexturePixel |= (FilePixel & 0x7C00) << 9;
				TexturePixel |= (FilePixel & 0x8000) << 16;

				// Store.
				*( (TextureData + Y*TgaHeader->Width)+X ) = TexturePixel;
			}
		}
	}

	void DecompressTGA_32bpp( const FTGAFileHeader* TgaHeader, uint32* TextureData )
	{
		uint8*	IdData = (uint8*)TgaHeader + sizeof(FTGAFileHeader);
		uint8*	ColorMap = IdData + TgaHeader->IdFieldLength;
		uint32*	ImageData = (uint32*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);

		for ( int32 Y = 0; Y < TgaHeader->Height; Y++ )
		{
			FMemory::Memcpy(TextureData + Y * TgaHeader->Width,ImageData + (TgaHeader->Height - Y - 1) * TgaHeader->Width,TgaHeader->Width * 4);
		}
	}

	void DecompressTGA_16bpp( const FTGAFileHeader* TgaHeader, uint32* TextureData )
	{
		uint8*	IdData = (uint8*)TgaHeader + sizeof(FTGAFileHeader);
		uint8*	ColorMap = IdData + TgaHeader->IdFieldLength;
		uint16*	ImageData = (uint16*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);
		uint16    FilePixel = 0;
		uint32	TexturePixel = 0;

		for ( int32 Y = TgaHeader->Height - 1; Y >= 0; Y-- )
		{					
			for ( int32 X = 0; X < TgaHeader->Width; X++ )
			{
				FilePixel = *ImageData++;
				// Convert file format A1R5G5B5 into pixel format B8G8R8A8
				TexturePixel = (FilePixel & 0x001F) << 3;
				TexturePixel |= (FilePixel & 0x03E0) << 6;
				TexturePixel |= (FilePixel & 0x7C00) << 9;
				TexturePixel |= (FilePixel & 0x8000) << 16;
				// Store.
				*((TextureData + Y*TgaHeader->Width) + X) = TexturePixel;						
			}
		}
	}

	void DecompressTGA_24bpp( const FTGAFileHeader* TgaHeader, uint32* TextureData )
	{
		uint8*	IdData = (uint8*)TgaHeader + sizeof(FTGAFileHeader);
		uint8*	ColorMap = IdData + TgaHeader->IdFieldLength;
		uint8*	ImageData = (uint8*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);
		uint8    Pixel[4];

		for( int32 Y = 0; Y < TgaHeader->Height; Y++ )
		{
			for( int32 X = 0; X < TgaHeader->Width; X++ )
			{
				Pixel[0] = *(( ImageData+( TgaHeader->Height-Y-1 )*TgaHeader->Width*3 )+X*3+0);
				Pixel[1] = *(( ImageData+( TgaHeader->Height-Y-1 )*TgaHeader->Width*3 )+X*3+1);
				Pixel[2] = *(( ImageData+( TgaHeader->Height-Y-1 )*TgaHeader->Width*3 )+X*3+2);
				Pixel[3] = 255;
				*((TextureData+Y*TgaHeader->Width)+X) = *(uint32*)&Pixel;
			}
		}
	}

	void DecompressTGA_8bpp( const FTGAFileHeader* TgaHeader, uint8* TextureData )
	{
		const uint8*  const IdData = (uint8*)TgaHeader + sizeof(FTGAFileHeader);
		const uint8*  const ColorMap = IdData + TgaHeader->IdFieldLength;
		const uint8*  const ImageData = (uint8*) (ColorMap + (TgaHeader->ColorMapEntrySize + 4) / 8 * TgaHeader->ColorMapLength);

		int32 RevY = 0;
		for (int32 Y = TgaHeader->Height-1; Y >= 0; --Y)
		{
			const uint8* ImageCol = ImageData + (Y * TgaHeader->Width); 
			uint8* TextureCol = TextureData + (RevY++ * TgaHeader->Width);
			FMemory::Memcpy(TextureCol, ImageCol, TgaHeader->Width);
		}
	}
}

bool DecompressTGA_helper( const FTGAFileHeader* TgaHeader, uint32*& TextureData, const int32 TextureDataSize )
{
	if ( TgaHeader->ImageTypeCode == 10 ) // 10 = RLE compressed 
	{
		// RLE compression: CHUNKS: 1 -byte header, high bit 0 = raw, 1 = compressed
		// bits 0-6 are a 7-bit count; count+1 = number of raw pixels following, or rle pixels to be expanded. 
		if(TgaHeader->BitsPerPixel == 32)
		{
			TgaImageSupportImpl::DecompressTGA_RLE_32bpp(TgaHeader, TextureData);
		}
		else if( TgaHeader->BitsPerPixel == 24 )
		{	
			TgaImageSupportImpl::DecompressTGA_RLE_24bpp(TgaHeader, TextureData);
		}
		else if( TgaHeader->BitsPerPixel == 16 )
		{
			TgaImageSupportImpl::DecompressTGA_RLE_16bpp(TgaHeader, TextureData);
		}
		else
		{
			UE_LOG( LogImageWrapper, Error, TEXT("TgaHeader uses an unsupported rle-compressed bit-depth: %u"), TgaHeader->BitsPerPixel );
			return false;
		}
	}
	else if(TgaHeader->ImageTypeCode == 2) // 2 = Uncompressed RGB
	{
		if(TgaHeader->BitsPerPixel == 32)
		{
			TgaImageSupportImpl::DecompressTGA_32bpp(TgaHeader, TextureData);
		}
		else if(TgaHeader->BitsPerPixel == 16)
		{
			TgaImageSupportImpl::DecompressTGA_16bpp(TgaHeader, TextureData);
		}
		else if(TgaHeader->BitsPerPixel == 24)
		{
			TgaImageSupportImpl::DecompressTGA_24bpp(TgaHeader, TextureData);
		}
		else
		{
			UE_LOG( LogImageWrapper, Error, TEXT("TgaHeader uses an unsupported bit-depth: %u"), TgaHeader->BitsPerPixel );
			return false;
		}
	}
	// Support for alpha stored as pseudo-color 8-bit TgaHeader
	else if(TgaHeader->ColorMapType == 1 && TgaHeader->ImageTypeCode == 1 && TgaHeader->BitsPerPixel == 8)
	{
		TgaImageSupportImpl::DecompressTGA_8bpp(TgaHeader, (uint8*)TextureData);
	}
	// standard grayscale
	else if(TgaHeader->ColorMapType == 0 && TgaHeader->ImageTypeCode == 3 && TgaHeader->BitsPerPixel == 8)
	{
		TgaImageSupportImpl::DecompressTGA_8bpp(TgaHeader, (uint8*)TextureData);
	}
	else
	{
		UE_LOG( LogImageWrapper, Error, TEXT("TgaHeader is an unsupported type: %u"), TgaHeader->ImageTypeCode );
		return false;
	}

	// Flip the image data if the flip bits are set in the TgaHeader header.
	const bool bFlipX = (TgaHeader->ImageDescriptor & 0x10) ? 1 : 0;
	const bool bFlipY = (TgaHeader->ImageDescriptor & 0x20) ? 1 : 0;
	if ( bFlipX || bFlipY )
	{
		TArray<uint8> FlippedData;
		FlippedData.AddUninitialized(TextureDataSize);

		int32 NumBlocksX = TgaHeader->Width;
		int32 NumBlocksY = TgaHeader->Height;
		int32 BlockBytes = TgaHeader->BitsPerPixel == 8 ? 1 : 4;

		uint8* MipData = (uint8*)TextureData;

		for( int32 Y = 0; Y < NumBlocksY;Y++ )
		{
			for( int32 X  = 0; X < NumBlocksX; X++ )
			{
				int32 DestX = bFlipX ? (NumBlocksX - X - 1) : X;
				int32 DestY = bFlipY ? (NumBlocksY - Y - 1) : Y;
				FMemory::Memcpy(
					&FlippedData[(DestX + DestY * NumBlocksX) * BlockBytes],
					&MipData[(X + Y * NumBlocksX) * BlockBytes],
					BlockBytes
				);
			}
		}

		FMemory::Memcpy( MipData, FlippedData.GetData(), FlippedData.Num() );
	}

	return true;
}