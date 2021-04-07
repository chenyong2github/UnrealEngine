// Copyright Epic Games, Inc. All Rights Reserved.

#include "OodleNetworkUtils.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace OodleUtils
{
	bool DecompressReplayData(const TArray<uint8>& InCompressed, TArray< uint8 >& OutBuffer)
	{
		int Size = 0;
		int CompressedSize = 0;

		FMemoryReader Reader(InCompressed);
		Reader << Size;
		Reader << CompressedSize;

		OutBuffer.SetNum(Size, false);

#if 0 // HAS_OODLE_DATA_SDK
		return (OodleLZ_Decompress(InCompressed.GetData() + Reader.Tell(), (SINTa)CompressedSize, OutBuffer.GetData(), OutBuffer.Num(), OodleLZ_FuzzSafe_Yes) == OutBuffer.Num());
#else
		bool bSuccess = FCompression::UncompressMemory(NAME_Zlib, OutBuffer.GetData(), OutBuffer.Num(), (void*)(InCompressed.GetData() + Reader.Tell()), CompressedSize);
		return bSuccess;
#endif
	}

	bool CompressReplayData(const TArray<uint8>& InBuffer, TArray< uint8 >& OutCompressed)
	{
		int Size = InBuffer.Num();
		int CompressedSize = 0;

		FMemoryWriter Writer(OutCompressed);
		Writer << Size;
		Writer << CompressedSize;		// Make space

		int64 ReservedBytes = Writer.Tell();

#if 0 // HAS_OODLE_DATA_SDK

		const OodleLZ_Compressor			Compressor = OodleLZ_Compressor_Selkie;
		const OodleLZ_CompressionLevel		Level = OodleLZ_CompressionLevel_VeryFast;

		OutCompressed.SetNum(ReservedBytes + OodleLZ_GetCompressedBufferSizeNeeded(Compressor, InBuffer.Num()));
		CompressedSize = OodleLZ_Compress(Compressor, InBuffer.GetData(), (SINTa)InBuffer.Num(), OutCompressed.GetData() + ReservedBytes, Level);

		if (CompressedSize == OODLELZ_FAILED)
		{
			return false;
		}
#else
		OutCompressed.SetNum(ReservedBytes + FCompression::CompressMemoryBound(NAME_Zlib, InBuffer.Num()));
		bool bSuccess = FCompression::CompressMemory(NAME_Zlib, OutCompressed.GetData() + ReservedBytes, CompressedSize, InBuffer.GetData(), InBuffer.Num());
		if (!bSuccess)
		{
			//log error?
			return false;
		}
#endif

		Writer.Seek(0);
		Writer << Size;
		Writer << CompressedSize;	// Save compressed size for real

		OutCompressed.SetNum(CompressedSize + ReservedBytes, false);

		return true;
	}
};
