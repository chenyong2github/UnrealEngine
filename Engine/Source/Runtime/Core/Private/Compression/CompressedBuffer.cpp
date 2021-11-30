// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/CompressedBuffer.h"

#include "Algo/Accumulate.h"
#include "Algo/ForEach.h"
#include "Compression/OodleDataCompression.h"
#include "Hash/Blake3.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ByteSwap.h"
#include "Misc/Crc.h"
#include "Misc/ScopeExit.h"
#include "Serialization/Archive.h"
#include "Templates/Function.h"

THIRD_PARTY_INCLUDES_START
#include "Compression/lz4.h"
THIRD_PARTY_INCLUDES_END

namespace UE::CompressedBuffer::Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr uint64 DefaultBlockSize = 256 * 1024;
static constexpr uint64 DefaultHeaderSize = 4 * 1024;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Method used to compress the data in a compressed buffer. */
enum class EMethod : uint8
{
	/** Header is followed by one uncompressed block. */
	None = 0,
	/** Header is followed by an array of compressed block sizes then the compressed blocks. */
	Oodle = 3,
	/** Header is followed by an array of compressed block sizes then the compressed blocks. */
	LZ4 = 4,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Header used on every compressed buffer. Always stored in big-endian format. */
struct FHeader
{
	static constexpr uint32 ExpectedMagic = 0xb7756362; // <dot>ucb

	/** A magic number to identify a compressed buffer. Always 0xb7756362. */
	uint32 Magic = ExpectedMagic;
	/** A CRC-32 used to check integrity of the buffer. Uses the polynomial 0x04c11db7. */
	uint32 Crc32 = 0;
	/** The method used to compress the buffer. Affects layout of data following the header. */
	EMethod Method = EMethod::None;
	/** The method-specific compressor used to compress the buffer. */
	uint8 Compressor = 0;
	/** The method-specific compression level used to compress the buffer. */
	uint8 CompressionLevel = 0;
	/** The power of two size of every uncompressed block except the last. Size is 1 << BlockSizeExponent. */
	uint8 BlockSizeExponent = 0;
	/** The number of blocks that follow the header. */
	uint32 BlockCount = 0;
	/** The total size of the uncompressed data. */
	uint64 TotalRawSize = 0;
	/** The total size of the compressed data including the header. */
	uint64 TotalCompressedSize = 0;
	/** The hash of the uncompressed data. */
	FBlake3Hash RawHash;

	/** Checks validity of the buffer based on the magic number, method, and CRC-32. */
	static bool IsValid(const FCompositeBuffer& CompressedData);
	static bool IsValid(const FSharedBuffer& CompressedData) { return IsValid(FCompositeBuffer(CompressedData)); }

	/** Read a header from a buffer that is at least sizeof(FHeader) without any validation. */
	static FHeader Read(const FCompositeBuffer& CompressedData)
	{
		FHeader Header;
		if (sizeof(FHeader) <= CompressedData.GetSize())
		{
			CompressedData.CopyTo(MakeMemoryView(&Header, &Header + 1));
			Header.ByteSwap();
		}
		return Header;
	}

	/**
	 * Write a header to a memory view that is at least sizeof(FHeader).
	 *
	 * @param HeaderView   View of the header to write, including any method-specific header data.
	 */
	void Write(FMutableMemoryView HeaderView) const
	{
		FHeader Header = *this;
		Header.ByteSwap();
		HeaderView.CopyFrom(MakeMemoryView(&Header, &Header + 1));
		Header.ByteSwap();
		Header.Crc32 = CalculateCrc32(HeaderView);
		Header.ByteSwap();
		HeaderView.CopyFrom(MakeMemoryView(&Header, &Header + 1));
	}

	/** Calculate the CRC-32 from a view of a header including any method-specific header data. */
	static uint32 CalculateCrc32(FMemoryView HeaderView)
	{
		uint32 Crc32 = 0;
		constexpr uint64 MethodOffset = STRUCT_OFFSET(FHeader, Method);
		for (FMemoryView View = HeaderView + MethodOffset; const uint64 ViewSize = View.GetSize();)
		{
			const int32 Size = static_cast<int32>(FMath::Min<uint64>(ViewSize, MAX_int32));
			Crc32 = FCrc::MemCrc32(View.GetData(), Size, Crc32);
			View += Size;
		}
		return Crc32;
	}

	void ByteSwap()
	{
		Magic = NETWORK_ORDER32(Magic);
		Crc32 = NETWORK_ORDER32(Crc32);
		BlockCount = NETWORK_ORDER32(BlockCount);
		TotalRawSize = NETWORK_ORDER64(TotalRawSize);
		TotalCompressedSize = NETWORK_ORDER64(TotalCompressedSize);
	}

	bool TryGetCompressParameters(
		ECompressedBufferCompressor& OutCompressor,
		ECompressedBufferCompressionLevel& OutCompressionLevel,
		uint64& OutBlockSize) const
	{
		switch (Method)
		{
		case EMethod::None:
			OutCompressor = ECompressedBufferCompressor::NotSet;
			OutCompressionLevel = ECompressedBufferCompressionLevel::None;
			OutBlockSize = 0;
			return true;
		case EMethod::Oodle:
			OutCompressor = ECompressedBufferCompressor(Compressor);
			OutCompressionLevel = ECompressedBufferCompressionLevel(CompressionLevel);
			OutBlockSize = uint64(1) << BlockSizeExponent;
			return true;
		default:
			return false;
		}
	}
};

static_assert(sizeof(FHeader) == 64, "FHeader is the wrong size.");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FDecoderSource
{
public:
	virtual bool Read(uint64 Offset, FMutableMemoryView Data) const = 0;
	virtual FMemoryView ReadOrView(uint64 Offset, uint64 Size, FDecoderContext& Context) const = 0;
	virtual FCompositeBuffer ReadToComposite(uint64 Offset, uint64 Size) const = 0;
};

class FEncoder
{
public:
	virtual FCompositeBuffer Compress(const FCompositeBuffer& RawData, uint64 BlockSize) const = 0;
};

class FDecoder
{
public:
	virtual uint64 GetHeaderSize(const FHeader& Header) const = 0;
	virtual void DecodeHeader(FMutableMemoryView HeaderView) const = 0;
	virtual bool TryDecompressTo(FDecoderContext& Context, const FDecoderSource& Source, const FHeader& Header, uint64 RawOffset, FMutableMemoryView RawView) const = 0;
	virtual FCompositeBuffer DecompressToComposite(FDecoderContext& Context, const FDecoderSource& Source, const FHeader& Header, uint64 RawOffset, uint64 RawSize) const;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FNoneEncoder final : public FEncoder
{
public:
	FCompositeBuffer Compress(const FCompositeBuffer& RawData, uint64 BlockSize) const final
	{
		FHeader Header;
		Header.Method = EMethod::None;
		Header.BlockCount = 1;
		Header.TotalRawSize = RawData.GetSize();
		Header.TotalCompressedSize = Header.TotalRawSize + sizeof(FHeader);
		Header.RawHash = FBlake3::HashBuffer(RawData);

		FUniqueBuffer HeaderData = FUniqueBuffer::Alloc(sizeof(FHeader));
		Header.Write(HeaderData);
		return FCompositeBuffer(HeaderData.MoveToShared(), RawData.MakeOwned());
	}
};

class FNoneDecoder final : public FDecoder
{
public:
	uint64 GetHeaderSize(const FHeader& Header) const final
	{
		return sizeof(FHeader);
	}

	void DecodeHeader(FMutableMemoryView HeaderView) const final
	{
		FHeader& Header = *static_cast<FHeader*>(HeaderView.GetData());
		Header.ByteSwap();
	}

	bool TryDecompressTo(
		FDecoderContext& Context,
		const FDecoderSource& Source,
		const FHeader& Header,
		uint64 RawOffset,
		FMutableMemoryView RawView) const final
	{
		if (Header.Method == EMethod::None && RawOffset + RawView.GetSize() <= Header.TotalRawSize)
		{
			return Source.Read(sizeof(FHeader) + RawOffset, RawView);
		}
		return false;
	}

	FCompositeBuffer DecompressToComposite(
		FDecoderContext& Context,
		const FDecoderSource& Source,
		const FHeader& Header,
		uint64 RawOffset,
		uint64 RawSize) const final
	{
		if (Header.Method == EMethod::None &&
			Header.TotalCompressedSize == Header.TotalRawSize + sizeof(FHeader))
		{
			return Source.ReadToComposite(sizeof(FHeader) + RawOffset, RawSize);
		}
		return FCompositeBuffer();
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBlockEncoder : public FEncoder
{
public:
	FCompositeBuffer Compress(const FCompositeBuffer& RawData, uint64 BlockSize) const final;

protected:
	virtual EMethod GetMethod() const = 0;
	virtual uint8 GetCompressor() const = 0;
	virtual uint8 GetCompressionLevel() const = 0;
	virtual uint64 CompressBlockBound(uint64 RawSize) const = 0;
	virtual bool CompressBlock(FMutableMemoryView& CompressedData, FMemoryView RawData) const = 0;

private:
	uint64 GetCompressedBlocksBound(uint64 BlockCount, uint64 BlockSize, uint64 RawSize) const
	{
		switch (BlockCount)
		{
		case 0:  return 0;
		case 1:  return CompressBlockBound(RawSize);
		default: return CompressBlockBound(BlockSize) - BlockSize + RawSize;
		}
	}
};

FCompositeBuffer FBlockEncoder::Compress(const FCompositeBuffer& RawData, const uint64 BlockSize) const
{
	checkf(FMath::IsPowerOfTwo(BlockSize) && BlockSize <= MAX_uint32,
		TEXT("BlockSize must be a 32-bit power of two but was %" UINT64_FMT "."), BlockSize);
	const uint64 RawSize = RawData.GetSize();
	FBlake3 RawHash;

	const uint64 BlockCount = FMath::DivideAndRoundUp(RawSize, BlockSize);
	checkf(BlockCount <= MAX_uint32, TEXT("Raw data of size %" UINT64_FMT " with block size %" UINT64_FMT " requires ")
		TEXT("%" UINT64_FMT " blocks, but the limit is %u."), RawSize, BlockSize, BlockCount, MAX_uint32);

	// Allocate the buffer for the header, metadata, and compressed blocks.
	const uint64 MetaSize = BlockCount * sizeof(uint32);
	const uint64 CompressedDataSize = sizeof(FHeader) + MetaSize + GetCompressedBlocksBound(BlockCount, BlockSize, RawSize);
	FUniqueBuffer CompressedData = FUniqueBuffer::Alloc(CompressedDataSize);

	// Compress the raw data in blocks and store the raw data for incompressible blocks.
	TArray64<uint32> CompressedBlockSizes;
	CompressedBlockSizes.Reserve(static_cast<uint32>(BlockCount));
	uint64 CompressedSize = 0;
	{
		FUniqueBuffer RawBlockCopy;
		FMutableMemoryView CompressedBlocksView = CompressedData.GetView() + sizeof(FHeader) + MetaSize;
		for (uint64 RawOffset = 0; RawOffset < RawSize;)
		{
			const uint64 RawBlockSize = FMath::Min(RawSize - RawOffset, BlockSize);
			const FMemoryView RawBlock = RawData.ViewOrCopyRange(RawOffset, RawBlockSize, RawBlockCopy);
			RawHash.Update(RawBlock);

			FMutableMemoryView CompressedBlock = CompressedBlocksView;
			if (!CompressBlock(CompressedBlock, RawBlock))
			{
				return FCompositeBuffer();
			}

			uint64 CompressedBlockSize = CompressedBlock.GetSize();
			if (RawBlockSize <= CompressedBlockSize)
			{
				CompressedBlockSize = RawBlockSize;
				CompressedBlocksView = CompressedBlocksView.CopyFrom(RawBlock);
			}
			else
			{
				CompressedBlocksView += CompressedBlockSize;
			}

			CompressedBlockSizes.Add(static_cast<uint32>(CompressedBlockSize));
			CompressedSize += CompressedBlockSize;
			RawOffset += RawBlockSize;
		}
	}

	// Return an uncompressed buffer if the compressed data is larger than the raw data.
	if (RawSize <= MetaSize + CompressedSize)
	{
		CompressedData.Reset();
		return FNoneEncoder().Compress(RawData, BlockSize);
	}

	// Write the header and calculate the CRC-32.
	Algo::ForEach(CompressedBlockSizes, [](uint32& Size) { Size = NETWORK_ORDER32(Size); });
	CompressedData.GetView().Mid(sizeof(FHeader), MetaSize).CopyFrom(MakeMemoryView(CompressedBlockSizes));

	FHeader Header;
	Header.Method = GetMethod();
	Header.Compressor = GetCompressor();
	Header.CompressionLevel = GetCompressionLevel();
	Header.BlockSizeExponent = static_cast<uint8>(FMath::FloorLog2_64(BlockSize));
	Header.BlockCount = static_cast<uint32>(BlockCount);
	Header.TotalRawSize = RawSize;
	Header.TotalCompressedSize = sizeof(FHeader) + MetaSize + CompressedSize;
	Header.RawHash = RawHash.Finalize();
	Header.Write(CompressedData.GetView().Left(sizeof(FHeader) + MetaSize));

	const FMemoryView CompositeView = CompressedData.GetView().Left(Header.TotalCompressedSize);
	return FCompositeBuffer(FSharedBuffer::MakeView(CompositeView, CompressedData.MoveToShared()));
}

class FBlockDecoder : public FDecoder
{
public:
	uint64 GetHeaderSize(const FHeader& Header) const final
	{
		return sizeof(FHeader) + sizeof(uint32) * uint64(Header.BlockCount);
	}

	void DecodeHeader(FMutableMemoryView HeaderView) const final
	{
		FHeader& Header = *static_cast<FHeader*>(HeaderView.GetData());
		Header.ByteSwap();
		TArrayView<uint32> CompressedBlockSizes(reinterpret_cast<uint32*>(&Header + 1), Header.BlockCount);
		Algo::ForEach(CompressedBlockSizes, [](uint32& Size) { Size = NETWORK_ORDER32(Size); });
	}

	bool TryDecompressTo(FDecoderContext& Context, const FDecoderSource& Source, const FHeader& Header, uint64 RawOffset, FMutableMemoryView RawView) const final;
	FCompositeBuffer DecompressToComposite(FDecoderContext& Context, const FDecoderSource& Source, const FHeader& Header, uint64 RawOffset, uint64 RawSize) const final;

protected:
	virtual bool DecompressBlock(FMutableMemoryView RawData, FMemoryView CompressedData) const = 0;
};

bool FBlockDecoder::TryDecompressTo(
	FDecoderContext& Context,
	const FDecoderSource& Source,
	const FHeader& Header,
	const uint64 RawOffset,
	FMutableMemoryView RawView) const
{
	if (Header.TotalRawSize < RawOffset + RawView.GetSize())
	{
		return false;
	}

	const uint64 BlockSize = uint64(1) << Header.BlockSizeExponent;
	const uint64 FirstBlockIndex = uint64(RawOffset / BlockSize);
	const uint64 LastBlockIndex = uint64((RawOffset + RawView.GetSize() - 1) / BlockSize);
	const uint64 LastBlockSize = BlockSize - ((Header.BlockCount * BlockSize) - Header.TotalRawSize);

	uint64 RawBlockOffset = RawOffset % BlockSize;

	const TConstArrayView64<uint32> CompressedBlockSizes(reinterpret_cast<const uint32*>(&Header + 1), Header.BlockCount);
	uint64 CompressedOffset = sizeof(FHeader) + uint64(Header.BlockCount) * sizeof(uint32);
	CompressedOffset += Algo::Accumulate(CompressedBlockSizes.Left(FirstBlockIndex), uint64(0));

	for (uint64 BlockIndex = FirstBlockIndex; BlockIndex <= LastBlockIndex; BlockIndex++)
	{
		const uint64 RawBlockSize = BlockIndex == Header.BlockCount - 1 ? LastBlockSize : BlockSize;
		const uint64 RawBlockReadSize = FMath::Min(RawView.GetSize(), RawBlockSize - RawBlockOffset);
		const uint32 CompressedBlockSize = CompressedBlockSizes[BlockIndex];
		const bool bIsCompressed = CompressedBlockSize < RawBlockSize;

		if (bIsCompressed)
		{
			if (Context.RawBlockIndex == BlockIndex)
			{
				RawView.Left(RawBlockReadSize).CopyFrom(Context.RawBlock.GetView().Mid(RawBlockOffset, RawBlockReadSize));
			}
			else
			{
				FMutableMemoryView RawBlock;
				if (RawBlockReadSize == RawBlockSize)
				{
					RawBlock = RawView.Left(RawBlockSize);
				}
				else
				{
					if (Context.RawBlock.GetSize() < RawBlockSize)
					{
						Context.RawBlock = FUniqueBuffer::Alloc(BlockSize);
					}
					RawBlock = Context.RawBlock.GetView().Left(RawBlockSize);
					Context.RawBlockIndex = BlockIndex;
				}

				const FMemoryView CompressedBlock = Source.ReadOrView(CompressedOffset, CompressedBlockSize, Context);
				if (CompressedBlock.IsEmpty() || !DecompressBlock(RawBlock, CompressedBlock))
				{
					return false;
				}

				if (RawBlockReadSize != RawBlockSize)
				{
					RawView.CopyFrom(RawBlock.Mid(RawBlockOffset, RawBlockReadSize));
				}
			}
		}
		else
		{
			Source.Read(CompressedOffset + RawBlockOffset, RawView.Left(RawBlockReadSize));
		}

		RawBlockOffset = 0;
		CompressedOffset += CompressedBlockSize;
		RawView += RawBlockReadSize;
	}

	return RawView.GetSize() == 0;
}

FCompositeBuffer FBlockDecoder::DecompressToComposite(
	FDecoderContext& Context,
	const FDecoderSource& Source,
	const FHeader& Header,
	const uint64 RawOffset,
	const uint64 RawSize) const
{
	FUniqueBuffer Buffer = FUniqueBuffer::Alloc(RawSize);
	if (TryDecompressTo(Context, Source, Header, RawOffset, Buffer))
	{
		return FCompositeBuffer(Buffer.MoveToShared());
	}
	return FCompositeBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FOodleEncoder final : public FBlockEncoder
{
public:
	FOodleEncoder(ECompressedBufferCompressor InCompressor, ECompressedBufferCompressionLevel InCompressionLevel)
		: Compressor(InCompressor)
		, CompressionLevel(InCompressionLevel)
	{
	}

protected:
	EMethod GetMethod() const final { return EMethod::Oodle; }
	uint8 GetCompressor() const final { return static_cast<uint8>(Compressor); }
	uint8 GetCompressionLevel() const final { return static_cast<uint8>(CompressionLevel); }

	uint64 CompressBlockBound(uint64 RawSize) const final
	{
		return static_cast<uint64>(FOodleDataCompression::CompressedBufferSizeNeeded(static_cast<int64>(RawSize)));
	}

	bool CompressBlock(FMutableMemoryView& CompressedData, FMemoryView RawData) const final
	{
		const int64 Size = FOodleDataCompression::Compress(
			CompressedData.GetData(), static_cast<uint64>(CompressedData.GetSize()),
			RawData.GetData(), static_cast<int64>(RawData.GetSize()),
			Compressor, CompressionLevel);
		CompressedData.LeftInline(static_cast<uint64>(Size));
		return Size > 0;
	}

private:
	const ECompressedBufferCompressor Compressor;
	const ECompressedBufferCompressionLevel CompressionLevel;
};

class FOodleDecoder final : public FBlockDecoder
{
protected:
	bool DecompressBlock(FMutableMemoryView RawData, FMemoryView CompressedData) const final
	{
		return FOodleDataCompression::Decompress(
			RawData.GetData(), static_cast<int64>(RawData.GetSize()),
			CompressedData.GetData(), static_cast<int64>(CompressedData.GetSize()));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FLZ4Encoder final : public FBlockEncoder
{
protected:
	EMethod GetMethod() const final { return EMethod::LZ4; }
	uint8 GetCompressor() const final { return 0; }
	uint8 GetCompressionLevel() const final { return 0; }

	uint64 CompressBlockBound(uint64 RawSize) const final
	{
		if (RawSize <= LZ4_MAX_INPUT_SIZE)
		{
			return static_cast<uint64>(LZ4_compressBound(static_cast<int>(RawSize)));
		}
		return 0;
	}

	bool CompressBlock(FMutableMemoryView& CompressedData, FMemoryView RawData) const final
	{
		if (RawData.GetSize() <= LZ4_MAX_INPUT_SIZE)
		{
			const int Size = LZ4_compress_default(
				static_cast<const char*>(RawData.GetData()), static_cast<char*>(CompressedData.GetData()),
				static_cast<int>(RawData.GetSize()), static_cast<int>(FMath::Min<uint64>(CompressedData.GetSize(), MAX_int32)));
			CompressedData.LeftInline(static_cast<uint64>(Size));
			return Size > 0;
		}
		return false;
	}
};

class FLZ4Decoder final : public FBlockDecoder
{
protected:
	bool DecompressBlock(FMutableMemoryView RawData, FMemoryView CompressedData) const final
	{
		if (CompressedData.GetSize() <= MAX_int32)
		{
			const int Size = LZ4_decompress_safe(
				static_cast<const char*>(CompressedData.GetData()),
				static_cast<char*>(RawData.GetData()),
				static_cast<int>(CompressedData.GetSize()),
				static_cast<int>(FMath::Min<uint64>(RawData.GetSize(), LZ4_MAX_INPUT_SIZE)));
			return static_cast<uint64>(Size) == RawData.GetSize();
		}
		return false;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const FDecoder* GetDecoder(EMethod Method)
{
	static FNoneDecoder None;
	static FOodleDecoder Oodle;
	static FLZ4Decoder LZ4;
	switch (Method)
	{
	default:
		return nullptr;
	case EMethod::None:
		return &None;
	case EMethod::Oodle:
		return &Oodle;
	case EMethod::LZ4:
		return &LZ4;
	}
}

template <typename BufferType>
inline FCompositeBuffer ValidBufferOrEmpty(BufferType&& CompressedData)
{
	return FHeader::IsValid(CompressedData) ? FCompositeBuffer(Forward<BufferType>(CompressedData)) : FCompositeBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FHeader::IsValid(const FCompositeBuffer& CompressedData)
{
	if (sizeof(FHeader) <= CompressedData.GetSize())
	{
		const FHeader Header = Read(CompressedData);
		if (Header.Magic == FHeader::ExpectedMagic)
		{
			if (const FDecoder* const Decoder = GetDecoder(Header.Method))
			{
				FUniqueBuffer HeaderCopy;
				const FMemoryView HeaderView = CompressedData.ViewOrCopyRange(0, Decoder->GetHeaderSize(Header), HeaderCopy);
				if (Header.Crc32 == FHeader::CalculateCrc32(HeaderView))
				{
					return true;
				}
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const FHeader* TryReadHeader(FDecoderContext& Context, FArchive& Ar)
{
	if (Context.HeaderOffset != MAX_uint64)
	{
		return static_cast<const FHeader*>(Context.Header.GetData());
	}

	check(Ar.IsLoading());
	const int64 Offset = Ar.Tell();
	if (Offset == INDEX_NONE)
	{
		return nullptr;
	}

	FHeader Header;
	Ar.Serialize(&Header, sizeof(FHeader));
	Header.ByteSwap();

	if (const FDecoder* const Decoder = GetDecoder(Header.Method); Decoder && Header.Magic == FHeader::ExpectedMagic)
	{
		const uint64 HeaderSize = Decoder->GetHeaderSize(Header);
		if (Context.Header.GetSize() < HeaderSize)
		{
			Context.Header = FUniqueBuffer::Alloc(FMath::Max(FMath::RoundUpToPowerOfTwo64(HeaderSize), DefaultHeaderSize));
		}

		const FMutableMemoryView HeaderView = Context.Header.GetView().Left(HeaderSize);
		const FMutableMemoryView HeaderTail = HeaderView.CopyFrom(MakeMemoryView(&Header, &Header + 1));
		Ar.Serialize(HeaderTail.GetData(), static_cast<int64>(HeaderTail.GetSize()));

		FHeader* const HeaderCopy = static_cast<FHeader*>(HeaderView.GetData());
		HeaderCopy->ByteSwap();
		if (Header.Crc32 == FHeader::CalculateCrc32(HeaderView))
		{
			Context.HeaderOffset = uint64(Offset);
			Decoder->DecodeHeader(HeaderView);
			return HeaderCopy;
		}
	}

	return nullptr;
}

static const FHeader* TryReadHeader(FDecoderContext& Context, const FCompositeBuffer& Buffer)
{
	if (Context.HeaderOffset != MAX_uint64)
	{
		return static_cast<const FHeader*>(Context.Header.GetData());
	}

	if (Buffer.GetSize() < sizeof(FHeader))
	{
		return nullptr;
	}

	FHeader Header;
	Buffer.CopyTo(MakeMemoryView(&Header, &Header + 1));
	Header.ByteSwap();

	if (const FDecoder* const Decoder = GetDecoder(Header.Method); Decoder && Header.Magic == FHeader::ExpectedMagic)
	{
		const uint64 HeaderSize = Decoder->GetHeaderSize(Header);
		if (Context.Header.GetSize() < HeaderSize)
		{
			Context.Header = FUniqueBuffer::Alloc(FMath::Max(FMath::RoundUpToPowerOfTwo64(HeaderSize), DefaultHeaderSize));
		}

		const FMutableMemoryView HeaderView = Context.Header.GetView().Left(HeaderSize);
		Buffer.CopyTo(HeaderView);

		if (Header.Crc32 == FHeader::CalculateCrc32(HeaderView))
		{
			Context.HeaderOffset = 0;
			Decoder->DecodeHeader(HeaderView);
			return static_cast<FHeader*>(HeaderView.GetData());
		}
	}

	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FArchiveDecoderSource final : public FDecoderSource
{
public:
	explicit FArchiveDecoderSource(FArchive& InArchive, const uint64 InBaseOffset)
		: Archive(InArchive)
		, BaseOffset(InBaseOffset)
	{
	}

	bool Read(uint64 Offset, FMutableMemoryView Data) const final
	{
		Archive.Seek(int64(BaseOffset + Offset));
		Archive.Serialize(Data.GetData(), int64(Data.GetSize()));
		return !Archive.IsError();
	}

	FMemoryView ReadOrView(uint64 Offset, uint64 Size, FDecoderContext& Context) const final
	{
		if (Context.CompressedBlock.GetSize() < Size)
		{
			Context.CompressedBlock = FUniqueBuffer::Alloc(FMath::Max(FMath::RoundUpToPowerOfTwo64(Size), DefaultBlockSize));
		}
		const FMutableMemoryView View = Context.CompressedBlock.GetView().Left(Size);
		return Read(Offset, View) ? View : FMemoryView();
	}

	FCompositeBuffer ReadToComposite(uint64 Offset, uint64 Size) const final
	{
		FUniqueBuffer Buffer = FUniqueBuffer::Alloc(Size);
		if (Read(Offset, Buffer))
		{
			return FCompositeBuffer(Buffer.MoveToShared());
		}
		return FCompositeBuffer();
	}

private:
	FArchive& Archive;
	const uint64 BaseOffset;
};

class FBufferDecoderSource final : public FDecoderSource
{
public:
	explicit FBufferDecoderSource(const FCompositeBuffer& InBuffer)
		: Buffer(InBuffer)
	{
	}

	bool Read(uint64 Offset, FMutableMemoryView Data) const final
	{
		if (Offset + Data.GetSize() <= Buffer.GetSize())
		{
			Buffer.CopyTo(Data, Offset);
			return true;
		}
		return false;
	}

	FMemoryView ReadOrView(uint64 Offset, uint64 Size, FDecoderContext& Context) const final
	{
		return Buffer.ViewOrCopyRange(Offset, Size, Context.CompressedBlock, [](uint64 BufferSize) -> FUniqueBuffer
		{
			return FUniqueBuffer::Alloc(FMath::Max(FMath::RoundUpToPowerOfTwo64(BufferSize), DefaultBlockSize));
		});
	}

	FCompositeBuffer ReadToComposite(uint64 Offset, uint64 Size) const final
	{
		return Buffer.Mid(Offset, Size).MakeOwned();
	}

private:
	const FCompositeBuffer& Buffer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::CompressedBuffer::Private

FCompressedBuffer FCompressedBuffer::Compress(const FCompositeBuffer& RawData)
{
	return Compress(RawData, ECompressedBufferCompressor::Mermaid, ECompressedBufferCompressionLevel::VeryFast);
}

FCompressedBuffer FCompressedBuffer::Compress(const FSharedBuffer& RawData)
{
	return Compress(FCompositeBuffer(RawData));
}

FCompressedBuffer FCompressedBuffer::Compress(
	const FCompositeBuffer& RawData,
	ECompressedBufferCompressor Compressor,
	ECompressedBufferCompressionLevel CompressionLevel,
	uint64 BlockSize)
{
	using namespace UE::CompressedBuffer::Private;

	if (BlockSize == 0)
	{
		BlockSize = DefaultBlockSize;
	}

	FCompressedBuffer Local;
	if (CompressionLevel == ECompressedBufferCompressionLevel::None)
	{
		Local.CompressedData = FNoneEncoder().Compress(RawData, BlockSize);
	}
	else
	{
		Local.CompressedData = FOodleEncoder(Compressor, CompressionLevel).Compress(RawData, BlockSize);
	}
	return Local;
}

FCompressedBuffer FCompressedBuffer::Compress(
	const FSharedBuffer& RawData,
	ECompressedBufferCompressor Compressor,
	ECompressedBufferCompressionLevel CompressionLevel,
	uint64 BlockSize)
{
	return Compress(FCompositeBuffer(RawData), Compressor, CompressionLevel, BlockSize);
}

FCompressedBuffer FCompressedBuffer::FromCompressed(const FCompositeBuffer& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::Private::ValidBufferOrEmpty(InCompressedData);
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(FCompositeBuffer&& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::Private::ValidBufferOrEmpty(MoveTemp(InCompressedData));
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(const FSharedBuffer& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::Private::ValidBufferOrEmpty(InCompressedData);
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(FSharedBuffer&& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::Private::ValidBufferOrEmpty(MoveTemp(InCompressedData));
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(FArchive& Ar)
{
	using namespace UE::CompressedBuffer::Private;
	check(Ar.IsLoading());

	FHeader Header;
	Ar.Serialize(&Header, sizeof(FHeader));
	Header.ByteSwap();

	FCompressedBuffer Local;
	if (Header.Magic == Header.ExpectedMagic)
	{
		FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(Header.TotalCompressedSize);
		Header.ByteSwap();
		const FMutableMemoryView MutableView = MutableBuffer.GetView().CopyFrom(MakeMemoryView(&Header, &Header + 1));
		Ar.Serialize(MutableView.GetData(), static_cast<int64>(MutableView.GetSize()));
		Local.CompressedData = ValidBufferOrEmpty(MutableBuffer.MoveToShared());
	}
	if (Local.IsNull())
	{
		Ar.SetError();
	}
	return Local;
}

uint64 FCompressedBuffer::GetCompressedSize() const
{
	return CompressedData.GetSize();
}

uint64 FCompressedBuffer::GetRawSize() const
{
	return CompressedData ? UE::CompressedBuffer::Private::FHeader::Read(CompressedData).TotalRawSize : 0;
}

FBlake3Hash FCompressedBuffer::GetRawHash() const
{
	return CompressedData ? UE::CompressedBuffer::Private::FHeader::Read(CompressedData).RawHash : FBlake3Hash();
}

bool FCompressedBuffer::TryGetCompressParameters(
	ECompressedBufferCompressor& OutCompressor,
	ECompressedBufferCompressionLevel& OutCompressionLevel,
	uint64& OutBlockSize) const
{
	using namespace UE::CompressedBuffer::Private;
	if (CompressedData)
	{
		return FHeader::Read(CompressedData).TryGetCompressParameters(OutCompressor, OutCompressionLevel, OutBlockSize);
	}
	return false;
}

bool FCompressedBuffer::TryDecompressTo(FMutableMemoryView RawView, uint64 RawOffset) const
{
	if (CompressedData)
	{
		return FCompressedBufferReader(*this).TryDecompressTo(RawView, RawOffset);
	}
	return false;
}

FSharedBuffer FCompressedBuffer::Decompress(uint64 RawOffset, uint64 RawSize) const
{
	if (CompressedData)
	{
		return FCompressedBufferReader(*this).Decompress(RawOffset, RawSize);
	}
	return FSharedBuffer();
}

FCompositeBuffer FCompressedBuffer::DecompressToComposite() const
{
	using namespace UE::CompressedBuffer::Private;
	if (CompressedData)
	{
		return FCompressedBufferReader(*this).DecompressToComposite();
	}
	return FCompositeBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FArchive& operator<<(FArchive& Ar, FCompressedBuffer& Buffer)
{
	if (Ar.IsLoading())
	{
		Buffer = FCompressedBuffer::FromCompressed(Ar);
	}
	else
	{
		for (const FSharedBuffer& Segment : Buffer.GetCompressed().GetSegments())
		{
			Ar.Serialize(const_cast<void*>(Segment.GetData()), static_cast<int64>(Segment.GetSize()));
		}
	}
	return Ar;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCompressedBufferReader::FCompressedBufferReader(FArchive& Archive)
{
	SetSource(Archive);
}

FCompressedBufferReader::FCompressedBufferReader(const FCompressedBuffer& Buffer)
{
	SetSource(Buffer);
}

void FCompressedBufferReader::ResetBuffers()
{
	using namespace UE::CompressedBuffer::Private;
	if (SourceArchive && Context.HeaderOffset != MAX_uint64)
	{
		SourceArchive->Seek(int64(Context.HeaderOffset));
	}
	Context = FDecoderContext();
}

void FCompressedBufferReader::ResetSource()
{
	Context.HeaderOffset = MAX_uint64;
	Context.RawBlockIndex = MAX_uint64;
	SourceArchive = nullptr;
	SourceBuffer = nullptr;
}

void FCompressedBufferReader::SetSource(FArchive& Archive)
{
	if (SourceArchive == &Archive)
	{
		return;
	}
	Context.HeaderOffset = MAX_uint64;
	Context.RawBlockIndex = MAX_uint64;
	SourceArchive = &Archive;
	SourceBuffer = nullptr;
}

void FCompressedBufferReader::SetSource(const FCompressedBuffer& Buffer)
{
	if (SourceBuffer == &Buffer)
	{
		return;
	}
	Context.HeaderOffset = MAX_uint64;
	Context.RawBlockIndex = MAX_uint64;
	SourceArchive = nullptr;
	SourceBuffer = &Buffer;
}

uint64 FCompressedBufferReader::GetCompressedSize()
{
	using namespace UE::CompressedBuffer::Private;
	if (const FCompressedBuffer* const Buffer = SourceBuffer)
	{
		return Buffer->GetCompressedSize();
	}
	if (const FHeader* const Header = TryReadHeader())
	{
		return Header->TotalCompressedSize;
	}
	return 0;
}

uint64 FCompressedBufferReader::GetRawSize()
{
	using namespace UE::CompressedBuffer::Private;
	if (const FCompressedBuffer* const Buffer = SourceBuffer)
	{
		return Buffer->GetRawSize();
	}
	if (const FHeader* const Header = TryReadHeader())
	{
		return Header->TotalRawSize;
	}
	return 0;
}

FBlake3Hash FCompressedBufferReader::GetRawHash()
{
	using namespace UE::CompressedBuffer::Private;
	if (const FCompressedBuffer* const Buffer = SourceBuffer)
	{
		return Buffer->GetRawHash();
	}
	if (const FHeader* const Header = TryReadHeader())
	{
		return Header->RawHash;
	}
	return FBlake3Hash();
}

bool FCompressedBufferReader::TryGetCompressParameters(
	ECompressedBufferCompressor& OutCompressor,
	ECompressedBufferCompressionLevel& OutCompressionLevel,
	uint64& OutBlockSize)
{
	using namespace UE::CompressedBuffer::Private;
	if (const FCompressedBuffer* const Buffer = SourceBuffer)
	{
		return Buffer->TryGetCompressParameters(OutCompressor, OutCompressionLevel, OutBlockSize);
	}
	if (const FHeader* const Header = TryReadHeader())
	{
		return Header->TryGetCompressParameters(OutCompressor, OutCompressionLevel, OutBlockSize);
	}
	return false;
}

bool FCompressedBufferReader::TryDecompressTo(FMutableMemoryView RawView, uint64 RawOffset)
{
	using namespace UE::CompressedBuffer::Private;
	if (const FHeader* const Header = TryReadHeader())
	{
		const uint64 TotalRawSize = Header->TotalRawSize;
		if (RawOffset <= TotalRawSize && RawView.GetSize() <= TotalRawSize - RawOffset)
		{
			if (const FDecoder* const Decoder = GetDecoder(Header->Method))
			{
				if (Decoder->TryDecompressTo(Context,
					SourceArchive
						? ImplicitConv<const FDecoderSource&>(FArchiveDecoderSource(*SourceArchive, Context.HeaderOffset))
						: ImplicitConv<const FDecoderSource&>(FBufferDecoderSource(SourceBuffer->GetCompressed())),
					*Header, RawOffset, RawView))
				{
					return true;
				}
			}
		}
	}
	return false;
}

FSharedBuffer FCompressedBufferReader::Decompress(const uint64 RawOffset, const uint64 RawSize)
{
	using namespace UE::CompressedBuffer::Private;
	if (const FHeader* const Header = TryReadHeader())
	{
		const uint64 TotalRawSize = Header->TotalRawSize;
		const uint64 RawSizeToCopy = RawSize == MAX_uint64 ? TotalRawSize - RawOffset : RawSize;
		if (RawOffset <= TotalRawSize && RawSizeToCopy <= TotalRawSize - RawOffset)
		{
			if (const FDecoder* const Decoder = GetDecoder(Header->Method))
			{
				FUniqueBuffer RawData = FUniqueBuffer::Alloc(RawSizeToCopy);
				if (Decoder->TryDecompressTo(Context,
					SourceArchive
						? ImplicitConv<const FDecoderSource&>(FArchiveDecoderSource(*SourceArchive, Context.HeaderOffset))
						: ImplicitConv<const FDecoderSource&>(FBufferDecoderSource(SourceBuffer->GetCompressed())),
					*Header, RawOffset, RawData))
				{
					return RawData.MoveToShared();
				}
			}
		}
	}
	return FSharedBuffer();
}

FCompositeBuffer FCompressedBufferReader::DecompressToComposite(const uint64 RawOffset, const uint64 RawSize)
{
	using namespace UE::CompressedBuffer::Private;
	if (const FHeader* const Header = TryReadHeader())
	{
		const uint64 TotalRawSize = Header->TotalRawSize;
		const uint64 RawSizeToCopy = RawSize == MAX_uint64 ? TotalRawSize - RawOffset : RawSize;
		if (RawOffset <= TotalRawSize && RawSizeToCopy <= TotalRawSize - RawOffset)
		{
			if (const FDecoder* const Decoder = GetDecoder(Header->Method))
			{
				return Decoder->DecompressToComposite(Context,
					SourceArchive
						? ImplicitConv<const FDecoderSource&>(FArchiveDecoderSource(*SourceArchive, Context.HeaderOffset))
						: ImplicitConv<const FDecoderSource&>(FBufferDecoderSource(SourceBuffer->GetCompressed())),
					*Header, RawOffset, RawSizeToCopy);
			}
		}
	}
	return FCompositeBuffer();
}

const UE::CompressedBuffer::Private::FHeader* FCompressedBufferReader::TryReadHeader()
{
	using namespace UE::CompressedBuffer;
	if (FArchive* const Archive = SourceArchive)
	{
		return Private::TryReadHeader(Context, *Archive);
	}
	if (const FCompressedBuffer* const Buffer = SourceBuffer)
	{
		return Private::TryReadHeader(Context, Buffer->GetCompressed());
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCompressedBufferDecoder::FImpl
{
public:
	bool TryDecompressTo(const FCompressedBuffer& CompressedBuffer, FMutableMemoryView RawView, uint64 RawOffset)
	{
		using namespace UE::CompressedBuffer::Private;
		Reader.SetSource(CompressedBuffer);
		ON_SCOPE_EXIT { Reader.ResetSource(); };
		return Reader.TryDecompressTo(RawView, RawOffset);
	}

private:
	FCompressedBufferReader Reader;
};

FCompressedBufferDecoder::FCompressedBufferDecoder()
: Impl(new FImpl())
{
}

FCompressedBufferDecoder::~FCompressedBufferDecoder()
{
}

bool FCompressedBufferDecoder::TryDecompressTo(const FCompressedBuffer& CompressedBuffer, FMutableMemoryView RawView, uint64 RawOffset)
{
	return Impl->TryDecompressTo(CompressedBuffer, RawView, RawOffset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
