// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hash/xxhash.h"

#include "Async/ParallelFor.h"
#include "Containers/ContainersFwd.h"
#include "Memory/CompositeBuffer.h"
#include "Memory/MemoryView.h"
#include "Memory/SharedBuffer.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Tasks/Task.h"

THIRD_PARTY_INCLUDES_START
#define XXH_INLINE_ALL
#include "ThirdParty/xxhash/xxhash.h"
THIRD_PARTY_INCLUDES_END

FXxHash64 FXxHash64::HashBuffer(FMemoryView View)
{
	return {XXH3_64bits(View.GetData(), View.GetSize())};
}

FXxHash64 FXxHash64::HashBuffer(const void* Data, uint64 Size)
{
	return {XXH3_64bits(Data, Size)};
}

FXxHash64 FXxHash64::HashBuffer(const FCompositeBuffer& Buffer)
{
	FXxHash64Builder Builder;
	Builder.Update(Buffer);
	return Builder.Finalize();
}


/* static */ UE::Tasks::TTask<FXxHash64> FXxHash64::HashBufferChunkedAsync(FMemoryView View, uint64 ChunkSize)
{
	if (View.GetSize() <= ChunkSize || ChunkSize == 0)
	{
		return UE::Tasks::Launch(TEXT("XxHash64.HashBufferChunkedAsync.Single"), [View]()
		{
			return FXxHash64::HashBuffer(View);
		});
	}

	return UE::Tasks::Launch(TEXT("XxHash64.HashBufferChunkedAsync.Multi"), [View, ChunkSize]()
	{
		uint64 ChunkCount = (View.GetSize() + ChunkSize - 1) / ChunkSize;

		// Due to limitations of ParallelFor, we can't hash anything larger than uint32_max * chunksize;
		uint32 ChunkCount32 = IntCastChecked<uint32, uint64>(ChunkCount);

		TArray<FXxHash64, TInlineAllocator<64>> ChunkHashes;
		ChunkHashes.AddDefaulted(ChunkCount32);

		ParallelFor(TEXT("XxHash64.PF"), ChunkCount32, 1, [View, ChunkSize, ChunkCount32, &ChunkHashes](int32 Index)
		{
			FMemoryView ChunkView = View.Mid(Index * ChunkSize, ChunkSize);
			ChunkHashes[Index] = FXxHash64::HashBuffer(ChunkView);
		});

		FXxHash64Builder Accumulator;
		for (FXxHash64& ChunkHash : ChunkHashes)
		{
			Accumulator.Update(&ChunkHash.Hash, sizeof(ChunkHash.Hash));
		}
		uint64 ViewSize = View.GetSize();
		Accumulator.Update(&ViewSize, sizeof(ViewSize));

		return Accumulator.Finalize();
	});
}

/* static */ UE::Tasks::TTask<FXxHash64> FXxHash64::HashBufferChunkedAsync(const void* Data, uint64 Size, uint64 ChunkSize)
{
	return HashBufferChunkedAsync(FMemoryView(Data, Size), ChunkSize);
}


FXxHash128 FXxHash128::HashBuffer(FMemoryView View)
{
	const XXH128_hash_t Value = XXH3_128bits(View.GetData(), View.GetSize());
	return {Value.low64, Value.high64};
}

FXxHash128 FXxHash128::HashBuffer(const void* Data, uint64 Size)
{
	const XXH128_hash_t Value = XXH3_128bits(Data, Size);
	return {Value.low64, Value.high64};
}

/* static */ UE::Tasks::TTask<FXxHash128> FXxHash128::HashBufferChunkedAsync(FMemoryView View, uint64 ChunkSize)
{
	if (View.GetSize() <= ChunkSize || ChunkSize == 0)
	{
		return UE::Tasks::Launch(TEXT("XxHash128.HashBufferChunkedAsync.Single"), [View]()
		{
			return FXxHash128::HashBuffer(View);
		});
	}

	return UE::Tasks::Launch(TEXT("XxHash128.HashBufferChunkedAsync.Multi"), [View, ChunkSize]()
	{
		uint64 ChunkCount = (View.GetSize() + ChunkSize - 1) / ChunkSize;
		
		// Due to limitations of ParallelFor, we can't hash anything larger than uint32_max * chunksize;
		uint32 ChunkCount32 = IntCastChecked<uint32, uint64>(ChunkCount);

		TArray<FXxHash128, TInlineAllocator<64>> ChunkHashes;
		ChunkHashes.AddDefaulted(ChunkCount32);

		ParallelFor(TEXT("XxHash128.PF"), ChunkCount32, 1, [View, ChunkSize, ChunkCount32, &ChunkHashes](int32 Index)
		{
			FMemoryView ChunkView = View.Mid(Index * ChunkSize, ChunkSize);
			ChunkHashes[Index] = FXxHash128::HashBuffer(ChunkView);
		});

		FXxHash128Builder Accumulator;
		for (FXxHash128& ChunkHash : ChunkHashes)
		{
			uint64 Hash[2] = {ChunkHash.HashLow, ChunkHash.HashHigh};
			Accumulator.Update(Hash, sizeof(Hash));
		}
		uint64 ViewSize = View.GetSize();
		Accumulator.Update(&ViewSize, sizeof(ViewSize));
		
		return Accumulator.Finalize();
	});
}

/* static */ UE::Tasks::TTask<FXxHash128> FXxHash128::HashBufferChunkedAsync(const void* Data, uint64 Size, uint64 ChunkSize)
{
	return HashBufferChunkedAsync(FMemoryView(Data, Size), ChunkSize);
}



FXxHash128 FXxHash128::HashBuffer(const FCompositeBuffer& Buffer)
{
	FXxHash128Builder Builder;
	Builder.Update(Buffer);
	return Builder.Finalize();
}

void FXxHash64Builder::Reset()
{
	static_assert(sizeof(StateBytes) == sizeof(XXH3_state_t), "Adjust the allocation in FXxHash64Builder to match XXH3_state_t");
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	XXH3_64bits_reset(&State);
}

void FXxHash64Builder::Update(FMemoryView View)
{
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	XXH3_64bits_update(&State, View.GetData(), View.GetSize());
}

void FXxHash64Builder::Update(const void* Data, uint64 Size)
{
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	XXH3_64bits_update(&State, Data, Size);
}

void FXxHash64Builder::Update(const FCompositeBuffer& Buffer)
{
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	for (const FSharedBuffer& Segment : Buffer.GetSegments())
	{
		XXH3_64bits_update(&State, Segment.GetData(), Segment.GetSize());
	}
}

FXxHash64 FXxHash64Builder::Finalize() const
{
	const XXH3_state_t& State = reinterpret_cast<const XXH3_state_t&>(StateBytes);
	const XXH64_hash_t Hash = XXH3_64bits_digest(&State);
	return {Hash};
}

void FXxHash128Builder::Reset()
{
	static_assert(sizeof(StateBytes) == sizeof(XXH3_state_t), "Adjust the allocation in FXxHash128Builder to match XXH3_state_t");
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	XXH3_128bits_reset(&State);
}

void FXxHash128Builder::Update(FMemoryView View)
{
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	XXH3_128bits_update(&State, View.GetData(), View.GetSize());
}

void FXxHash128Builder::Update(const void* Data, uint64 Size)
{
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	XXH3_128bits_update(&State, Data, Size);
}

void FXxHash128Builder::Update(const FCompositeBuffer& Buffer)
{
	XXH3_state_t& State = reinterpret_cast<XXH3_state_t&>(StateBytes);
	for (const FSharedBuffer& Segment : Buffer.GetSegments())
	{
		XXH3_128bits_update(&State, Segment.GetData(), Segment.GetSize());
	}
}

FXxHash128 FXxHash128Builder::Finalize() const
{
	const XXH3_state_t& State = reinterpret_cast<const XXH3_state_t&>(StateBytes);
	const XXH128_hash_t Hash = XXH3_128bits_digest(&State);
	return {Hash.low64, Hash.high64};
}
