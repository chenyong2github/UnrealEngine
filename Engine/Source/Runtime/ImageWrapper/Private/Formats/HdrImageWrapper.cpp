// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/HdrImageWrapper.h"

#define LOCTEXT_NAMESPACE "HdrImageWrapper"

namespace UE::ImageWrapper::Private::HdrImageWrapper
{
	FText GetEndOFBufferErrorMessage()
	{
		return LOCTEXT("EndOFBufferError","Reached the end of the buffer before finishing decompressing the hdr. The hdr image is likely corrupted");
	}

	FText GetImageDoneBeforeEndOfBufferErrorMessage()
	{
		return LOCTEXT("IMageDoneButThereIsStilSomeDataToDecompress", "Reached the end of the raw image before finishing decompressing the hdr. The hdr image is likely to be corrupted");
	}
}


bool FHdrImageWrapper::SetCompressedFromView(TArrayView64<const uint8> Data)
{
	CompressedData = Data;

	if (CompressedData.Num() < 11)
	{
		FreeCompressedData();
		return false;
	}

	const uint8* FileDataPtr = CompressedData.GetData();

	char Line[256];

	GetHeaderLine(FileDataPtr, Line);

	if (FCStringAnsi::Strcmp(Line, "#?RADIANCE"))
	{
		FreeCompressedData();
		return false;
	}

	// Todo add validation for the format
	bool bHasRGBEFormat = false;
	while (GetHeaderLine(FileDataPtr, Line) && !bHasRGBEFormat)
	{
		if (!FCStringAnsi::Strcmp(Line,"FORMAT=32-bit_rle_rgbe"))
		{
			bHasRGBEFormat = true;
		}
	}

	if (bHasRGBEFormat)
	{
		while (GetHeaderLine(FileDataPtr, Line))
		{
			char* HeightStr = FCStringAnsi::Strstr(Line, "Y ");
			char* WidthStr = FCStringAnsi::Strstr(Line, "X ");

			if (HeightStr != NULL && WidthStr != NULL)
			{
				// insert a /0 after the height value
				*(WidthStr-2) = 0;

				Height = FCStringAnsi::Atoi(HeightStr+2);
				Width = FCStringAnsi::Atoi(WidthStr+2);

				RGBDataStart = FileDataPtr;
				return true;
			}
		}
	}
	else
	{
		ErrorMessage = LOCTEXT("WrongFormatError", "The hdr buffer use a unsupported format. Only the 32-bit_rle_rgbe format is supported.");
	}
	
	FreeCompressedData();
	return false;
}

bool FHdrImageWrapper::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
	CompressedDataHolder.Reset(InCompressedSize);
	CompressedDataHolder.AddUninitialized(InCompressedSize);
	FMemory::Memcpy(CompressedDataHolder.GetData(), InCompressedData, InCompressedSize);

	return SetCompressedFromView(MakeArrayView(CompressedDataHolder));
}

bool FHdrImageWrapper::SetRaw(const void* InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth, const int32 InBytesPerRow)
{
	// Move the code that export hdr here
	unimplemented();
	return false;
}

bool FHdrImageWrapper::SetAnimationInfo(int32 InNumFrames, int32 InFramerate)
{
	unimplemented();
	return false;
}

TArray64<uint8> FHdrImageWrapper::GetCompressed(int32 Quality)
{
	// Move the code that export hdr here
	unimplemented();
	return {};
}

bool FHdrImageWrapper::GetRaw(const ERGBFormat InFormat, int32 InBitDepth, TArray64<uint8>& OutRawData)
{
	if (InFormat != ERGBFormat::BGRE || InBitDepth != 8)
	{
		ErrorMessage = LOCTEXT("UnSupportedFormatORBitDepth", "The format and/or the bit depth is not supported by the HdrImageWrapper. Only the BGRE format and a bitdepth of 8 is supported");
		return false;
	}

	if (!IsCompressedImageValid())
	{
		return false;
	}

	const int64 SizeRawImageInBytes = Width * Height * 4;
	OutRawData.Reset(SizeRawImageInBytes);
	OutRawData.AddUninitialized(SizeRawImageInBytes);

	const uint8* FileDataPtr = RGBDataStart;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		if (!DecompressScanline(&(OutRawData[Width * Y * 4]), FileDataPtr))
		{
			return false;
		}
	}

	return true;
}

int32 FHdrImageWrapper::GetWidth() const
{
	return Width;
}

int32 FHdrImageWrapper::GetHeight() const
{
	return Height;
}

int32 FHdrImageWrapper::GetBitDepth() const
{
	return 8;
}

ERGBFormat FHdrImageWrapper::GetFormat() const
{
	return ERGBFormat::BGRE;
}

int32 FHdrImageWrapper::GetNumFrames() const
{
	return INDEX_NONE;
}

int32 FHdrImageWrapper::GetFramerate() const
{
	return INDEX_NONE;
}

const FText& FHdrImageWrapper::GetErrorMessage() const
{
	return ErrorMessage;
}

bool FHdrImageWrapper::GetHeaderLine(const uint8*& BufferPos, char Line[256])
{
	char* LinePtr = Line;

	const uint8* EndOfBuffer = CompressedData.GetData() + CompressedData.Num();
	uint32 i;

	for(i = 0; i < 255; ++i)
	{
		if(*BufferPos == 0 || *BufferPos == 10 || *BufferPos == 13)
		{
			++BufferPos;
			break;
		}

		if (BufferPos >= EndOfBuffer)
		{
			ErrorMessage = LOCTEXT("RechedEndOfBufferWhileParsingHeader", "Reached the end of the Hdr buffer before we were done reading the header. The Hdr is invalid");
			return false;
		}

		*LinePtr++ = *BufferPos++;
	}

	Line[i] = 0;

	return true;
}

bool FHdrImageWrapper::DecompressScanline(uint8* Out, const uint8*& In)
{
	// minimum and maxmimum scanline length for encoding
	const int32 MINELEN = 8;
	const int32 MAXELEN = 0x7fff;

	if(Width < MINELEN || Width > MAXELEN)
	{
		return OldDecompressScanline(Out, In, Width);
	}

	uint8 Red = *In;

	if(Red != 2)
	{
		return OldDecompressScanline(Out, In, Width);
	}

	++In;

	uint8 Green = *In++;
	uint8 Blue = *In++;
	uint8 Exponant = *In++;

	if(Green != 2 || Blue & 128)
	{
		*Out++ = Blue;
		*Out++ = Green;
		*Out++ = Red;
		*Out++ = Exponant;
		return OldDecompressScanline(Out, In, Width - 1);
	}

	const uint8* EndOfOutBuffer = Out + Width * Height;
	const uint8* EndOfBuffer = CompressedData.GetData() + CompressedData.Num();
	for(uint32 ChannelRead = 0; ChannelRead < 4; ++ChannelRead)
	{
		// The file is in RGBE but we decompress in BGRE So swap the red and blue
		uint8 CurrentToWrite = ChannelRead;
		if (ChannelRead == 0)
		{
			CurrentToWrite = 2;
		}
		else if (ChannelRead == 2)
		{
			CurrentToWrite = 0;
		}

		uint8* OutSingleChannel = Out + CurrentToWrite;
		int32 MultiRunIndex = 0;
		while ( MultiRunIndex < Width )
		{
			if (In >= EndOfBuffer)
			{
				ErrorMessage = UE::ImageWrapper::Private::HdrImageWrapper::GetEndOFBufferErrorMessage();
				return false;
			}

			uint8 Current = *In++;

			if (Current > 128)
			{
				uint32 Count = Current & 0x7f;

				if (In >= EndOfBuffer)
				{
					ErrorMessage = UE::ImageWrapper::Private::HdrImageWrapper::GetEndOFBufferErrorMessage();
					return false;
				}
				Current = *In++;

				if ( OutSingleChannel + 4 * (Count - 1) >= EndOfOutBuffer)
				{
					ErrorMessage = UE::ImageWrapper::Private::HdrImageWrapper::GetImageDoneBeforeEndOfBufferErrorMessage();
					return false;
				}

				for(uint32 RunIndex = 0; RunIndex < Count; ++RunIndex)
				{
					*OutSingleChannel = Current;
					OutSingleChannel += 4;
				}
				MultiRunIndex += Count;
			}
			else
			{
				uint32 Count = Current;

				if ( OutSingleChannel + 4 * (Count - 1) >= EndOfOutBuffer)
				{
					ErrorMessage = UE::ImageWrapper::Private::HdrImageWrapper::GetImageDoneBeforeEndOfBufferErrorMessage();
					return false;
				}

				for(uint32 RunIndex = 0; RunIndex < Count; ++RunIndex)
				{
					if (In >= EndOfBuffer)
					{
						ErrorMessage = UE::ImageWrapper::Private::HdrImageWrapper::GetEndOFBufferErrorMessage();
						return false;
					}
					*OutSingleChannel = *In++;
					OutSingleChannel += 4;
				}
				MultiRunIndex += Count;
			}
		}
	}

	return true;
}

bool FHdrImageWrapper::OldDecompressScanline(uint8* Out, const uint8*& In, int32 Lenght)
{
	int32 Shift = 0;

	while(Lenght > 0)
	{
		uint8 Red = *In++; 
		uint8 Green = *In++;
		uint8 Blue = *In++; 
		uint8 Exponent = *In++; 

		if(Red == 1 && Green ==1 && Blue == 1)
		{
			int32 Count = ((int32)(Exponent) << Shift);
			Lenght -= Count;

			if ( Lenght < 0 )
			{
				ErrorMessage = LOCTEXT("EndOfLineError","Reached the end of the outputted scanline before finishing decompressing the line. The hdr image is likely to be corrupted");
				return false;
			}

			Red = *(Out - 4); 
			Green = *(Out - 3);
			Blue = *(Out - 2); 
			Exponent = *(Out - 1); 

			for(int32 i = Count; i > 0; --i)
			{
				*Out++ = Blue;
				*Out++ = Green;
				*Out++ = Red;
				*Out++ = Exponent;
			}

			Shift += 8;
		}
		else
		{
			*Out++ = Blue;
			*Out++ = Green;
			*Out++ = Red;
			*Out++ = Exponent;
			Shift = 0;
			--Lenght;
		}
	}

	return true;
}

bool FHdrImageWrapper::IsCompressedImageValid() const
{
	return CompressedData.Num() > 0 && RGBDataStart;
}

void FHdrImageWrapper::FreeCompressedData()
{
	CompressedData = TArrayView64<const uint8>();
	RGBDataStart = nullptr;
	CompressedDataHolder.Empty();
}

#undef LOCTEXT_NAMESPACE
