// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/HashTable.h"
#include "VirtualTextureShared.h"

class FAllocatedVirtualTexture;

/** Allocates virtual memory address space. */
class RENDERER_API FVirtualTextureAllocator
{
public:
	FVirtualTextureAllocator(uint32 Dimensions);
	~FVirtualTextureAllocator() {}

	/**
	 * Initialise the allocator with the given initial size.
	 */
	void Initialize(uint32 InSize);

	/**
	 * Increase size of region managed by allocator by factor of 2 in each dimension.
	 */
	void Grow();

	/**
	 * Translate a virtual page address in the address space to a local page address within a virtual texture.
	 * @return nullptr If there is no virtual texture allocated at this address.
	 */
	FAllocatedVirtualTexture* Find(uint32 vAddress, uint32& Local_vAddress) const;

	/**
	 * Allocate address space for the virtual texture.
	 * @return (~0) if no space left, the virtual page address if successfully allocated.
	 */
	uint32 Alloc(FAllocatedVirtualTexture* VT);

	/**
	 * Test if an allocation of the given size will succeed.
	 * @return false if there isn't enough space left.
	 */
	bool TryAlloc(uint32 InSize);

	/**
	 * Free the virtual texture.
	 */
	void Free(FAllocatedVirtualTexture* VT);

	/** Get current number of allocations. */
	inline uint32 GetNumAllocations() const { return NumAllocations; }

	/** Get current number of allocated pages. */
	inline uint32 GetNumAllocatedPages() const { return NumAllocatedPages; }

	/** Output debugging information to the console. */
	void DumpToConsole(bool verbose);

private:
	int32 AcquireBlock();
	void FreeAddressBlock(uint32 Index);
	uint32 Find(uint32 vAddress) const;

	struct FAddressBlock
	{
		FAllocatedVirtualTexture*	VT;
		uint32						vAddress : 24;
		uint32						vLogSize : 4;
		uint32						MipBias : 4;
		uint16						Parent;
		uint16						FirstChild;
		uint16						FirstSibling;
		uint16						NextSibling;
		uint16						NextFree;
		uint16						PrevFree;

		FAddressBlock()
		{}

		FAddressBlock(uint8 LogSize)
			: VT(nullptr)
			, vAddress(0)
			, vLogSize(LogSize)
			, MipBias(0)
			, Parent(0xffff)
			, FirstChild(0xffff)
			, FirstSibling(0xffff)
			, NextSibling(0xffff)
			, NextFree(0xffff)
			, PrevFree(0xffff)
		{}

		FAddressBlock(const FAddressBlock& Block, uint32 Offset, uint32 Dimensions)
			: VT(nullptr)
			, vAddress(Block.vAddress + (Offset << (Dimensions * Block.vLogSize)))
			, vLogSize(Block.vLogSize)
			, MipBias(0)
			, Parent(Block.Parent)
			, FirstChild(0xffff)
			, FirstSibling(Block.FirstSibling)
			, NextSibling(0xffff)
			, NextFree(0xffff)
			, PrevFree(0xffff)
		{}
	};

	const uint32			vDimensions;
	uint32					LogSize;

	TArray< FAddressBlock >	AddressBlocks;
	TArray< uint16 >		FreeList;
	uint16					GlobalFreeList;
	TArray< uint32 >		SortedAddresses;
	TArray< uint16 >		SortedIndices;
	FHashTable				HashTable;
	uint16                  RootIndex;

	uint32					NumAllocations;
	uint32					NumAllocatedPages;
};
