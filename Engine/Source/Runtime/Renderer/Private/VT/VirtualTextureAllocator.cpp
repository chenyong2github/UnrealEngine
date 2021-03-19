// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualTextureAllocator.h"
#include "AllocatedVirtualTexture.h"
#include "VirtualTexturing.h"

FVirtualTextureAllocator::FVirtualTextureAllocator(uint32 Dimensions)
	: vDimensions(Dimensions)
	, LogSize(0u)
	, NumAllocations(0u)
	, NumAllocatedPages(0u)
{
}

void FVirtualTextureAllocator::Initialize(uint32 InSize)
{
	check(NumAllocations == 0);
	LogSize = FMath::CeilLogTwo(InSize);

	AddressBlocks.Reset(1);
	SortedAddresses.Reset(1);
	SortedIndices.Reset(1);
	FreeList.Reset(LogSize + 1);

	// Start with one empty block
	FAddressBlock DefaultBlock(LogSize);
	DefaultBlock.State = EBlockState::FreeList;
	AddressBlocks.Add(DefaultBlock);

	SortedAddresses.Add(0u);
	SortedIndices.Add(0u);

	// Init free list
	FreeList.AddUninitialized(LogSize + 1);
	for (uint8 i = 0; i < LogSize; i++)
	{
		FreeList[i] = 0xffff;
	}
	FreeList[LogSize] = 0;

	// Init global free list
	GlobalFreeList = 0xffff;

	RootIndex = 0;
}

void FVirtualTextureAllocator::Grow()
{
	// If we are empty then fast path is to reinitialize
	if (NumAllocations == 0)
	{
		Initialize(1 << (LogSize + 1));
		return;
	}

	++LogSize;

	// Add entry for for free list of next LogSize (currently empty)
	FreeList.Add(0xffff);

	uint16 OldRootIndex = RootIndex;

	// Add new root block
	FAddressBlock RootBlock(LogSize);
	RootBlock.State = EBlockState::AllocatedChildren;
	RootBlock.FirstChild = OldRootIndex;

	RootIndex = AcquireBlock();
	AddressBlocks[RootIndex] = RootBlock;

	// Reparent old root block
	int32 NextSibling = AcquireBlock();
	AddressBlocks[OldRootIndex].Parent = RootIndex;
	AddressBlocks[OldRootIndex].FirstSibling = OldRootIndex;
	AddressBlocks[OldRootIndex].NextSibling = NextSibling;

	// Add new siblings for old root block
	const int32 NumChildren = (1 << vDimensions);
	for (int32 Sibling = 1; Sibling < NumChildren; Sibling++)
	{
		const int32 BlockIndex = NextSibling;
		NextSibling = (Sibling + 1 < NumChildren) ? AcquireBlock() : 0xffff;

		FAddressBlock Block(AddressBlocks[OldRootIndex], Sibling, vDimensions);
		Block.NextSibling = NextSibling;
		AddressBlocks[BlockIndex] = Block;
	}

	// Add new siblings to lists
	SortedAddresses.InsertUninitialized(0, NumChildren - 1);
	SortedIndices.InsertUninitialized(0, NumChildren - 1);
	check(SortedAddresses.Num() == SortedIndices.Num());

	int32 Sibling = 1;
	uint16 Index = AddressBlocks[OldRootIndex].NextSibling;
	while (Index != 0xffff)
	{
		FAddressBlock& AddressBlock = AddressBlocks[Index];

		// Place on free list
		check(AddressBlock.State == EBlockState::None);
		AddressBlock.State = EBlockState::FreeList;

		AddressBlock.NextFree = FreeList[AddressBlock.vLogSize];
		if (AddressBlock.NextFree != 0xffff)
		{
			AddressBlocks[AddressBlock.NextFree].PrevFree = Index;
		}
		FreeList[AddressBlock.vLogSize] = Index;

		// Add to sorted list
		// We are inserting sorted so need to be careful to add siblings in reverse order
		SortedAddresses[NumChildren - Sibling - 1] = AddressBlock.vAddress;
		SortedIndices[NumChildren - Sibling - 1] = Index;

		Index = AddressBlock.NextSibling;
		Sibling++;
	}
}

int32 FVirtualTextureAllocator::AcquireBlock()
{
	int32 Index = GlobalFreeList;
	if (Index == 0xffff)
	{
		Index = AddressBlocks.AddUninitialized();
	}
	else
	{
		check(AddressBlocks[Index].State == EBlockState::GlobalFreeList);
		GlobalFreeList = AddressBlocks[Index].NextFree;
		if (GlobalFreeList != 0xffff)
		{
			AddressBlocks[GlobalFreeList].PrevFree = 0xffff;
		}
	}

	// Debug fill memory to invalid value
	FAddressBlock& AddressBlock = AddressBlocks[Index];
	FMemory::Memset(AddressBlock, 0xCC);

	return Index;
}

uint32 FVirtualTextureAllocator::FindAddressBlock(uint32 vAddress) const
{
	uint32 Min = 0;
	uint32 Max = SortedAddresses.Num();

	// Binary search for lower bound
	while (Min != Max)
	{
		const uint32 Mid = Min + (Max - Min) / 2;
		const uint32 Key = SortedAddresses[Mid];

		if (vAddress < Key)
			Min = Mid + 1;
		else
			Max = Mid;
	}

	return Min;
}

FAllocatedVirtualTexture* FVirtualTextureAllocator::Find(uint32 vAddress, uint32& OutLocal_vAddress) const
{
	const uint32 SortedIndex = FindAddressBlock(vAddress);

	const uint16 Index = SortedIndices[SortedIndex];
	const FAddressBlock& AddressBlock = AddressBlocks[Index];
	check(SortedAddresses[SortedIndex] == AddressBlock.vAddress);

	FAllocatedVirtualTexture* AllocatedVT = nullptr;
	const uint32 BlockSize = 1 << (vDimensions * AddressBlock.vLogSize);
	if (vAddress >= AddressBlock.vAddress &&
		vAddress < AddressBlock.vAddress + BlockSize)
	{
		AllocatedVT = AddressBlock.VT;
		if (AllocatedVT)
		{
			OutLocal_vAddress = vAddress - AllocatedVT->GetVirtualAddress();
		}
		// TODO mip bias
	}

	return AllocatedVT;
}

bool FVirtualTextureAllocator::TryAlloc(uint32 InLogSize)
{
	for (int i = InLogSize; i < FreeList.Num(); i++)
	{
		uint16 FreeIndex = FreeList[i];
		if (FreeIndex != 0xffff)
		{
			return true;
		}
	}
	return false;
}

void FVirtualTextureAllocator::SubdivideBlock(uint32 ParentIndex)
{
	const uint32 NumChildren = (1 << vDimensions);
	
	const uint32 vLogSize = AddressBlocks[ParentIndex].vLogSize - 1u;
	const uint32 vAddress = AddressBlocks[ParentIndex].vAddress;

	const int32 SortedIndex = FindAddressBlock(vAddress);
	check(vAddress == SortedAddresses[SortedIndex]);

	// Make room for newly added
	SortedAddresses.InsertUninitialized(SortedIndex, NumChildren - 1u);
	SortedIndices.InsertUninitialized(SortedIndex, NumChildren - 1u);
	check(SortedAddresses.Num() == SortedIndices.Num());

	uint16 FirstSiblingIndex = 0xffff;
	uint16 PrevChildIndex = 0xffff;
	for (uint32 Sibling = 0; Sibling < NumChildren; Sibling++)
	{
		const int32 ChildBlockIndex = AcquireBlock();
		const uint32 vChildAddress = vAddress + (Sibling << (vDimensions * vLogSize));

		if (Sibling == 0u)
		{
			FirstSiblingIndex = ChildBlockIndex;
			AddressBlocks[ParentIndex].FirstChild = FirstSiblingIndex;
			// Replace parent block with new allocated block
			SortedIndices[SortedIndex] = FirstSiblingIndex;
		}
		else
		{
			const int32 SortedIndexOffset = NumChildren - 1 - Sibling;
			SortedAddresses[SortedIndex + SortedIndexOffset] = vChildAddress;
			SortedIndices[SortedIndex + SortedIndexOffset] = ChildBlockIndex;
			AddressBlocks[PrevChildIndex].NextSibling = ChildBlockIndex;
		}
	
		FAddressBlock ChildBlock(vLogSize);
		ChildBlock.vAddress = vChildAddress;
		ChildBlock.Parent = ParentIndex;
		ChildBlock.FirstSibling = FirstSiblingIndex;
		ChildBlock.NextSibling = 0xffff;

		// New child blocks start out on the free list
		ChildBlock.State = EBlockState::FreeList;
		ChildBlock.NextFree = FreeList[vLogSize];
		if (ChildBlock.NextFree != 0xffff)
		{
			AddressBlocks[ChildBlock.NextFree].PrevFree = ChildBlockIndex;
		}
		FreeList[vLogSize] = ChildBlockIndex;

		AddressBlocks[ChildBlockIndex] = ChildBlock;
		PrevChildIndex = ChildBlockIndex;
	}
}

void FVirtualTextureAllocator::MarkBlockAllocated(uint32 Index, uint32 vAllocatedTileX, uint32 vAllocatedTileY, FAllocatedVirtualTexture* VT)
{
	FAddressBlock* AllocBlock = &AddressBlocks[Index];
	check(AllocBlock->State == EBlockState::FreeList);
	check(AllocBlock->FirstChild == 0xffff);

	const uint32 vLogSize = AllocBlock->vLogSize;

	// check to see if block is in the correct position
	const uint32 vAllocatedTileX1 = vAllocatedTileX + VT->GetWidthInTiles();
	const uint32 vAllocatedTileY1 = vAllocatedTileY + VT->GetHeightInTiles();
	const uint32 vBlockAddress = AllocBlock->vAddress;
	const uint32 vBlockTileX = FMath::ReverseMortonCode2(vBlockAddress);
	const uint32 vBlockTileY = FMath::ReverseMortonCode2(vBlockAddress >> 1);
	const uint32 BlockSize = (1u << vLogSize);

	check(vBlockTileX >= vAllocatedTileX);
	check(vBlockTileY >= vAllocatedTileY);

	if (vBlockTileX < vAllocatedTileX1 && vBlockTileY < vAllocatedTileY1)
	{
		// Block overlaps the VT we're trying to allocate

		// Remove from free list
		{
			const uint32 PrevFreeIndex = AllocBlock->PrevFree;
			const uint32 NextFreeIndex = AllocBlock->NextFree;
			if (PrevFreeIndex != 0xffff)
			{
				AddressBlocks[PrevFreeIndex].NextFree = NextFreeIndex;
				AllocBlock->PrevFree = 0xffff;
			}
			if (NextFreeIndex != 0xffff)
			{
				AddressBlocks[NextFreeIndex].PrevFree = PrevFreeIndex;
				AllocBlock->NextFree = 0xffff;
			}
			if (FreeList[vLogSize] == Index)
			{
				FreeList[vLogSize] = NextFreeIndex;
			}
			AllocBlock->State = EBlockState::None;
		}

		if (vBlockTileX + BlockSize <= vAllocatedTileX1 || vBlockTileY + BlockSize <= vAllocatedTileY1)
		{
			// Block is entirely contained within the VT we're trying to allocate
			++NumAllocations;
			NumAllocatedPages += 1u << (vDimensions * vLogSize);

			// Add to hash table
			uint16 Key = reinterpret_cast<UPTRINT>(VT) / 16;
			HashTable.Add(Key, Index);

			AllocBlock->VT = VT;
			AllocBlock->State = EBlockState::AllocatedTexture;
		}
		else
		{
			// Block intersects the VT, subdivide further
			AllocBlock->State = EBlockState::AllocatedChildren;
			AllocBlock = nullptr; // list will be potentially reallocated

			SubdivideBlock(Index);
	
			uint32 NumChildren = 0u;
			uint16 ChildIndex = AddressBlocks[Index].FirstChild;
			check(ChildIndex == AddressBlocks[ChildIndex].FirstSibling);
			while (ChildIndex != 0xffff)
			{
				check(AddressBlocks[ChildIndex].Parent == Index);

				MarkBlockAllocated(ChildIndex, vAllocatedTileX, vAllocatedTileY, VT);

				ChildIndex = AddressBlocks[ChildIndex].NextSibling;
				NumChildren++;
			}
			check(NumChildren == (1u << vDimensions));
		}
	}
}

uint32 FVirtualTextureAllocator::Alloc(FAllocatedVirtualTexture* VT)
{
	// Pad out to square power of 2
	const uint32 MinSize = FMath::Min(VT->GetWidthInTiles(), VT->GetHeightInTiles());
	const uint32 MaxSize = FMath::Max(VT->GetWidthInTiles(), VT->GetHeightInTiles());
	const uint8 vLogMaxSize = FMath::CeilLogTwo(MaxSize);

	// Find smallest free that fits
	for (int i = vLogMaxSize; i < FreeList.Num(); i++)
	{
		uint16 FreeIndex = FreeList[i];
		if (FreeIndex != 0xffff)
		{
			// Found free
			uint16 AllocIndex = FreeIndex;
			FAddressBlock& AllocBlock = AddressBlocks[AllocIndex];
			check(AllocBlock.VT == nullptr);
			check(AllocBlock.State == EBlockState::FreeList);

			const uint32 vAddress = AllocBlock.vAddress;
			const uint32 vTileX = FMath::ReverseMortonCode2(vAddress);
			const uint32 vTileY = FMath::ReverseMortonCode2(vAddress >> 1);

			MarkBlockAllocated(AllocIndex, vTileX, vTileY, VT);

			check(AddressBlocks[AllocIndex].State != EBlockState::FreeList);

			return vAddress;
		}
	}

	return ~0u;
}

void FVirtualTextureAllocator::Free(FAllocatedVirtualTexture* VT)
{
	// Find block index
	uint16 Key = reinterpret_cast<UPTRINT>(VT) / 16;
	uint32 Index = HashTable.First(Key);
	while (HashTable.IsValid(Index))
	{
		const uint32 NextIndex = HashTable.Next(Index);
		FAddressBlock& AddressBlock = AddressBlocks[Index];
		if (AddressBlock.VT == VT)
		{
			check(AddressBlock.State == EBlockState::AllocatedTexture);
			check(AddressBlock.FirstChild == 0xffff); // texture allocation should be leaf
			AddressBlock.State = EBlockState::AllocatedChildren;
			AddressBlock.VT = nullptr;

			check(NumAllocations > 0u);
			--NumAllocations;

			const uint32 NumPagesForBlock = 1u << (vDimensions * AddressBlock.vLogSize);
			check(NumAllocatedPages >= NumPagesForBlock);
			NumAllocatedPages -= NumPagesForBlock;

			// Add block to free list
			// This handles merging free siblings
			FreeAddressBlock(Index);

			// Remove the index from the hash table as it may be reused later
			HashTable.Remove(Key, Index);
		}
		Index = NextIndex;
	}
}

void FVirtualTextureAllocator::FreeAddressBlock(uint32 Index)
{
	FAddressBlock& AddressBlock = AddressBlocks[Index];
	check(AddressBlock.VT == nullptr);
	check(AddressBlock.State == EBlockState::AllocatedChildren);
	check(AddressBlock.NextFree == 0xffff);
	check(AddressBlock.PrevFree == 0xffff);

	// If all siblings are free then we can merge them
	uint32 SiblingIndex = AddressBlock.FirstSibling;
	bool bConsolidateSiblings = SiblingIndex != 0xffff;
	while (bConsolidateSiblings && SiblingIndex != 0xffff)
	{
		bConsolidateSiblings &= (SiblingIndex == Index || FreeList[AddressBlock.vLogSize] == SiblingIndex || AddressBlocks[SiblingIndex].PrevFree != 0xffff);
		SiblingIndex = AddressBlocks[SiblingIndex].NextSibling;
	}

	if (!bConsolidateSiblings)
	{
		// If we got here, the block's children have already been consolidated/removed
		AddressBlock.FirstChild = 0xffff;

		// Simply place this block on the free list
		AddressBlock.State = EBlockState::FreeList;
		AddressBlock.NextFree = FreeList[AddressBlock.vLogSize];
		if (AddressBlock.NextFree != 0xffff)
		{
			AddressBlocks[AddressBlock.NextFree].PrevFree = Index;
		}
		FreeList[AddressBlock.vLogSize] = Index;
	}
	else
	{
		// Remove all of this block's siblings from free list and add to global free list
		uint32 FreeIndex = AddressBlock.FirstSibling;
		while (FreeIndex != 0xffff)
		{
			FAddressBlock& FreeBlock = AddressBlocks[FreeIndex];

			if (FreeIndex != Index)
			{
				// All our siblings must be free (we checked above to get into this case)
				check(FreeBlock.State == EBlockState::FreeList);

				const uint32 PrevFreeIndex = FreeBlock.PrevFree;
				const uint32 NextFreeIndex = FreeBlock.NextFree;
				if (PrevFreeIndex != 0xffff)
				{
					AddressBlocks[PrevFreeIndex].NextFree = NextFreeIndex;
					FreeBlock.PrevFree = 0xffff;
				}
				if (NextFreeIndex != 0xffff)
				{
					AddressBlocks[NextFreeIndex].PrevFree = PrevFreeIndex;
					FreeBlock.NextFree = 0xffff;
				}
				if (FreeList[AddressBlock.vLogSize] == FreeIndex)
				{
					FreeList[AddressBlock.vLogSize] = NextFreeIndex;
				}
			}

			if (GlobalFreeList != 0xffff)
			{
				FreeBlock.NextFree = GlobalFreeList;
				AddressBlocks[GlobalFreeList].PrevFree = FreeIndex;
			}
			GlobalFreeList = FreeIndex;
			FreeBlock.State = EBlockState::GlobalFreeList;

			FreeIndex = FreeBlock.NextSibling;
		}

		// Remove this block and its siblings from the sorted lists
		// We can assume that the sibling blocks are sequential in the sorted list since they are free and so have no children
		// FirstSibling will be the last in the range of siblings in the sorted lists 
		const uint32 SortedIndexRangeEnd = FindAddressBlock(AddressBlocks[AddressBlock.FirstSibling].vAddress);
		check(SortedAddresses[SortedIndexRangeEnd] == AddressBlocks[AddressBlock.FirstSibling].vAddress);
		const uint32 NumSiblings = 1 << vDimensions;
		check(SortedIndexRangeEnd + 1 >= NumSiblings);
		const uint32 SortedIndexRangeStart = SortedIndexRangeEnd + 1 - NumSiblings;

		// Remove all but one siblings because...
		SortedAddresses.RemoveAt(SortedIndexRangeStart, NumSiblings - 1, false);
		SortedIndices.RemoveAt(SortedIndexRangeStart, NumSiblings - 1, false);
		// ... we replace first sibling with parent
		SortedIndices[SortedIndexRangeStart] = AddressBlock.Parent;
		check(SortedAddresses[SortedIndexRangeStart] == AddressBlocks[AddressBlock.Parent].vAddress);

		// Add parent block to free list (and possibly consolidate)
		FreeAddressBlock(AddressBlock.Parent);
	}
}

void FVirtualTextureAllocator::DumpToConsole(bool verbose)
{
	for (int32 BlockID = SortedIndices.Num() - 1; BlockID >= 0; BlockID--)
	{
		FAddressBlock& Block = AddressBlocks[SortedIndices[BlockID]];
		uint32 X = FMath::ReverseMortonCode2(Block.vAddress);
		uint32 Y = FMath::ReverseMortonCode2(Block.vAddress >> 1);
		uint32 Size = 1 << Block.vLogSize;

		UE_LOG(LogVirtualTexturing, Display, TEXT("Block: vAddress %i,%i, size: %ix%i (tiles),  "), X, Y, Size, Size);
		if (Block.VT != nullptr)
		{
			if (verbose)
			{
				UE_LOG(LogVirtualTexturing, Display, TEXT("%p"), Block.VT);
			}
			Block.VT->DumpToConsole(verbose);
		}
		else
		{
			if (verbose)
			{
				UE_LOG(LogVirtualTexturing, Display, TEXT("NULL VT"));
			}
		}
	}
}
