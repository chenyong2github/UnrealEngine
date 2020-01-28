// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/SmallListSet.h"

const int32 FSmallListSet::NullValue = -1;

void FSmallListSet::Resize(int32 NewSize)
{
	int32 CurSize = (int32)ListHeads.GetLength();
	if (NewSize > CurSize)
	{
		ListHeads.Resize(NewSize);
		for (int32 k = CurSize; k < NewSize; ++k)
		{
			ListHeads[k] = NullValue;
		}
	}
}


void FSmallListSet::AllocateAt(int32 ListIndex)
{
	check(ListIndex >= 0);
	if (ListIndex >= (int)ListHeads.GetLength())
	{
		int32 j = (int32)ListHeads.GetLength();
		ListHeads.InsertAt(NullValue, ListIndex);
		// need to set intermediate values to null! 
		while (j < ListIndex)
		{
			ListHeads[j] = NullValue;
			j++;
		}
	}
	else
	{
		checkf(ListHeads[ListIndex] == NullValue, TEXT("FSmallListSet: list at %d is not empty!"), ListIndex);
	}
}




void FSmallListSet::Insert(int32 ListIndex, int32 Value)
{
	check(ListIndex >= 0);
	int32 block_ptr = ListHeads[ListIndex];
	if (block_ptr == NullValue)
	{
		block_ptr = AllocateBlock();
		ListBlocks[block_ptr] = 0;
		ListHeads[ListIndex] = block_ptr;
	}

	int32 N = ListBlocks[block_ptr];
	if (N < BLOCKSIZE) 
	{
		ListBlocks[block_ptr + N + 1] = Value;
	}
	else 
	{
		// spill to linked list
		int32 cur_head = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];

		if (FreeHeadIndex == NullValue)
		{
			// allocate linkedlist node
			int32 new_ptr = (int32)LinkedListElements.GetLength();
			LinkedListElements.Add(Value);
			LinkedListElements.Add(cur_head);
			ListBlocks[block_ptr + BLOCK_LIST_OFFSET] = new_ptr;
		}
		else 
		{
			// pull from free list
			int32 free_ptr = FreeHeadIndex;
			FreeHeadIndex = LinkedListElements[free_ptr + 1];
			LinkedListElements[free_ptr] = Value;
			LinkedListElements[free_ptr + 1] = cur_head;
			ListBlocks[block_ptr + BLOCK_LIST_OFFSET] = free_ptr;
		}
	}

	// count element
	ListBlocks[block_ptr] += 1;
}




bool FSmallListSet::Remove(int32 ListIndex, int32 Value)
{
	check(ListIndex >= 0);
	int32 block_ptr = ListHeads[ListIndex];
	int32 N = ListBlocks[block_ptr];


	int32 iEnd = block_ptr + FMath::Min(N, BLOCKSIZE);
	for (int32 i = block_ptr + 1; i <= iEnd; ++i) 
	{

		if (ListBlocks[i] == Value) 
		{
			// TODO since this is a set and order doesn't matter, shouldn't you just move the last thing 
			// to the empty spot rather than shifting the whole list left?
			for (int32 j = i + 1; j <= iEnd; ++j)     // shift left
			{
				ListBlocks[j - 1] = ListBlocks[j];
			}
			//block_store[iEnd] = -2;     // OPTIONAL

			if (N > BLOCKSIZE) 
			{
				int32 cur_ptr = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
				ListBlocks[block_ptr + BLOCK_LIST_OFFSET] = LinkedListElements[cur_ptr + 1];  // point32 to cur->next
				ListBlocks[iEnd] = LinkedListElements[cur_ptr];
				AddFreeLink(cur_ptr);
			}

			ListBlocks[block_ptr] -= 1;
			return true;
		}

	}

	// search list
	if (N > BLOCKSIZE) 
	{
		if (RemoveFromLinkedList(block_ptr, Value)) 
		{
			ListBlocks[block_ptr] -= 1;
			return true;
		}
	}

	return false;
}




void FSmallListSet::Move(int32 FromIndex, int32 ToIndex)
{
	check(FromIndex >= 0);
	check(ToIndex >= 0);
	check(ListHeads[ToIndex] == NullValue);
	check(ListHeads[FromIndex] != NullValue);
	ListHeads[ToIndex] = ListHeads[FromIndex];
	ListHeads[FromIndex] = NullValue;
}




void FSmallListSet::Clear(int32 ListIndex)
{
	check(ListIndex >= 0);
	int32 block_ptr = ListHeads[ListIndex];
	if (block_ptr != NullValue)
	{
		int32 N = ListBlocks[block_ptr];

		// if we have spilled to linked-list, free nodes
		if (N > BLOCKSIZE) 
		{
			int32 cur_ptr = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
			while (cur_ptr != NullValue)
			{
				int32 free_ptr = cur_ptr;
				cur_ptr = LinkedListElements[cur_ptr + 1];
				AddFreeLink(free_ptr);
			}
			ListBlocks[block_ptr + BLOCK_LIST_OFFSET] = NullValue;
		}

		// free our block
		ListBlocks[block_ptr] = 0;
		FreeBlocks.Add(block_ptr);
		ListHeads[ListIndex] = NullValue;
	}
}




bool FSmallListSet::Contains(int32 ListIndex, int32 Value) const
{
	check(ListIndex >= 0);
	int32 block_ptr = ListHeads[ListIndex];
	if (block_ptr != NullValue)
	{
		int32 N = ListBlocks[block_ptr];
		if (N < BLOCKSIZE) 
		{
			int32 iEnd = block_ptr + N;
			for (int32 i = block_ptr + 1; i <= iEnd; ++i) 
			{
				if (ListBlocks[i] == Value)
				{
					return true;
				}
			}
		}
		else 
		{
			// we spilled to linked list, have to iterate through it as well
			int32 iEnd = block_ptr + BLOCKSIZE;
			for (int32 i = block_ptr + 1; i <= iEnd; ++i) 
			{
				if (ListBlocks[i] == Value)
				{
					return true;
				}
			}
			int32 cur_ptr = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
			while (cur_ptr != NullValue)
			{
				if (LinkedListElements[cur_ptr] == Value)
				{
					return true;
				}
				cur_ptr = LinkedListElements[cur_ptr + 1];
			}
		}
	}
	return false;
}







int32 FSmallListSet::Find(int32 ListIndex, const TFunction<bool(int32)>& PredicateFunc, int32 InvalidValue) const
{
	int32 block_ptr = ListHeads[ListIndex];
	if (block_ptr != NullValue)
	{
		int32 N = ListBlocks[block_ptr];
		if (N < BLOCKSIZE)
		{
			int32 iEnd = block_ptr + N;
			for (int32 i = block_ptr + 1; i <= iEnd; ++i)
			{
				int32 Value = ListBlocks[i];
				if (PredicateFunc(Value))
				{
					return Value;
				}
			}
		}
		else
		{
			// we spilled to linked list, have to iterate through it as well
			int32 iEnd = block_ptr + BLOCKSIZE;
			for (int32 i = block_ptr + 1; i <= iEnd; ++i)
			{
				int32 Value = ListBlocks[i];
				if (PredicateFunc(Value))
				{
					return Value;
				}
			}
			int32 cur_ptr = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
			while (cur_ptr != NullValue)
			{
				int32 Value = LinkedListElements[cur_ptr];
				if (PredicateFunc(Value))
				{
					return Value;
				}
				cur_ptr = LinkedListElements[cur_ptr + 1];
			}
		}
	}
	return InvalidValue;
}






bool FSmallListSet::Replace(int32 ListIndex, const TFunction<bool(int32)>& PredicateFunc, int32 NewValue)
{
	int32 block_ptr = ListHeads[ListIndex];
	if (block_ptr != NullValue)
	{
		int32 N = ListBlocks[block_ptr];
		if (N < BLOCKSIZE)
		{
			int32 iEnd = block_ptr + N;
			for (int32 i = block_ptr + 1; i <= iEnd; ++i)
			{
				int32 Value = ListBlocks[i];
				if (PredicateFunc(Value))
				{
					ListBlocks[i] = NewValue;
					return true;
				}
			}
		}
		else
		{
			// we spilled to linked list, have to iterate through it as well
			int32 iEnd = block_ptr + BLOCKSIZE;
			for (int32 i = block_ptr + 1; i <= iEnd; ++i)
			{
				int32 Value = ListBlocks[i];
				if (PredicateFunc(Value))
				{
					ListBlocks[i] = NewValue;
					return true;
				}
			}
			int32 cur_ptr = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
			while (cur_ptr != NullValue)
			{
				int32 Value = LinkedListElements[cur_ptr];
				if (PredicateFunc(Value))
				{
					LinkedListElements[cur_ptr] = NewValue;
					return true;
				}
				cur_ptr = LinkedListElements[cur_ptr + 1];
			}
		}
	}
	return false;
}





int32 FSmallListSet::AllocateBlock()
{
	int32 nfree = (int32)FreeBlocks.GetLength();
	if (nfree > 0)
	{
		int32 ptr = FreeBlocks[nfree - 1];
		FreeBlocks.PopBack();
		return ptr;
	}
	int32 nsize = (int32)ListBlocks.GetLength();
	ListBlocks.InsertAt(NullValue, nsize + BLOCK_LIST_OFFSET);
	ListBlocks[nsize] = 0;
	AllocatedCount++;
	return nsize;
}



bool FSmallListSet::RemoveFromLinkedList(int32 block_ptr, int32 val)
{
	int32 cur_ptr = ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
	int32 prev_ptr = NullValue;
	while (cur_ptr != NullValue)
	{
		if (LinkedListElements[cur_ptr] == val)
		{
			int32 next_ptr = LinkedListElements[cur_ptr + 1];
			if (prev_ptr == NullValue)
			{
				ListBlocks[block_ptr + BLOCK_LIST_OFFSET] = next_ptr;
			}
			else
			{
				LinkedListElements[prev_ptr + 1] = next_ptr;
			}
			AddFreeLink(cur_ptr);
			return true;
		}
		prev_ptr = cur_ptr;
		cur_ptr = LinkedListElements[cur_ptr + 1];
	}
	return false;
}
