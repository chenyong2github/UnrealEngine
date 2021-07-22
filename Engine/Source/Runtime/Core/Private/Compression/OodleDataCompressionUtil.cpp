// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/OodleDataCompressionUtil.h"
#include "Templates/CheckValueCast.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace FOodleDataCompressionUtil
{
	// Our header contains 2 int32s, the compressed and decompressed sizes.
	static constexpr int32 CompressedTArrayHeaderSize = sizeof(int32) * 2;

	bool CORE_API FCompressedArray::CompressData(FOodleDataCompression::ECompressor InCompressor, FOodleDataCompression::ECompressionLevel InLevel, const void* InData, int32 InDataSize)
	{
		// Size the array so that it fits our header and the memory oodle requires to do the work.
		int32 CompSizeNeeded = TCheckValueCast<int32>(FOodleDataCompression::CompressedBufferSizeNeeded(InDataSize));
		this->SetNum( TCheckValueCast<int32>( CompressedTArrayHeaderSize + CompSizeNeeded) );

		// We compress in to the buffer past our header, then write the header below.
		void* CompPtr = this->GetData() + CompressedTArrayHeaderSize;
		int32 CompressedSize = TCheckValueCast<int32>(FOodleDataCompression::Compress(CompPtr, CompSizeNeeded, InData, InDataSize, InCompressor, InLevel));
		if ( CompressedSize <= 0 )
		{
			// Probably a bad parameter.
			this->Empty();
			return false;
		}
		
		int32* Sizes = (int32*)this->GetData();

		Sizes[0] = InDataSize;
		Sizes[1] = CompressedSize;

#if !PLATFORM_LITTLE_ENDIAN
		Sizes[0] = BYTESWAP_ORDER32(Sizes[0]);
		Sizes[1] = BYTESWAP_ORDER32(Sizes[1]);
#endif

		// Trim off the end working space Oodle needed to do the compress work.
		this->SetNum( TCheckValueCast<int32>(CompressedSize + CompressedTArrayHeaderSize) , false);
		return true;
	}

	bool CORE_API FCompressedArray::DecompressToAllocatedBuffer(void*& OutDestinationBuffer, int32& OutDestinationBufferSize) const
	{
		int32 DecompressedSize, CompressedSize;
		if (PeekSizes(CompressedSize, DecompressedSize) == false)
		{
			return false;
		}

		// If we have a valid header, then if we don't have the actual data, it's corrupted data.
		check(CompressedTArrayHeaderSize + CompressedSize <= Num());

		void* DestinationBuffer = FMemory::Malloc(DecompressedSize);
		if (DestinationBuffer == 0)
		{
			return false;
		}

		OutDestinationBufferSize = DecompressedSize;
		OutDestinationBuffer = DestinationBuffer;

		if (FOodleDataCompression::Decompress(DestinationBuffer, DecompressedSize, GetData() + CompressedTArrayHeaderSize, CompressedSize) == false)
		{
			FMemory::Free(DestinationBuffer);
			OutDestinationBuffer = 0;
			return false;
		}

		return true;
	}

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
