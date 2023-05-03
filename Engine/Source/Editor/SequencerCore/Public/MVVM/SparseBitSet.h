// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/UnrealTemplate.h"
#include "Containers/Array.h"

namespace UE::Sequencer
{

template<typename T>
struct TDynamicSparseBitSetBucketStorage;

template<typename T>
struct TFixedSparseBitSetBucketStorage;

template<typename HashType, typename BucketStorage = TDynamicSparseBitSetBucketStorage<uint8>>
struct TSparseBitSet;


enum class ESparseBitSetBitResult
{
	NewlySet,
	AlreadySet,
};

/**
 * NOTE: This class is currently considered internal only, and should only be used by engine code.
 * A sparse bitset comprising a hash of integer indexes with set bits, and a sparse array of unsigned integers (referred to as buckets) whose width is defined by the storage.
 *
 * The maximum size bitfield that is representible by this bitset is defined as sizeof(HashType)*sizeof(BucketStorage::BucketType). For example, a 64 bit hash with 32 bit buckets
 * can represent a bitfield of upto 2048 bits.
 *
 * The hash allows for empty buckets to be completely omitted from memory, and affords very fast comparison for buckets that have no set bits.
 * This container is specialized for relatively large bitfields that have small numbers of set bits (ie, needles in haystacks) as they will provide the best memory vs CPU tradeoffs.
 */
template<typename HashType, typename BucketStorage>
struct TSparseBitSet
{
	using BucketType = typename BucketStorage::BucketType;
	static constexpr uint32 HashSize    = sizeof(HashType)*8;
	static constexpr uint32 BucketSize  = sizeof(typename BucketStorage::BucketType)*8;
	static constexpr uint32 MaxBitIndex = HashSize * BucketSize;

	TSparseBitSet()
		: BucketHash(0)
	{}

	TSparseBitSet(BucketStorage&& InBucketStorage)
		: Buckets(MoveTemp(InBucketStorage))
		, BucketHash(0)
	{}

	template<typename OtherHashType, typename OtherStorageType>
	void CopyTo(TSparseBitSet<OtherHashType, OtherStorageType>& Other) const
	{
		static_assert(TSparseBitSet<OtherHashType, OtherStorageType>::BucketSize == BucketSize, "Cannot copy sparse bitsets of different bucket sizes");

		// Copy the buckets
		const uint32 NumBuckets = FMath::CountBits(Other.BucketHash);
		Other.Buckets.SetNum(NumBuckets);
		CopyToUnsafe(Other, NumBuckets);
	}

	/** Copy this bitset to another without resizing the destination's bucket storage. Bucket storage must be >= this size. */
	template<typename OtherHashType, typename OtherStorageType>
	void CopyToUnsafe(TSparseBitSet<OtherHashType, OtherStorageType>& Other, uint32 OtherBucketCapacity) const
	{
		static_assert(TSparseBitSet<OtherHashType, OtherStorageType>::BucketSize == BucketSize, "Cannot copy sparse bitsets of different bucket sizes");

		const uint32 ThisNumBuckets = this->NumBuckets();
		checkf(OtherBucketCapacity >= ThisNumBuckets, TEXT("Attempting to copy a sparse bitset without enough capacity in the destination (%d, required %d)"), OtherBucketCapacity, ThisNumBuckets);

		// Copy the hash
		Other.BucketHash = this->BucketHash;

		// Copy the buckets
		FMemory::Memcpy(Other.Buckets.GetData(), this->Buckets.Storage.GetData(), sizeof(typename BucketStorage::BucketType)*ThisNumBuckets);
	}

	/**
	 * Count the number of buckets in this bitset
	 */
	uint32 NumBuckets() const
	{
		return FMath::CountBits(this->BucketHash);
	}

	/**
	 * Set the bit at the specified index.
	 * Any bits between Num and BitIndex will be considered 0.
	 *
	 * @return true if the bit was previously considered 0 and is now set, false if it was already set.
	 */
	ESparseBitSetBitResult SetBit(uint32 BitIndex)
	{
		CheckIndex(BitIndex);

		FBitOffsets Offsets(BucketHash, BitIndex);

		// Do we need to add a new bucket?
		if ( (BucketHash & Offsets.HashBit) == 0)
		{
			BucketHash |= Offsets.HashBit;
			Buckets.Insert(Offsets.BitMaskWithinBucket, Offsets.BucketIndex);
			return ESparseBitSetBitResult::NewlySet;
		}
		else if ((Buckets.Get(Offsets.BucketIndex) & Offsets.BitMaskWithinBucket) == 0)
		{
			Buckets.Get(Offsets.BucketIndex) |= Offsets.BitMaskWithinBucket;
			return ESparseBitSetBitResult::NewlySet;
		}

		return ESparseBitSetBitResult::AlreadySet;
	}

	/**
	 * Check whether the specified bit index is set
	 */
	bool IsBitSet(uint32 BitIndex) const
	{
		CheckIndex(BitIndex);

		const uint32 Hash    = BitIndex / BucketSize;
		const uint32 HashBit = (1 << Hash);
		if (BucketHash & HashBit)
		{
			const uint32     BucketIndex  = FMath::CountBits(BucketHash & (HashBit-1));
			const uint32     ThisBitIndex = (BitIndex-BucketSize*Hash);
			const BucketType ThisBitMask  = BucketType(1u) << ThisBitIndex;

			return Buckets.Get(BucketIndex) & ThisBitMask;
		}
		return false;
	}

	/**
	 * Get the sparse bucket index of the specified bit
	 */
	int32 GetSparseBucketIndex(uint32 BitIndex) const
	{
		CheckIndex(BitIndex);

		const uint32 Hash    = BitIndex / BucketSize;
		const uint32 HashBit = (1 << Hash);
		if (BucketHash & HashBit)
		{
			uint32 BucketIndex = FMath::CountBits(BucketHash & (HashBit-1));

			const uint32 ThisBitIndex = (BitIndex-BucketSize*Hash);
			const BucketType ThisBitMask = BucketType(1u) << ThisBitIndex;

			BucketType ThisBucket = Buckets.Get(BucketIndex);
			if (ThisBucket & ThisBitMask)
			{
				// Compute the offset
				int32 SparseIndex = FMath::CountBits(ThisBucket & (ThisBitMask-1));

				// Count all the preceding buckets to find the final sparse index
				while (BucketIndex > 0)
				{
					--BucketIndex;
					SparseIndex += FMath::CountBits(Buckets.Get(BucketIndex));
				}
				return SparseIndex;
			}
		}
		return INDEX_NONE;
	}

private:

	template<typename, typename>
	friend struct TSparseBitSet;

	FORCEINLINE void CheckIndex(uint32 BitIndex) const
	{
		checkfSlow(BitIndex < MaxBitIndex, TEXT("Invalid index (%d) specified for a sparse bitset of maximum size (%d)"), BitIndex, MaxBitIndex);
	}

	struct FBitOffsets
	{
		uint32 HashBit;
		int32  BucketIndex;
		BucketType BitMaskWithinBucket;
		FBitOffsets(HashType InBucketHash, uint32 BitIndex)
		{
			const uint32 Hash = BitIndex / BucketSize;
			HashBit = (1 << Hash);

			BucketIndex = FMath::CountBits(InBucketHash & (HashBit-1));

			const uint32 ThisBitIndex = (BitIndex-BucketSize*Hash);
			BitMaskWithinBucket = BucketType(1u) << ThisBitIndex;
		}
	};

	BucketStorage Buckets;
	HashType      BucketHash;
};



template<typename T>
struct TDynamicSparseBitSetBucketStorage
{
	using BucketType = T;

	TArray<BucketType, TInlineAllocator<8>> Storage;

	void Insert(BucketType InitialValue, int32 Index)
	{
		Storage.Insert(InitialValue, Index);
	}

	BucketType* GetData()              { return Storage.GetData(); }

	BucketType& Get(int32 Index)       { return Storage[Index]; }
	BucketType  Get(int32 Index) const { return Storage[Index]; }
};

template<typename T>
struct TFixedSparseBitSetBucketStorage
{
	using BucketType = T;

	BucketType* Storage = nullptr;

	BucketType* GetData()              { return Storage; }

	BucketType& Get(int32 Index)       { return Storage[Index]; }
	BucketType  Get(int32 Index) const { return Storage[Index]; }
};

} // namespace UE::Sequencer