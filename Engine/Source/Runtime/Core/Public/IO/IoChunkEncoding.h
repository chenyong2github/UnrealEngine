// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Memory/MemoryView.h"
#include "UObject/NameTypes.h"

class FIoBuffer;

/** I/O chunk encryption method. */
enum class EIoEncryptionMethod : uint8
{
	None	= 0,
	AES 	= (1 << 0)
};

/** Defines how the I/O chunk gets encoded into a set of compressed and encrypted block(s). */
struct FIoChunkEncodingParams
{
	FName CompressionFormat = TEXT("Oodle");
	FMemoryView EncryptionKey;
	uint32 BlockSize = (64 << 10);
};

/** Parameters for decoding a set of encoded blocks(s). */
struct FIoChunkDecodingParams
	: public FIoChunkEncodingParams
{
	uint64 TotalRawSize = 0;
	uint64 RawOffset = 0;
	uint64 EncodedOffset = 0;
	TConstArrayView<uint32> EncodedBlockSize;
};

/**
 * Encodes data into a set of encrypted and compressed blocks.
 * The chunk encoding information is enocoded into a 16 byte
 * header followed by N block sizes.
 */
class CORE_API FIoChunkEncoding
{
public:
	static constexpr uint32 ExpectedMagic = 0x2e696f; // .io
	static constexpr uint32 DefaultBlockSize = (64 << 10);
	static constexpr uint32 MaxBlockCount = (1 << 24);
	static constexpr uint64 MaxSize = (uint64(1) << 40);

	/** Header describing the encoded I/O chunk. */
	struct CORE_API FHeader
	{
		uint64 Magic: 24;
		uint64 RawSize: 40;
		uint64 EncodedSize: 40;
		uint64 BlockSizeExponent: 8;
		uint64 Flags: 8;
		uint64 Pad: 8;
		uint32 Blocks[];

		bool IsValid() const;
		uint32 GetBlockSize() const;
		uint32 GetBlockCount() const;
		uint64 GetTotalHeaderSize() const;

		static const FHeader* Decode(FMemoryView HeaderData);
	};

	static_assert(sizeof(FHeader) == 16, "I/O chunk header size mismatch");

	static bool Encode(const FIoChunkEncodingParams& Params, FMemoryView RawData, FIoBuffer& OutEncodedData);
	static bool Encode(const FIoChunkEncodingParams& Params, FMemoryView RawData, FIoBuffer& OutHeader, FIoBuffer& OutEncodedBlocks);
	static bool Decode(const FIoChunkDecodingParams& Params, FMemoryView EncodedBlocks, FMutableMemoryView OutRawData);
	static bool Decode(FMemoryView EncodedData, FName CompressionFormat, FMemoryView EncryptionKey, FMutableMemoryView OutRawData, uint64 Offset = 0);

	static bool GetEncodedRange(uint64 TotalRawSize, uint32 RawBlockSize, TConstArrayView<uint32> EncodedBlockSize, uint64 RawOffset, uint64 RawSize, uint64& OutEncodedStart, uint64& OutEncodedEnd);
	static bool GetEncodedRange(const FIoChunkDecodingParams& Params, uint64 RawSize, uint64& OutEncodedStart, uint64& OutEncodedEnd);
	static uint64 GetTotalEncodedSize(TConstArrayView<uint32> EncodedBlockSize);
};
