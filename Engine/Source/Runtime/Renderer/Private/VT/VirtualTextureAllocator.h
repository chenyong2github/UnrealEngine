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
	explicit FVirtualTextureAllocator(uint32 Dimensions);
	~FVirtualTextureAllocator() {}

	/**
	 * Initialise the allocator
	 */
	void Initialize(uint32 MaxSize);

	uint32 GetAllocatedWidth() const { return AllocatedWidth; }
	uint32 GetAllocatedHeight() const { return AllocatedHeight; }

	/**
	 * Translate a virtual page address in the address space to a local page address within a virtual texture.
	 * @return nullptr If there is no virtual texture allocated at this address.
	 */
	FAllocatedVirtualTexture* Find(uint32 vAddress, uint32& OutLocal_vAddress) const;
	inline FAllocatedVirtualTexture* Find(uint32 vAddress) const { uint32 UnusedLocal_vAddress = 0u; return Find(vAddress, UnusedLocal_vAddress); }

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

#if WITH_EDITOR
	void SaveDebugImage(const TCHAR* ImageName) const;
#endif

private:
	enum class EBlockState : uint8
	{
		None,
		GlobalFreeList,
		FreeList,
		PartiallyFreeList,
		AllocatedTexture,
	};

	struct FTestRegion
	{
		uint32 BaseIndex;
		uint32 vTileX0;
		uint32 vTileY0;
		uint32 vTileX1;
		uint32 vTileY1;
	};

	void LinkFreeList(uint16& InOutListHead, EBlockState State, uint16 Index);
	void UnlinkFreeList(uint16& InOutListHead, EBlockState State, uint16 Index);

	int32 AcquireBlock();
	void FreeAddressBlock(uint32 Index, bool bTopLevelBlock);
	uint32 FindAddressBlock(uint32 vAddress) const;

	void SubdivideBlock(uint32 ParentIndex);

	void MarkBlockAllocated(uint32 Index, uint32 vAllocatedTileX, uint32 vAllocatedTileY, FAllocatedVirtualTexture* VT);

	bool TestAllocation(uint32 Index, uint32 vTileX0, uint32 vTileY0, uint32 vTileX1, uint32 vTileY1) const;

#if WITH_EDITOR
	void FillDebugImage(uint32 Index, uint32* ImageData, TMap<FAllocatedVirtualTexture*, uint32>& ColorMap) const;
#endif

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
		EBlockState                 State;

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
			, State(EBlockState::None)
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
			, State(EBlockState::None)
		{}
	};

	const uint32			vDimensions;
	uint32					AllocatedWidth;
	uint32					AllocatedHeight;

	TArray< FAddressBlock >	AddressBlocks;
	TArray< uint16 >		FreeList;
	TArray< uint16 >		PartiallyFreeList;
	uint16					GlobalFreeList;
	TArray< uint32 >		SortedAddresses;
	TArray< uint16 >		SortedIndices;
	FHashTable				HashTable;
	uint16                  RootIndex;

	uint32					NumAllocations;
	uint32					NumAllocatedPages;
};
