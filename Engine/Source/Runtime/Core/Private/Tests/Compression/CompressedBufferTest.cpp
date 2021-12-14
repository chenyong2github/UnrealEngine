// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/CompressedBuffer.h"

#include "Compression/OodleDataCompression.h"
#include "Hash/Blake3.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedBufferTest, "System.Core.Compression.CompressedBuffer", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCompressedBufferTest::RunTest(const FString& Parameters)
{
	const uint8 ZeroBuffer[1024]{};
	const FBlake3Hash ZeroBufferHash = FBlake3::HashBuffer(MakeMemoryView(ZeroBuffer));

	// Test Null
	{
		const FCompressedBuffer Buffer;
		TestFalse(TEXT("FCompressedBuffer()"), (bool)Buffer);
		TestTrue(TEXT("FCompressedBuffer().IsNull()"), Buffer.IsNull());
		TestTrue(TEXT("FCompressedBuffer().IsOwned()"), Buffer.IsOwned());
		TestEqual(TEXT("FCompressedBuffer().GetCompressedSize()"), Buffer.GetCompressedSize(), uint64(0));
		TestEqual(TEXT("FCompressedBuffer().GetRawSize()"), Buffer.GetRawSize(), uint64(0));
		TestEqual(TEXT("FCompressedBuffer().GetRawHash()"), Buffer.GetRawHash(), FBlake3Hash::Zero);
		TestTrue(TEXT("FCompressedBuffer().Decompress()"), Buffer.Decompress().IsNull());
		TestTrue(TEXT("FCompressedBuffer().DecompressToComposite()"), Buffer.DecompressToComposite().IsNull());
	}

	// Test Method None
	{
		ECompressedBufferCompressor Compressor = ECompressedBufferCompressor::Kraken;
		ECompressedBufferCompressionLevel CompressionLevel = ECompressedBufferCompressionLevel::Normal;
		uint64 BlockSize = MAX_uint64;

		const FCompressedBuffer Buffer = FCompressedBuffer::Compress(FSharedBuffer::MakeView(MakeMemoryView(ZeroBuffer)),
			ECompressedBufferCompressor::NotSet, ECompressedBufferCompressionLevel::None);
		TestTrue(TEXT("FCompressedBuffer::Compress(None)"), (bool)Buffer);
		TestFalse(TEXT("FCompressedBuffer::Compress(None).IsNull()"), Buffer.IsNull());
		TestTrue(TEXT("FCompressedBuffer::Compress(None).IsOwned()"), Buffer.IsOwned());
		TestEqual(TEXT("FCompressedBuffer::Compress(None).GetCompressedSize()"), Buffer.GetCompressedSize(), sizeof(ZeroBuffer) + 64);
		TestEqual(TEXT("FCompressedBuffer::Compress(None).GetRawSize()"), Buffer.GetRawSize(), sizeof(ZeroBuffer));
		TestEqual(TEXT("FCompressedBuffer::Compress(None).GetRawHash()"), Buffer.GetRawHash(), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(None).Decompress()"), FBlake3::HashBuffer(Buffer.Decompress()), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(None).DecompressToComposite()"), FBlake3::HashBuffer(Buffer.DecompressToComposite()), Buffer.GetRawHash());
		TestTrue(TEXT("FCompressedBuffer::Compress(None).TryGetCompressParameters()"), Buffer.TryGetCompressParameters(Compressor, CompressionLevel, BlockSize) &&
			Compressor == ECompressedBufferCompressor::NotSet && CompressionLevel == ECompressedBufferCompressionLevel::None && BlockSize == 0);

		Compressor = ECompressedBufferCompressor::Kraken;
		CompressionLevel = ECompressedBufferCompressionLevel::Normal;
		BlockSize = MAX_uint64;

		const FCompressedBuffer BufferCopy = FCompressedBuffer::FromCompressed(Buffer.GetCompressed());
		TestTrue(TEXT("FCompressedBuffer::Compress(None, Copy)"), (bool)Buffer);
		TestFalse(TEXT("FCompressedBuffer::Compress(None, Copy).IsNull()"), Buffer.IsNull());
		TestTrue(TEXT("FCompressedBuffer::Compress(None, Copy).IsOwned()"), Buffer.IsOwned());
		TestEqual(TEXT("FCompressedBuffer::Compress(None, Copy).GetCompressedSize()"), Buffer.GetCompressedSize(), sizeof(ZeroBuffer) + 64);
		TestEqual(TEXT("FCompressedBuffer::Compress(None, Copy).GetRawSize()"), Buffer.GetRawSize(), sizeof(ZeroBuffer));
		TestEqual(TEXT("FCompressedBuffer::Compress(None, Copy).GetRawHash()"), Buffer.GetRawHash(), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(None, Copy).Decompress()"), FBlake3::HashBuffer(Buffer.Decompress()), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(None, Copy).DecompressToComposite()"), FBlake3::HashBuffer(Buffer.DecompressToComposite()), Buffer.GetRawHash());
		TestTrue(TEXT("FCompressedBuffer::Compress(None, Copy).TryGetCompressParameters()"), Buffer.TryGetCompressParameters(Compressor, CompressionLevel, BlockSize) &&
			Compressor == ECompressedBufferCompressor::NotSet && CompressionLevel == ECompressedBufferCompressionLevel::None && BlockSize == 0);
	}

	// Test Method Oodle
	{
		ECompressedBufferCompressor Compressor = ECompressedBufferCompressor::Kraken;
		ECompressedBufferCompressionLevel CompressionLevel = ECompressedBufferCompressionLevel::Normal;
		uint64 BlockSize = MAX_uint64;

		const FCompressedBuffer Buffer = FCompressedBuffer::Compress(FSharedBuffer::MakeView(MakeMemoryView(ZeroBuffer)),
			ECompressedBufferCompressor::Mermaid, ECompressedBufferCompressionLevel::VeryFast);
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle)"), (bool)Buffer);
		TestFalse(TEXT("FCompressedBuffer::Compress(Oodle).IsNull()"), Buffer.IsNull());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle).IsOwned()"), Buffer.IsOwned());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle).GetCompressedSize()"), Buffer.GetCompressedSize() < sizeof(ZeroBuffer));
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle).GetRawSize()"), Buffer.GetRawSize(), sizeof(ZeroBuffer));
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle).GetRawHash()"), Buffer.GetRawHash(), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle).Decompress()"), FBlake3::HashBuffer(Buffer.Decompress()), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle).DecompressToComposite()"), FBlake3::HashBuffer(Buffer.DecompressToComposite()), Buffer.GetRawHash());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle).TryGetCompressParameters()"), Buffer.TryGetCompressParameters(Compressor, CompressionLevel, BlockSize) &&
			Compressor == ECompressedBufferCompressor::Mermaid && CompressionLevel == ECompressedBufferCompressionLevel::VeryFast && FMath::IsPowerOfTwo(BlockSize));

		Compressor = ECompressedBufferCompressor::Kraken;
		CompressionLevel = ECompressedBufferCompressionLevel::Normal;
		BlockSize = MAX_uint64;

		const FCompressedBuffer BufferCopy = FCompressedBuffer::FromCompressed(Buffer.GetCompressed());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle, Copy)"), (bool)Buffer);
		TestFalse(TEXT("FCompressedBuffer::Compress(Oodle, Copy).IsNull()"), Buffer.IsNull());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle, Copy).IsOwned()"), Buffer.IsOwned());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle, Copy).GetCompressedSize()"), Buffer.GetCompressedSize() < sizeof(ZeroBuffer));
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle, Copy).GetRawSize()"), Buffer.GetRawSize(), sizeof(ZeroBuffer));
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle, Copy).GetRawHash()"), Buffer.GetRawHash(), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle, Copy).Decompress()"), FBlake3::HashBuffer(Buffer.Decompress()), ZeroBufferHash);
		TestEqual(TEXT("FCompressedBuffer::Compress(Oodle, Copy).DecompressToComposite()"), FBlake3::HashBuffer(Buffer.DecompressToComposite()), Buffer.GetRawHash());
		TestTrue(TEXT("FCompressedBuffer::Compress(Oodle, Copy).TryGetCompressParameters()"), Buffer.TryGetCompressParameters(Compressor, CompressionLevel, BlockSize) &&
			Compressor == ECompressedBufferCompressor::Mermaid && CompressionLevel == ECompressedBufferCompressionLevel::VeryFast && FMath::IsPowerOfTwo(BlockSize));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedBufferDecompressTest, "System.Core.Compression.CompressedBufferDecompress", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCompressedBufferDecompressTest::RunTest(const FString& Parameters)
{
	const auto GenerateData = [](int32 N) -> TArray<uint64>
	{
		TArray<uint64> Data;
		Data.SetNum(N);
		for (int32 Idx = 0; Idx < Data.Num(); ++Idx)
		{
			Data[Idx] = uint64(Idx);
		}
		return Data;
	};

	const auto ValidateData = [this](TConstArrayView<uint64> Values, TConstArrayView<uint64> ExpectedValues, int32 Offset)
	{
		int32 ExpectedIndex = Offset;
		for (uint64 Value : Values)
		{
			const uint64 ExpectedValue = ExpectedValues[ExpectedIndex];
			TestEqual("UncompressedValue", Value, ExpectedValue);
			ExpectedIndex++;
		}
	};

	const auto CastToArrayView = [](FMemoryView View) -> TConstArrayView<uint64>
	{
		return MakeArrayView(static_cast<const uint64*>(View.GetData()), static_cast<int32>(View.GetSize() / sizeof(uint64)));
	};

	FCompressedBufferReader Reader;

	// Test decompress with offset and size
	{
		const auto UncompressAndValidate = [this, &Reader, &ValidateData, &CastToArrayView](
			const FCompressedBuffer& Compressed,
			const int32 OffsetCount,
			const int32 Count,
			const TConstArrayView<uint64> ExpectedValues)
		{
			Reader.SetSource(Compressed);
			{
				const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
				const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
				TestEqual("UncompressedCount", Values.Num(), Count);
				ValidateData(Values, ExpectedValues, OffsetCount);
			}
			{
				FUniqueBuffer Uncompressed = FUniqueBuffer::Alloc(Count * sizeof(uint64));
				const bool bOk = Reader.TryDecompressTo(Uncompressed, OffsetCount * sizeof(uint64));
				TestTrue("UncompressedOk", bOk);
				const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
				ValidateData(Values, ExpectedValues, OffsetCount);
			}
		};

		constexpr uint64 BlockSize = 64 * sizeof(uint64);
		constexpr int32 N = 5000;
		const TArray<uint64> ExpectedValues = GenerateData(N);

		const FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::Mermaid,
			FOodleDataCompression::ECompressionLevel::Optimal4,
			BlockSize);

		UncompressAndValidate(Compressed, 0, N, ExpectedValues);
		UncompressAndValidate(Compressed, 1, N - 1, ExpectedValues);
		UncompressAndValidate(Compressed, N - 1, 1, ExpectedValues);
		UncompressAndValidate(Compressed, 0, 1, ExpectedValues);
		UncompressAndValidate(Compressed, 2, 4, ExpectedValues);
		UncompressAndValidate(Compressed, 0, 512, ExpectedValues);
		UncompressAndValidate(Compressed, 3, 514, ExpectedValues);
		UncompressAndValidate(Compressed, 256, 512, ExpectedValues);
		UncompressAndValidate(Compressed, 512, 512, ExpectedValues);
		UncompressAndValidate(Compressed, 512, 512, ExpectedValues);
		UncompressAndValidate(Compressed, 4993, 4, ExpectedValues);
	}

	// Decompress with offset only
	{
		constexpr uint64 BlockSize = 64 * sizeof(uint64);
		constexpr int32 N = 1000;
		const TArray<uint64> ExpectedValues = GenerateData(N);

		const FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::Mermaid,
			FOodleDataCompression::ECompressionLevel::Optimal4,
			BlockSize);
		
		constexpr uint64 OffsetCount = 150;
		{
			FSharedBuffer Buffer = Compressed.GetCompressed().ToShared();
			FBufferReader Ar(const_cast<void*>(Buffer.GetData()), int64(Buffer.GetSize()), /*bFreeOnClose*/ false, /*bIsPersistent*/ true);
			FCompressedBufferReaderSourceScope Source(Reader, Ar);
			const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64));
			const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
			ValidateData(Values, ExpectedValues, OffsetCount);
		}
		{
			FCompressedBufferReaderSourceScope Source(Reader, Compressed);
			const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64));
			const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
			ValidateData(Values, ExpectedValues, OffsetCount);
		}

		// Short Buffer
		{
			const FCompressedBuffer CompressedShort =
				FCompressedBuffer::FromCompressed(Compressed.GetCompressed().Mid(0, Compressed.GetCompressedSize() - 128));
			Reader.SetSource(CompressedShort);
			const FSharedBuffer Uncompressed = Reader.Decompress();
			TestTrue(TEXT("FCompressedBufferReader::Decompress(Oodle, Short)"), Uncompressed.IsNull());
		}
	}

	// Only one block
	{
		constexpr uint64 BlockSize = 256 * sizeof(uint64);
		constexpr int32 N = 100;
		const TArray<uint64> ExpectedValues = GenerateData(N);

		const FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::Mermaid,
			FOodleDataCompression::ECompressionLevel::Optimal4,
			BlockSize);

		constexpr uint64 OffsetCount = 2;
		constexpr uint64 Count = 50;
		{
			FSharedBuffer Buffer = Compressed.GetCompressed().ToShared();
			FBufferReader Ar(const_cast<void*>(Buffer.GetData()), int64(Buffer.GetSize()), /*bFreeOnClose*/ false, /*bIsPersistent*/ true);
			FCompressedBufferReaderSourceScope Source(Reader, Ar);
			const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
			ValidateData(Values, ExpectedValues, OffsetCount);
		}
		{
			FCompressedBufferReaderSourceScope Source(Reader, Compressed);
			const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
			ValidateData(Values, ExpectedValues, OffsetCount);
		}
	}

	// Uncompressed
	{
		constexpr int32 N = 4242;
		const TArray<uint64> ExpectedValues = GenerateData(N);

		const FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::NotSet,
			FOodleDataCompression::ECompressionLevel::None);
		Reader.SetSource(Compressed);

		{
			constexpr uint64 OffsetCount = 0;
			constexpr uint64 Count = N;
			const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
			ValidateData(Values, ExpectedValues, OffsetCount);
		}

		{
			constexpr uint64 OffsetCount = 21;
			constexpr uint64 Count = 999;
			const FSharedBuffer Uncompressed = Reader.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			const TConstArrayView<uint64> Values = CastToArrayView(Uncompressed);
			ValidateData(Values, ExpectedValues, OffsetCount);
		}

		// Short Buffer
		{
			const FCompressedBuffer CompressedShort =
				FCompressedBuffer::FromCompressed(Compressed.GetCompressed().Mid(0, Compressed.GetCompressedSize() - 128));
			Reader.SetSource(CompressedShort);
			const FSharedBuffer Uncompressed = Reader.Decompress();
			TestTrue(TEXT("FCompressedBufferReader::Decompress(None, Short)"), Uncompressed.IsNull());
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
