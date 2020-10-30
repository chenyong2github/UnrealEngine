// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS
#include "CoreTypes.h"

/** Log category. */
DECLARE_LOG_CATEGORY_EXTERN(LogExrReaderGpu, Log, All);

/**
* This class is a collection of distant and compressed takes on 
* OpenExr's ScanLineInputFile and ReadPixel functionality.
*/
class EXRREADERGPU_API FExrReader
{
public:

	/** 
	*	This is based on Open Exr implementation.
	*	We need to handle all of these in the future. At the moment only INCREASING_Y is 
	*	supported.
	*/
	enum ELineOrder
	{
		INCREASING_Y,
		DECREASING_Y,
		RANDOM_Y // Tiled files.
	};
private:
	static const int32	STRING_SIZE = 256;
	static const int32	MAX_LENGTH = STRING_SIZE - 1;
	static const int32	PLANAR_RGB_SCANLINE_PADDING = 8;
public:
	FExrReader() :FileHandle(nullptr) {};
private:

	static bool ReadMagicNumberAndVersionField(FILE* FileHandle);

	/** 
	* This is just so we can get line offsets. Based on OpenExr implementation with some caveats.
	* The header in general is discarded, but without reading it - it is impossible to get to the actual pixel data reliably. 
	* Returns false if it has failed reading the file in any way.
	*/
	static bool ReadHeaderData(FILE* FileHandle);

	/** Reads Scan line offsets so that we can jump to a position in file. */
	static bool ReadLineOffsets(FILE* FileHandle, ELineOrder LineOrder, TArray<int64>& LineOffsets);

public:

	/** Discards the header and fills the buffer with plain pixel data. */
	static bool GenerateTextureData(uint16* Buffer, FString FilePath, int32 TextureWidth, int32 TextureHeight, int32& PixelSize, int32 NumChannels)
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
		TArray<int64> LineOffsets;
		{
			ReadMagicNumberAndVersionField(FileHandle);
			ReadHeaderData(FileHandle);
			LineOffsets.SetNum(TextureHeight);

			// At the moment we support only increasing Y.
			ELineOrder LineOrder = INCREASING_Y;
			ReadLineOffsets(FileHandle, LineOrder, LineOffsets);
		}

		fseek(FileHandle, LineOffsets[0], SEEK_SET);
		fread(Buffer, TextureWidth * PixelSize + PLANAR_RGB_SCANLINE_PADDING, TextureHeight, FileHandle);

		fclose(FileHandle);
		return true;
	}

	/** 
	* This function is used to open file and keep a handle to it so the file can be read in chunks.
	* Reading in chunks allows the process of reading to be canceled midway through reading. 
	* This function reads and discards the header and keeps the pointers to the scanlines. 
	*/
	bool OpenExrAndPrepareForPixelReading(FString FilePath, int32 TextureWidth, int32 TextureHeight, int32& PixelSize, int32 NumChannels)
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
			LineOffsets.SetNum(TextureHeight);

			// At the moment we support only increasing Y.
			ELineOrder LineOrder = INCREASING_Y;
			ReadLineOffsets(FileHandle, LineOrder, LineOffsets);
		}

		fseek(FileHandle, LineOffsets[0], SEEK_SET);
		
		return true;
	}

	/** 
	* Read a chunk of an Exr file previously open via OpenExrAndPrepareForPixelReading.
	* The read starts from the last point of file reading.
	*/
	bool ReadExrImageChunk(void* Buffer, int32 ChunkSize)
	{
		if (FileHandle == nullptr)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("File is not open for reading. Please use OpenExrAndPrepareForPixelReading"));
			return false;
		}
		if (Buffer == nullptr)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("Buffer provided is invalid. Please provide a valid buffer. "));
			return false;
		}

		size_t Result = fread(Buffer, ChunkSize, 1, FileHandle);
		if (Result != 1)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("Issue reading EXR chunk. "));
		}
		return Result == 1;
	}

	/**
	* After we have finished reading the file that was open via OpenExrAndPrepareForPixelReading
	* We need to close it to avoid memory leaks
	*/
	bool CloseExrFile()
	{
		if (FileHandle == nullptr)
		{
			UE_LOG(LogExrReaderGpu, Error, TEXT("File is not open for reading. Please use OpenExrAndPrepareForPixelReading"));
			return false;
		}
		fclose(FileHandle);
		LineOffsets.Empty();
		return true;
	}
private:
	/**
	* One approach to reading is to first open a file, read in chunks and then close the file.
	* It is all done in 3 sepate functions and requires to keep track of Filehandle
	*/
	FILE* FileHandle;

	/**
	* These are byte offsets pointing to scanlines from the begining of the file.
	*/
	TArray<int64> LineOffsets;

};
#endif
