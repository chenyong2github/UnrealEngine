// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/OodleDataCompressionUtil.h"
#include "Templates/CheckValueCast.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace FOodleDataCompressionUtil
{
	bool CORE_API DecompressReplayData(const TArray<uint8>& InCompressed, TArray< uint8 >& OutBuffer)
	{
		int32 Size = 0;
		int32 CompressedSize = 0;

		FMemoryReader Reader(InCompressed);
		Reader << Size;
		Reader << CompressedSize;

		OutBuffer.SetNum(Size, false);

		if ( Reader.Tell() + CompressedSize > InCompressed.Num() )
			return false;

		return FOodleDataCompression::Decompress(OutBuffer.GetData(), OutBuffer.Num(), InCompressed.GetData() + Reader.Tell(), CompressedSize );
	}

	bool CORE_API CompressReplayData(const TArray<uint8>& InBuffer, TArray< uint8 >& OutCompressed)
	{
		const void * InPtr = InBuffer.GetData();
		const int32 InSize = InBuffer.Num();
		int32 CompressedSize = 0;

		int32 ReservedBytes = sizeof(int32)*2;
				
		const FOodleDataCompression::ECompressor		Compressor = FOodleDataCompression::ECompressor::Selkie;
		const FOodleDataCompression::ECompressionLevel	Level = FOodleDataCompression::ECompressionLevel::VeryFast;
		
		int32 CompSizeNeeded = (int32) FOodleDataCompression::CompressedBufferSizeNeeded(InSize);
		OutCompressed.SetNum( TCheckValueCast<int32>( ReservedBytes + CompSizeNeeded) );
		void * CompPtr = OutCompressed.GetData() + ReservedBytes;

		CompressedSize = (int32) FOodleDataCompression::Compress(CompPtr,CompSizeNeeded,InPtr,InSize,Compressor,Level);
		if ( CompressedSize <= 0 )
		{
			return false;
		}
		
		int32 InSize32 = TCheckValueCast<int32>(InSize);
		int32 CompressedSize32 = TCheckValueCast<int32>(CompressedSize);

		FMemoryWriter Writer(OutCompressed);
		Writer << InSize32;
		Writer << CompressedSize32;
		
		check( Writer.Tell() == ReservedBytes );

		OutCompressed.SetNum( TCheckValueCast<int32>(CompressedSize + ReservedBytes) , false);

		return true;
	}
};
