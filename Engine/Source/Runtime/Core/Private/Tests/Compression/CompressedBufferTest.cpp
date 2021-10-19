// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/CompressedBuffer.h"
#include "Compression/OodleDataCompression.h"

#include "Misc/AutomationTest.h"

PRAGMA_DISABLE_OPTIMIZATION
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedBufferDecompressTest, "System.Core.Compression.CompressedBufferDecompress", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCompressedBufferDecompressTest::RunTest(const FString& Parameters)
{
	auto GenerateData = [](int32 N) -> TArray<uint64> {
		TArray<uint64> Data;
		Data.SetNum(N);
		for (int32 Idx = 0; Idx < Data.Num(); ++Idx)
		{
			Data[Idx] = uint64(Idx);
		}
		return Data;
	};

	auto ValidateData = [this](TArrayView<uint64 const> Values, TArrayView<uint64 const> ExpectedValues, int32 Offset) {
		int32 ExpectedIndex = Offset;	
		for (uint64 Value : Values)
		{
			const uint64 ExpectedValue = ExpectedValues[ExpectedIndex];
			TestEqual("UncompressedValue", Value, ExpectedValue);
			ExpectedIndex++;
		}
	};

	// Test decompress with offset and size
	{
		auto UncompressAndValidate = [this, &ValidateData](
			FCompressedBuffer Compressed,
			int32 OffsetCount,
			int32 Count,
			const TArrayView<uint64 const>& ExpectedValues) {
			
			FSharedBuffer Uncompressed = Compressed.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			TArrayView<uint64 const> Values((const uint64*)Uncompressed.GetData(), int32(Uncompressed.GetSize() / sizeof(uint64)));
			TestEqual("UncompressedCount", Values.Num(), Count);
			ValidateData(Values, ExpectedValues, OffsetCount);
		};

		const uint64 BlockSize			= 64 * sizeof(uint64);
		const int32 N					= 5000;
		TArray<uint64> ExpectedValues	= GenerateData(N);
		
		FCompressedBuffer Compressed = FCompressedBuffer::Compress(
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
		const uint64 BlockSize			= 64 * sizeof(uint64);
		const int32 N					= 1000;
		TArray<uint64> ExpectedValues	= GenerateData(N);
		
		FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::Mermaid,
			FOodleDataCompression::ECompressionLevel::Optimal4,
			BlockSize);
		
		const uint64				OffsetCount	 = 150;
		FSharedBuffer				Uncompressed = Compressed.Decompress(OffsetCount * sizeof(uint64));
		TArrayView<uint64 const>	Values((const uint64*)Uncompressed.GetData(), int32(Uncompressed.GetSize() / sizeof(uint64)));
		ValidateData(Values, ExpectedValues, OffsetCount);
	}

	// Only one block
	{
		const uint64 BlockSize			= 256 * sizeof(uint64);
		const int32 N					= 100;
		TArray<uint64> ExpectedValues	= GenerateData(N);
		
		FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::Mermaid,
			FOodleDataCompression::ECompressionLevel::Optimal4,
			BlockSize);
		
		const uint64 OffsetCount		= 2;
		const uint64 Count				= 50;
		FSharedBuffer Uncompressed		= Compressed.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
		TArrayView<uint64 const> Values((const uint64*)Uncompressed.GetData(), int32(Uncompressed.GetSize() / sizeof(uint64)));
		ValidateData(Values, ExpectedValues, OffsetCount);
	}

	// Uncompressed
	{
		const int32 N					= 4242;
		TArray<uint64> ExpectedValues	= GenerateData(N);
		
		FCompressedBuffer Compressed = FCompressedBuffer::Compress(
			FSharedBuffer::MakeView(MakeMemoryView(ExpectedValues)),
			FOodleDataCompression::ECompressor::NotSet,
			FOodleDataCompression::ECompressionLevel::None);
		
		{
			const uint64 OffsetCount = 0;
			const uint64 Count = N;
			FSharedBuffer Uncompressed = Compressed.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			TArrayView<uint64 const> Values((const uint64*)Uncompressed.GetData(), int32(Uncompressed.GetSize() / sizeof(uint64)));
			ValidateData(Values, ExpectedValues, OffsetCount);
		}

		{
			const uint64 OffsetCount = 21;
			const uint64 Count = 999;
			FSharedBuffer Uncompressed = Compressed.Decompress(OffsetCount * sizeof(uint64), Count * sizeof(uint64));
			TArrayView<uint64 const> Values((const uint64*)Uncompressed.GetData(), int32(Uncompressed.GetSize() / sizeof(uint64)));
			ValidateData(Values, ExpectedValues, OffsetCount);
		}
	}

	return true;
}
PRAGMA_ENABLE_OPTIMIZATION
