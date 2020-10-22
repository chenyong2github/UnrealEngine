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
	AddressBlocks.Add(FAddressBlock(LogSize));
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
	if (GlobalFreeList == 0xffff)
	{
		return AddressBlocks.AddUninitialized();
	}

	int32 FreeBlock = GlobalFreeList;

	GlobalFreeList = AddressBlocks[GlobalFreeList].NextFree;
	if (GlobalFreeList != 0xffff)
	{
		AddressBlocks[GlobalFreeList].PrevFree = 0xffff;
	}

	return FreeBlock;
}

uint32 FVirtualTextureAllocator::Find(uint32 vAddress) const
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

FAllocatedVirtualTexture* FVirtualTextureAllocator::Find(uint32 vAddress, uint32& Local_vAddress) const
{
	const uint32 SortedIndex = Find(vAddress);

	const uint16 Index = SortedIndices[SortedIndex];
	const FAddressBlock& AddressBlock = AddressBlocks[Index];
	check(SortedAddresses[SortedIndex] == AddressBlock.vAddress);

	const uint32 BlockSize = 1 << (vDimensions * AddressBlock.vLogSize);
	if (vAddress >= AddressBlock.vAddress &&
		vAddress < AddressBlock.vAddress + BlockSize)
	{
		Local_vAddress = vAddress - AddressBlock.vAddress;
		// TODO mip bias
		return AddressBlock.VT;
	}

	return nullptr;
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

uint32 FVirtualTextureAllocator::Alloc(FAllocatedVirtualTexture* VT)
{
	// Pad out to square power of 2
	const uint32 BlockSize = FMath::Max(VT->GetWidthInTiles(), VT->GetHeightInTiles());
	const uint8 vLogSize = FMath::CeilLogTwo(BlockSize);

	// Find smallest free that fits
	for (int i = vLogSize; i < FreeList.Num(); i++)
	{
		uint16 FreeIndex = FreeList[i];
		if (FreeIndex != 0xffff)
		{
			// Found free
			uint16 AllocIndex = FreeIndex;
			FAddressBlock* AllocBlock = &AddressBlocks[AllocIndex];
			check(AllocBlock->VT == nullptr);
			check(AllocBlock->PrevFree == 0xffff);

			// Remove from free list
			FreeList[i] = AllocBlock->NextFree;
			if (AllocBlock->NextFree != 0xffff)
			{
				AddressBlocks[AllocBlock->NextFree].PrevFree = 0xffff;
				AllocBlock->NextFree = 0xffff;
			}

			// Recursive subdivide to requested size
			TArray<int32, TInlineAllocator<32>> NewBlocks;
			while (AllocBlock->vLogSize > vLogSize)
			{
				const uint32 NumChildren = (1 << vDimensions);

				// Create child blocks
				const int32 FirstChildIndex = AcquireBlock();
				int32 NextSibling = AcquireBlock();

				AllocBlock = &AddressBlocks[AllocIndex];
				AllocBlock->FirstChild = FirstChildIndex;

				FAddressBlock FirstChildBlock(AllocBlock->vLogSize - 1);
				FirstChildBlock.vAddress = AllocBlock->vAddress;
				FirstChildBlock.Parent = AllocIndex;
				FirstChildBlock.FirstSibling = FirstChildIndex;
				FirstChildBlock.NextSibling = NextSibling;
				AddressBlocks[FirstChildIndex] = FirstChildBlock;

				for (uint32 Sibling = 1; Sibling < NumChildren; Sibling++)
				{
					const int32 BlockIndex = NextSibling;
					NextSibling = (Sibling + 1 < NumChildren) ? AcquireBlock() : 0xffff;

					FAddressBlock Block(FirstChildBlock, Sibling, vDimensions);
					Block.NextSibling = NextSibling;
					AddressBlocks[BlockIndex] = Block;
					NewBlocks.Add(BlockIndex);
				}

				AllocIndex = FirstChildIndex;
				AllocBlock = &AddressBlocks[AllocIndex];
			}

			// If new blocks were generated add them to the lists
			if (NewBlocks.Num())
			{
				FAddressBlock* ParentBlock = &AddressBlocks[FreeIndex];
				check(ParentBlock->vAddress == AllocBlock->vAddress);

				const int32 SortedIndex = Find(AllocBlock->vAddress);
				check(AllocBlock->vAddress == SortedAddresses[SortedIndex]);

				// Replace parent block with new allocated block
				SortedIndices[SortedIndex] = AllocIndex;
				// Make room for newly added
				SortedAddresses.InsertUninitialized(SortedIndex, NewBlocks.Num());
				SortedIndices.InsertUninitialized(SortedIndex, NewBlocks.Num());
				check(SortedAddresses.Num() == SortedIndices.Num());

				// Add new blocks to lists
				int32 DepthCount = 0;
				while (ParentBlock->FirstChild != 0xffff)
				{
					// Add siblings at this size
					int32 Sibling = 1;
					uint16 Index = AddressBlocks[ParentBlock->FirstChild].NextSibling;
					while (Index != 0xffff)
					{
						FAddressBlock& AddressBlock = AddressBlocks[Index];

						// Place on free list
						AddressBlock.NextFree = FreeList[AddressBlock.vLogSize];
						if (AddressBlock.NextFree != 0xffff)
						{
							AddressBlocks[AddressBlock.NextFree].PrevFree = Index;
						}
						FreeList[AddressBlock.vLogSize] = Index;

						// Add to sorted list
						// We are inserting sorted so need to be careful to add siblings in reverse order
						const int32 NumChildren = (1 << vDimensions);
						const int32 SortedIndexOffset = (DepthCount + 1) * (NumChildren - 1) - Sibling;
						SortedAddresses[SortedIndex + SortedIndexOffset] = AddressBlock.vAddress;
						SortedIndices[SortedIndex + SortedIndexOffset] = Index;

						Index = AddressBlock.NextSibling;
						Sibling++;
					}

					// Now handle child siblings
					ParentBlock = &AddressBlocks[ParentBlock->FirstChild];
					DepthCount++;
				}
			}

			++NumAllocations;
			NumAllocatedPages += 1u << (vDimensions * vLogSize);

			// Add to hash table
			uint16 Key = reinterpret_cast<UPTRINT>(VT) / 16;
			HashTable.Add(Key, AllocIndex);

			AllocBlock->VT = VT;
			return AllocBlock->vAddress;
		}
	}

	return ~0u;
}

void FVirtualTextureAllocator::Free(FAllocatedVirtualTexture* VT)
{
	// Find block index
	uint16 Key = reinterpret_cast<UPTRINT>(VT) / 16;
	uint32 Index;
	for (Index = HashTable.First(Key); HashTable.IsValid(Index); Index = HashTable.Next(Index))
	{
		if (AddressBlocks[Index].VT == VT)
		{
			break;
		}
	}
	if (HashTable.IsValid(Index))
	{
		FAddressBlock& AddressBlock = AddressBlocks[Index];
		check(AddressBlock.VT == VT);
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
}

void FVirtualTextureAllocator::FreeAddressBlock(uint32 Index)
{
	FAddressBlock& AddressBlock = AddressBlocks[Index];
	check(AddressBlock.VT == nullptr);
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
		// Simply place this block on the free list
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

			if (GlobalFreeList != 0xffff)
			{
				FreeBlock.NextFree = GlobalFreeList;
				AddressBlocks[GlobalFreeList].PrevFree = FreeIndex;
			}
			GlobalFreeList = FreeIndex;

			FreeIndex = FreeBlock.NextSibling;
		}

		// Remove this block and its siblings from the sorted lists
		// We can assume that the sibling blocks are sequential in the sorted list since they are free and so have no children
		// FirstSibling will be the last in the range of siblings in the sorted lists 
		const uint32 SortedIndexRangeEnd = Find(AddressBlocks[AddressBlock.FirstSibling].vAddress);
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
