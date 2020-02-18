// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"

/**
 * I/O Store TOC header.
 */
struct FIoStoreTocHeader
{
	static constexpr char TocMagicImg[] = "-==--==--==--==-";

	uint8	TocMagic[16];
	uint32	TocHeaderSize;
	uint32	TocEntryCount;
	uint32	TocEntrySize;	// For sanity checking
	uint32	CompressionBlockCount;
	uint32	CompressionBlockSize;
	uint32	CompressionNameCount;
	uint32	TocPad[22];

	void MakeMagic()
	{
		FMemory::Memcpy(TocMagic, TocMagicImg, sizeof TocMagic);
	}

	bool CheckMagic() const
	{
		return FMemory::Memcmp(TocMagic, TocMagicImg, sizeof TocMagic) == 0;
	}
};

/**
 * Combined offset and length.
 */
struct FIoOffsetAndLength
{
public:
	inline uint64 GetOffset() const
	{
		return OffsetAndLength[4]
			| (uint64(OffsetAndLength[3]) << 8)
			| (uint64(OffsetAndLength[2]) << 16)
			| (uint64(OffsetAndLength[1]) << 24)
			| (uint64(OffsetAndLength[0]) << 32)
			;
	}

	inline uint64 GetLength() const
	{
		return OffsetAndLength[9]
			| (uint64(OffsetAndLength[8]) << 8)
			| (uint64(OffsetAndLength[7]) << 16)
			| (uint64(OffsetAndLength[6]) << 24)
			| (uint64(OffsetAndLength[5]) << 32)
			;
	}

	inline void SetOffset(uint64 Offset)
	{
		OffsetAndLength[0] = uint8(Offset >> 32);
		OffsetAndLength[1] = uint8(Offset >> 24);
		OffsetAndLength[2] = uint8(Offset >> 16);
		OffsetAndLength[3] = uint8(Offset >>  8);
		OffsetAndLength[4] = uint8(Offset >>  0);
	}

	inline void SetLength(uint64 Length)
	{
		OffsetAndLength[5] = uint8(Length >> 32);
		OffsetAndLength[6] = uint8(Length >> 24);
		OffsetAndLength[7] = uint8(Length >> 16);
		OffsetAndLength[8] = uint8(Length >> 8);
		OffsetAndLength[9] = uint8(Length >> 0);
	}

private:
	// We use 5 bytes for offset and size, this is enough to represent
	// an offset and size of 1PB
	uint8 OffsetAndLength[5 + 5];
};

/**
 * I/O Store TOC entry.
 */
struct FIoStoreTocEntry
{
	FIoOffsetAndLength	OffsetAndLength;

	// TBD: should the chunk ID use content addressing, or names, or a
	//      mix of both?
	FIoChunkId	ChunkId;

	inline uint64 GetOffset() const
	{
		return OffsetAndLength.GetOffset();
	}

	inline uint64 GetLength() const
	{
		return OffsetAndLength.GetLength();
	}

	inline void SetOffset(uint64 Offset)
	{
		OffsetAndLength.SetOffset(Offset);
	}

	inline void SetLength(uint64 Length)
	{
		OffsetAndLength.SetLength(Length);
	}
};
/**
 * Compression block entry.
 */
struct FIoStoreCompressedBlockEntry
{
	/** Offset and size of the compressed block. */
	FIoOffsetAndLength OffsetAndLength;
	/** Index into the compression methods array. */
	uint8 CompressionMethodIndex;
};

/**
 * I/O store compresssion info.
 */
struct FIoStoreCompressionInfo
{
	enum
	{
		/** Compression method name max length. */
		CompressionMethodNameLen = 32,
		/** No compression. */
		InvalidCompressionIndex = 255,
	};

	uint8 GetCompressionMethodIndex(FName CompressionMethod)
	{
		if (CompressionMethod == NAME_None)
		{
			return InvalidCompressionIndex;
		}

		for (int32 Idx = 0; Idx < CompressionMethods.Num(); ++Idx)
		{
			if (CompressionMethods[Idx] == CompressionMethod)
			{
				return uint8(Idx);
			}
		}

		const uint8 Idx = uint8(CompressionMethods.Num());
		CompressionMethods.Add(CompressionMethod);

		return Idx;
	}

	TArray<FIoStoreCompressedBlockEntry> BlockEntries;
	TArray<FName> CompressionMethods;
	int64 BlockSize = 0;
	int64 UncompressedContainerSize = 0;
	int64 CompressedContainerSize = 0;
};
