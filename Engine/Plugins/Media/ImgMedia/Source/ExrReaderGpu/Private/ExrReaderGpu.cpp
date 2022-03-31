// Copyright Epic Games, Inc. All Rights Reserved.
#include "ExrReaderGpu.h"

#if PLATFORM_WINDOWS

#include "ExrReaderGpuModule.h"

PRAGMA_DEFAULT_VISIBILITY_START
THIRD_PARTY_INCLUDES_START
	#include "Imath/ImathBox.h"
	#include "OpenEXR/ImfCompressionAttribute.h"
	#include "OpenEXR/ImfHeader.h"
	#include "OpenEXR/ImfRgbaFile.h"
	#include "OpenEXR/ImfStandardAttributes.h"
	#include "OpenEXR/ImfVersion.h"
THIRD_PARTY_INCLUDES_END
PRAGMA_DEFAULT_VISIBILITY_END

namespace
{
	bool ReadBytes(FILE* FileHandle, char* Buffer, int32 Length)
	{
		int32 Result = -1;
		try
		{
			Result = fread(Buffer, 1, Length, FileHandle);
		}
		catch (...)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("Error reading bytes"));
			return false;
		}
		
		if (Result != Length)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("Error reading file %i %i"), ferror(FileHandle), feof(FileHandle));
			return false;
		}
		return true;
	}

	template <int32 Length>
	bool CheckIsNullTerminated(const char(&StringToCheck)[Length])
	{
		for (int32 i = 0; i < Length; ++i) 
		{
			if (StringToCheck[i] == '\0')
			{
				return true;
			}
		}

		// The string was never terminated.
		UE_LOG(LogExrReaderGpu, Error, TEXT("Invalid EXR file with string that was never terminated"));
		return false;
	}

	bool Read4ByteValue(FILE* In, int32& OutValue)
	{
		char ThrowAwayValue[4];
		if (!ReadBytes(In, ThrowAwayValue, 4))
		{
			return false;
		}

		OutValue = (static_cast <unsigned char> (ThrowAwayValue[0]) & 0x000000ff) |
			((static_cast <unsigned char> (ThrowAwayValue[1]) << 8) & 0x0000ff00) |
			((static_cast <unsigned char> (ThrowAwayValue[2]) << 16) & 0x00ff0000) |
			(static_cast <unsigned char> (ThrowAwayValue[3]) << 24);
		return true;
	}


	bool Read8ByteValue(FILE* InFileHandle, int64& OutValue)
	{
		unsigned char ThrowAwayValue[8];
		if (!ReadBytes(InFileHandle, reinterpret_cast<char*>(ThrowAwayValue), 8))
		{
			return false;
		}

		OutValue = ((uint64)ThrowAwayValue[0] & 0x00000000000000ffLL) |
			(((uint64)ThrowAwayValue[1] << 8) & 0x000000000000ff00LL) |
			(((uint64)ThrowAwayValue[2] << 16) & 0x0000000000ff0000LL) |
			(((uint64)ThrowAwayValue[3] << 24) & 0x00000000ff000000LL) |
			(((uint64)ThrowAwayValue[4] << 32) & 0x000000ff00000000LL) |
			(((uint64)ThrowAwayValue[5] << 40) & 0x0000ff0000000000LL) |
			(((uint64)ThrowAwayValue[6] << 48) & 0x00ff000000000000LL) |
			((uint64)ThrowAwayValue[7] << 56);
		return true;
	}

	bool ReadString(FILE* InFileHandle, int32 Length, char OutValue[])
	{
		while (Length >= 0)
		{
			if (!ReadBytes(InFileHandle, OutValue, 1))
			{
				return false;
			}

			if (*OutValue == 0)
			{
				break;
			}

			--Length;
			++OutValue;
		}
		return true;
	}

	bool ReadString(FILE* InFileHandle, int32 InSize)
	{
		TArray<char> Value;
		Value.SetNum(InSize);

		for (int32 CharNum = 0; CharNum < InSize; CharNum++)
		{
			if (!ReadBytes(InFileHandle, &Value[CharNum], 1))
			{
				return false;
			}
		}
		return true;
	}

}

bool FExrReader::ReadMagicNumberAndVersionField(FILE* FileHandle)
{
	using namespace OPENEXR_IMF_INTERNAL_NAMESPACE;

	int32 Magic;
	int32 Version;
	if (!Read4ByteValue(FileHandle, Magic))
	{
		return false;
	}
	Read4ByteValue(FileHandle, Version);

	if (Magic != MAGIC || getVersion(Version) != EXR_VERSION || !supportsFlags(getFlags(Version)))
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("Invalid EXR file has been detected."));
		return false;
	}

	return true;
}

bool FExrReader::ReadHeaderData(FILE* FileHandle)
{
	int32 AttrCount = 0;
	bool bNothingIsRead = false;

	// Read all header attributes.
	while (true)
	{
		char AttributeName[STRING_SIZE];
		ReadString(FileHandle, MAX_LENGTH, AttributeName);

		// Check to make sure it is not null. And if it is - its the end of the header.
		if (AttributeName[0] == 0)
		{
			if (AttrCount == 0)
			{
				bNothingIsRead = true;
			}

			break;
		}

		AttrCount++;

		// Read the attribute type and the size of the attribute value.
		char TypeName[STRING_SIZE];
		int32 Size;

		ReadString(FileHandle, MAX_LENGTH, TypeName);
		CheckIsNullTerminated(TypeName);
		Read4ByteValue(FileHandle, Size);

		if (Size < 0)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("Invalid EXR file has been detected."));
			return false;
		}

		try
		{
			ReadString(FileHandle, Size);
		}
		catch (...)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("Issue reading attribute from EXR: %s"), AttributeName);
			return false;
		}
	}
	return true;
}

bool FExrReader::ReadLineOrTileOffsets(FILE* FileHandle, ELineOrder LineOrder, TArray<int64>& LineOrTileOffsets)
{
	// At the moment we only support increasing Y EXR; 
	// LineOrder is currently unused but we might need to take that into account as exr scanlines can go from top to bottom and vice versa.
	if (LineOrder != INCREASING_Y)
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("Unsupported Line order in EXR: %s"), LineOrder);
		return false;
	}

	for (int32 i = 0; i < LineOrTileOffsets.Num(); i++)
	{
		Read8ByteValue(FileHandle, LineOrTileOffsets[i]);
	}
	return true;
}

bool FExrReader::GenerateTextureData(uint16* Buffer, int32 BufferSize, FString FilePath, int32 NumberOfScanlines, int32 NumChannels)
{
	check(Buffer != nullptr);

	FILE* FileHandle;
	errno_t Error = fopen_s(&FileHandle, TCHAR_TO_ANSI(*FilePath), "rb");

	if (FileHandle == NULL || Error != 0)
	{
		return false;
	}

	fseek(FileHandle, 0, SEEK_END);
	int64 FileLength = ftell(FileHandle);
	rewind(FileHandle);

	// Reading header (throwaway) and line offset data. We just need line offset data.
	TArray<int64> LineOrTileOffsets;
	{
		ReadMagicNumberAndVersionField(FileHandle);
		ReadHeaderData(FileHandle);
		LineOrTileOffsets.SetNum(NumberOfScanlines);

		// At the moment we support only increasing Y.
		ELineOrder LineOrder = INCREASING_Y;
		ReadLineOrTileOffsets(FileHandle, LineOrder, LineOrTileOffsets);
	}

	fseek(FileHandle, LineOrTileOffsets[0], SEEK_SET);
	fread(Buffer, BufferSize, 1 /*NumOfElements*/, FileHandle);

	fclose(FileHandle);
	return true;
}

bool FExrReader::OpenExrAndPrepareForPixelReading(FString FilePath, int32 NumOffsets)
{
	if (FileHandle != nullptr)
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("The file has already been open for reading but never closed."));
		return false;
	}

	errno_t Error = fopen_s(&FileHandle, TCHAR_TO_ANSI(*FilePath), "rb");

	if (FileHandle == NULL || Error != 0)
	{
		return false;
	}

	// Reading header (throwaway) and line offset data. We just need line offset data.
	{
		ReadMagicNumberAndVersionField(FileHandle);
		if (!ReadHeaderData(FileHandle))
		{
			return false;
		}
		LineOrTileOffsets.SetNum(NumOffsets);

		// At the moment we support only increasing Y.
		ELineOrder LineOrder = INCREASING_Y;
		ReadLineOrTileOffsets(FileHandle, LineOrder, LineOrTileOffsets);
	}

	fseek(FileHandle, LineOrTileOffsets[0], SEEK_SET);

	return true;
}

bool FExrReader::ReadExrImageChunk(void* Buffer, int64 ChunkSize)
{
	if (FileHandle == nullptr)
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("File is not open for reading. Please use OpenExrAndPrepareForPixelReading. "));
		return false;
	}
	if (Buffer == nullptr)
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("Buffer provided is invalid. Please provide a valid buffer. "));
		return false;
	}
	size_t NumElements = 1;
	size_t NumElementsRead = fread(Buffer, ChunkSize, NumElements, FileHandle);
	if (NumElementsRead != NumElements)
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("Issue reading EXR chunk. "));
	}
	return NumElementsRead == NumElements;
}

bool FExrReader::SeekTileWithinFile(const int32 StartTileIndex, const FIntPoint& TexDimInTiles, int64& OutBufferOffset)
{
	if (StartTileIndex >= LineOrTileOffsets.Num())
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("Tile index is invalid."));
		return false;
	}
	int64 LineOffset = LineOrTileOffsets[StartTileIndex];
	OutBufferOffset = LineOffset - LineOrTileOffsets[0];
	return fseek(FileHandle, LineOffset, SEEK_SET) == 0;
}

bool FExrReader::SeekTileWithinFileCustom(const int32 StartTileIndex, const int64 TileStride, int64& OutBufferOffset)
{
	// Our custom exr is written as one single tile. In Exr every tile starts with 20 byte padding.
	// Therefore we want to ignore it, when we read custom tiles.
	const int32 TilePadding = 20;
	int64 LineOffset = TilePadding + LineOrTileOffsets[0] + StartTileIndex * TileStride;
	OutBufferOffset = StartTileIndex * TileStride;
	return fseek(FileHandle, LineOffset, SEEK_SET) == 0;
}

bool FExrReader::CloseExrFile()
{
	if (FileHandle == nullptr)
	{
		UE_LOG(LogExrReaderGpu, Error, TEXT("File is not open for reading. Please use OpenExrAndPrepareForPixelReading"));
		return false;
	}
	fclose(FileHandle);
	LineOrTileOffsets.Empty();
	return true;
}

#endif