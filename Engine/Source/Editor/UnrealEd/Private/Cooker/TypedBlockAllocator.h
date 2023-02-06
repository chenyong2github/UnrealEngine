// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMath.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"

/**
 * An allocator that batches allocation calls into blocks to reduce malloc costs.
 * 
 * Not threadsafe; must be guarded by critical section if used from multiple threads.
 * 
 * Block size is adaptive; it doubles with each new block, up to MaxBlockSize.
 * 
 * Allocated memory is not released until Empty is called.
 */
template <typename ElementType>
class TTypedBlockAllocator
{
public:
	/** Set the MinimumBlockSize. Default is 1024. All new blocks will be this size or larger. */
	void SetMinBlockSize(int32 BlockSize);
	int32 GetMinBlockSize() const;
	/**
	 * Set the MaximumBlockSize. Default is 65536. New blocks added by Alloc will be this size or
	 * less (ignored if MaximumBlockSize < MinimumBlockSize). Blocks added by Reserve are not
	 * limited by MaximumBlockSize. 
	 */
	void SetMaxBlockSize(int32 BlockSize);
	int32 GetMaxBlockSize() const;

	/** Return the memory for an Element without calling a constructor. */
	ElementType* Alloc();
	/**
	 * Make the memory for an Element returned from Alloc or Construct available again to Alloc.
	 * Does not call the ElementType destructor. Does not return the memory to the inner allocator.
	 */
	void Free(ElementType* Element);
	/** Allocate an Element and call constructor with the given arguments. */
	template <typename... ArgsType>
	ElementType* NewElement(ArgsType&&... Args);
	/** Call destructor on the given element and call Free on its memory. */
	void DeleteElement(ElementType* Element);

	/**
	 * Call the given Callback on every element that has been returned from Alloc. Not valid to call if any
	 * elements have been Freed because it is too expensive to prevent calling the Callback on the freed elements.
	 */
	template <typename CallbackType>
	void EnumerateAllocations(CallbackType&& Callback);

	/**
	 * Allocate enough memory from the inner allocator to ensure that AllocationCount more calls to Alloc can be made
	 * without further calls to the inner allocator.
	 * 
	 * @param AllocationCount The number of future calls to Alloc that should reserved.
	 * @param InMaxBlockSize If non-zero, the maximum capacity of any Blocks allocated by ReserveDelta.
	 *        If zero, MaxBlockSize is used.
	 */
	void ReserveDelta(int32 AllocationCount, int32 InMaxBlockSize = 0);
	/**
	 * Release all allocated memory. For performance, Empty does not require that allocations have been destructed
	 * or freed; caller is responsible for calling any necessary ElementType destructors and for dropping all
	 * references to the allocated ElementTypes.
	 */
	void Empty();

private:
	struct FAllocationBlock
	{
		FAllocationBlock(int32 InCapacity);

		TUniquePtr<TTypeCompatibleBytes<ElementType>[]> Elements;
		int32 NextIndex;
		int32 Capacity;
	};
	TArray<FAllocationBlock> Blocks;
	ElementType* FreeList = nullptr;
	int32 NextBlock = 0;
	int32 NumAllocations = 0;
	int32 NumFreeList = 0;
	int32 MinBlockSize = 1024;
	int32 MaxBlockSize = 65536;
};


template <typename ElementType>
inline void TTypedBlockAllocator<ElementType>::SetMinBlockSize(int32 BlockSize)
{
	check(BlockSize >= 0);
	MinBlockSize = FMath::Max(BlockSize, 1);
}

template <typename ElementType>
inline int32 TTypedBlockAllocator<ElementType>::GetMinBlockSize() const
{
	return MinBlockSize;
}

template <typename ElementType>
inline void TTypedBlockAllocator<ElementType>::SetMaxBlockSize(int32 BlockSize)
{
	check(BlockSize >= 0);
	MaxBlockSize = BlockSize;
}

template <typename ElementType>
inline int32 TTypedBlockAllocator<ElementType>::GetMaxBlockSize() const
{
	return MaxBlockSize;
}

template <typename ElementType>
ElementType* TTypedBlockAllocator<ElementType>::Alloc()
{
	ElementType* Result = nullptr;
	if (FreeList)
	{
		check(NumFreeList > 0);
		Result = FreeList;
		FreeList = *reinterpret_cast<ElementType**>(FreeList);
		++NumAllocations;
		--NumFreeList;
		return Result;
	}

	FAllocationBlock* Block = nullptr;
	for (; NextBlock < Blocks.Num(); ++NextBlock)
	{
		if (Blocks[NextBlock].NextIndex < Blocks[NextBlock].Capacity)
		{
			Block = &Blocks[NextBlock];
			break;
		}
	}

	if (!Block)
	{
		check(NextBlock == Blocks.Num());
		// Double our NumAllocations with each new block, up to a maximum block size
		int32 BlockCapacity = FMath::Max(NumAllocations, MinBlockSize);
		if (MaxBlockSize > MinBlockSize)
		{
			BlockCapacity = FMath::Min(BlockCapacity, MaxBlockSize);
		}
		Block = &Blocks.Emplace_GetRef(BlockCapacity);
	}

	Result = Block->Elements[Block->NextIndex].GetTypedPtr();
	++Block->NextIndex;
	++NumAllocations;
	return Result;
}

template <typename ElementType>
void TTypedBlockAllocator<ElementType>::Free(ElementType* Element)
{
	if constexpr (sizeof(ElementType) <= sizeof(ElementType*))
	{
		static_assert(sizeof(ElementType) >= sizeof(ElementType*),
			"The FreeList is implemented by storing pointers within each freed element. To use the FreeList, elementsize must be >= pointer size.");
	}
	check(NumAllocations > 0);

	*reinterpret_cast<ElementType**>(Element) = FreeList;
	FreeList = Element;
	--NumAllocations;
	++NumFreeList;
}

template <typename ElementType>
template <typename... ArgsType>
ElementType* TTypedBlockAllocator<ElementType>::NewElement(ArgsType&&... Args)
{
	return new(Alloc()) ElementType(Forward<ArgsType>(Args)...);
}

template <typename ElementType>
void TTypedBlockAllocator<ElementType>::DeleteElement(ElementType* Element)
{
	// We need a typedef here because VC won't compile the destructor call below if ElementType itself has a member
	// called ElementType
	typedef ElementType TTypedBlockAllocatorDestructElementType;
	((TTypedBlockAllocatorDestructElementType*)Element)->
		TTypedBlockAllocatorDestructElementType::~TTypedBlockAllocatorDestructElementType();
	Free(Element);
}

template <typename ElementType>
template <typename CallbackType>
void TTypedBlockAllocator<ElementType>::EnumerateAllocations(CallbackType&& Callback)
{
	checkf(FreeList == nullptr, TEXT("It is invalid to call EnumerateAllocations after calling Free or Destruct."));
	for (const FAllocationBlock& Block : Blocks)
	{
		for (TTypeCompatibleBytes<ElementType>& Bytes :
			TArrayView<TTypeCompatibleBytes<ElementType>>(Block.Elements.Get(), Block.NextIndex))
		{
			ElementType* Element = Bytes.GetTypedPtr();
			Callback(Element);
		}
	}
}

template <typename ElementType>
void TTypedBlockAllocator<ElementType>::ReserveDelta(int32 AllocationCount, int32 InMaxBlockSize)
{
	int32 DeltaAllocationCount = AllocationCount - NumFreeList;
	for (const FAllocationBlock& Block: TConstArrayView<FAllocationBlock>(Blocks).RightChop(NextBlock))
	{
		DeltaAllocationCount -= Block.Capacity - Block.NextIndex;
	}
	if (InMaxBlockSize == 0)
	{
		InMaxBlockSize = MaxBlockSize;
	}
	if (DeltaAllocationCount <= 0)
	{
		return;
	}

	// Allocate blocks until we have enough capacity to cover the Reservation. As with blocks allocated from
	// Alloc, set the unclamped capacity high enough to double our number of allocations. But also set it high
	// enough to cover the remaining count, and use the different MaxBlockSize if passed in.
	int32 BlockCapacity = FMath::Max(DeltaAllocationCount, NumAllocations);
	BlockCapacity = FMath::Max(BlockCapacity, MinBlockSize);
	if (InMaxBlockSize > MinBlockSize)
	{
		BlockCapacity = FMath::Min(BlockCapacity, InMaxBlockSize);
	}
	Blocks.Emplace(BlockCapacity);
	DeltaAllocationCount -= BlockCapacity;

	if (DeltaAllocationCount > 0)
	{
		// For further blocks, use the MaxBlockSize
		check(InMaxBlockSize > MinBlockSize); // Otherwise we would have allocated all of DeltaAllocationCount in a single block
		BlockCapacity = InMaxBlockSize;
		while (DeltaAllocationCount > 0)
		{
			Blocks.Emplace(BlockCapacity);
			DeltaAllocationCount -= BlockCapacity;
		}
	}
}

template <typename ElementType>
void TTypedBlockAllocator<ElementType>::Empty()
{
	Blocks.Empty();
	FreeList = nullptr;
	NextBlock = 0;
	NumAllocations = 0;
	NumFreeList = 0;
}

template <typename ElementType>
TTypedBlockAllocator<ElementType>::FAllocationBlock::FAllocationBlock(int32 InCapacity)
	: Elements(InCapacity > 0 ? new TTypeCompatibleBytes<ElementType>[InCapacity] : nullptr)
	, NextIndex(0)
	, Capacity(InCapacity)
{
}
