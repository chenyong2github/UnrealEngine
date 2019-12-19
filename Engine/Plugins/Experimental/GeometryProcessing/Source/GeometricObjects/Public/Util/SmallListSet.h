// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp small_list_set

#pragma once

#include "Util/DynamicVector.h"

/**
 * FSmallListSet stores a set of short integer-valued variable-size lists.
 * The lists are encoded into a few large TDynamicVector buffers, with internal pooling,
 * so adding/removing lists usually does not involve any or delete ops.
 * 
 * The lists are stored in two parts. The first N elements are stored in a linear
 * subset of a TDynamicVector. If the list spills past these N elements, the extra elements
 * are stored in a linked list (which is also stored in a flat array).
 * 
 * Each list stores its count, so list-size operations are constant time.
 * All the internal "pointers" are 32-bit.
 * 
 * @todo look at usage of TFunction, are we making unnecessary copies?
 */
class GEOMETRICOBJECTS_API FSmallListSet
{
protected:
	/** This value is used to indicate Null in internal pointers */
	static const int32 NullValue; // = -1;		// note: cannot be constexpr because we pass as reference to several functions, requires C++17

	/** size of initial linear-memory portion of lists */
	static constexpr int32 BLOCKSIZE = 8;
	/** offset from start of linear-memory portion of list that contains pointer to head of variable-length linked list */
	static constexpr int32 BLOCK_LIST_OFFSET = BLOCKSIZE + 1;

	/** mapping from list index to offset into block_store that contains list data */
	TDynamicVector<int32> ListHeads{};

	/** 
	 * flat buffer used to store per-list linear-memory blocks. 
	 * blocks are BLOCKSIZE+2 long, elements are [CurrentCount, item0...itemN, LinkedListPtr]
	 */
	TDynamicVector<int32> ListBlocks{};

	/** list of free blocks as indices/offsets into block_store */
	TDynamicVector<int32> FreeBlocks{};

	/** number of allocated lists */
	int32 AllocatedCount{0};

	/**
	 * flat buffer used to store linked-list "spill" elements
	 * each element is [value, next_ptr]
	 */
	TDynamicVector<int32> LinkedListElements{};

	/** index of first free element in linked_store */
	int32 FreeHeadIndex{NullValue};


public:
	/**
	 * @return largest current list index
	 */
	size_t Size() const 
	{
		return ListHeads.GetLength();
	}

	/**
	 * set new number of lists
	 */
	void Resize(int32 NewSize);


	/**
	 * @return true if a list has been allocated at the given ListIndex
	 */
	bool IsAllocated(int32 ListIndex) const
	{
		return (ListIndex >= 0 && ListIndex < (int32)ListHeads.GetLength() && ListHeads[ListIndex] != NullValue);
	}

	/**
	 * Create a list at the given ListIndex
	 */
	void AllocateAt(int32 ListIndex);


	/**
	 * Insert Value into list at ListIndex
	 */
	void Insert(int32 ListIndex, int32 Value);



	/**
	 * remove Value from the list at ListIndex
	 * @return false if Value was not in this list
	 */
	bool Remove(int32 ListIndex, int32 Value);



	/**
	 * Move list at FromIndex to ToIndex
	 */
	void Move(int32 FromIndex, int32 ToIndex);



	/**
	 * Remove all elements from the list at ListIndex
	 */
	void Clear(int32 ListIndex);


	/**
	 * @return the size of the list at ListIndex
	 */
	inline int32 GetCount(int32 ListIndex) const
	{
		check(ListIndex >= 0);
		int32 block_ptr = ListHeads[ListIndex];
		return (block_ptr == NullValue) ? 0 : ListBlocks[block_ptr];
	}


	/**
	 * @return the first item in the list at ListIndex
	 * @warning does not check for zero-size-list!
	 */
	inline int32 First(int32 ListIndex) const
	{
		check(ListIndex >= 0);
		int32 block_ptr = ListHeads[ListIndex];
		return ListBlocks[block_ptr + 1];
	}


	/**
	 * Search for the given Value in list at ListIndex
	 * @return true if found
	 */
	bool Contains(int32 ListIndex, int32 Value) const;


	/**
	 * Search the list at ListIndex for a value where PredicateFunc(value) returns true
	 * @return the found value, or the InvalidValue argument if not found
	 */
	int32 Find(int32 ListIndex, const TFunction<bool(int32)>& PredicateFunc, int32 InvalidValue = -1) const;



	/**
	 * Search the list at ListIndex for a value where PredicateFunc(value) returns true, and replace it with NewValue
	 * @return true if the value was found and replaced
	 */
	bool Replace(int32 ListIndex, const TFunction<bool(int32)>& PredicateFunc, int32 NewValue);



	//
	// iterator support
	// 


	friend class ValueIterator;

	/**
	 * ValueIterator iterates over the values of a small list
	 * An optional mapping function can be provided which will then be applied to the values returned by the * operator
	 */
	class ValueIterator
	{
	public:
		inline ValueIterator() 
		{ 
			ListSet = nullptr;
			MapFunc = nullptr; 
			ListIndex = 0; 
		}

		inline bool operator==(const ValueIterator& Other) const 
		{
			return ListSet == Other.ListSet && ListIndex == Other.ListIndex;
		}
		inline bool operator!=(const ValueIterator& Other) const 
		{
			return ListSet != Other.ListSet || ListIndex != Other.ListIndex || iCur != Other.iCur || cur_ptr != Other.cur_ptr;
		}

		inline int32 operator*() const 
		{
			return (MapFunc == nullptr) ? cur_value : MapFunc(cur_value);
		}

		inline const ValueIterator& operator++() 		// prefix
		{
			this->GotoNext();
			return *this;
		}
		//inline ValueIterator operator++(int32) {		// postfix
		//	index_iterator copy(*this);
		//	this->GotoNext();
		//	return copy;
		//}


	protected:
		inline void GotoNext() 
		{
			if (N == 0)
			{
				SetToEnd();
				return;
			}
			GotoNextOverflow();
		}

		inline void GotoNextOverflow() 
		{
			if (iCur <= iEnd) 
			{
				cur_value = ListSet->ListBlocks[iCur];
				iCur++;
			}
			else if (cur_ptr != NullValue) 
			{
				cur_value = ListSet->LinkedListElements[cur_ptr];
				cur_ptr = ListSet->LinkedListElements[cur_ptr + 1];
			}
			else
			{
				SetToEnd();
			}
		}

		inline ValueIterator(
			const FSmallListSet* ListSetIn,
			int32 ListIndex, 
			bool is_end,
			const TFunction<int32(int32)>& MapFuncIn = nullptr)
		{
			this->ListSet = ListSetIn;
			this->MapFunc = MapFuncIn;
			this->ListIndex = ListIndex;
			if (is_end) 
			{
				SetToEnd();
			}
			else 
			{
				block_ptr = ListSet->ListHeads[ListIndex];
				if (block_ptr != ListSet->NullValue)
				{
					N = ListSet->ListBlocks[block_ptr];
					iEnd = (N < BLOCKSIZE) ? (block_ptr + N) : (block_ptr + BLOCKSIZE);
					iCur = block_ptr + 1;
					cur_ptr = (N < BLOCKSIZE) ? NullValue : ListSet->ListBlocks[block_ptr + BLOCK_LIST_OFFSET];
					GotoNext();
				}
				else
				{
					SetToEnd();
				}
			}
		}

		inline void SetToEnd() 
		{
			block_ptr = ListSet->NullValue;
			N = 0;
			iCur = -1;
			cur_ptr = -1;
		}

		const FSmallListSet * ListSet;
		TFunction<int32(int32)> MapFunc;
		int32 ListIndex;
		int32 block_ptr;
		int32 N;
		int32 iEnd;
		int32 iCur;
		int32 cur_ptr;
		int32 cur_value;
		friend class FSmallListSet;
	};

	/**
	 * @return iterator for start of list at ListIndex
	 */
	inline ValueIterator BeginValues(int32 ListIndex) const 
	{
		return ValueIterator(this, ListIndex, false);
	}

	/**
	 * @return iterator for start of list at ListIndex, with given value mapping function
	 */
	inline ValueIterator BeginValues(int32 ListIndex, const TFunction<int32(int32)>& MapFunc) const 
	{
		return ValueIterator(this, ListIndex, false, MapFunc);
	}

	/**
	 * @return iterator for end of list at ListIndex
	 */
	inline ValueIterator EndValues(int32 ListIndex) const 
	{
		return ValueIterator(this, ListIndex, true);
	}

	/**
	 * ValueEnumerable is an object that provides begin/end semantics for a small list, suitable for use with a range-based for loop
	 */
	class ValueEnumerable
	{
	public:
		const FSmallListSet* ListSet;
		int32 ListIndex;
		TFunction<int32(int32)> MapFunc;
		ValueEnumerable() {}
		ValueEnumerable(const FSmallListSet* ListSetIn, int32 ListIndex, TFunction<int32(int32)> MapFunc = nullptr)
		{
			this->ListSet = ListSetIn;
			this->ListIndex = ListIndex;
			this->MapFunc = MapFunc;
		}
		typename FSmallListSet::ValueIterator begin() const { return ListSet->BeginValues(ListIndex, MapFunc); }
		typename FSmallListSet::ValueIterator end() const { return ListSet->EndValues(ListIndex); }
	};

	/**
	 * @return a value enumerable for the given ListIndex
	 */
	inline ValueEnumerable Values(int32 ListIndex) const
	{
		return ValueEnumerable(this, ListIndex);
	}

	/**
	 * @return a value enumerable for the given ListIndex, with the given value mapping function
	 */
	inline ValueEnumerable Values(int32 ListIndex, const TFunction<int32(int32)>& MapFunc) const 
	{
		return ValueEnumerable(this, ListIndex, MapFunc);
	}




protected:


	// grab a block from the free list, or allocate a one
	int32 AllocateBlock();

	// push a link-node onto the free list
	inline void AddFreeLink(int32 ptr)
	{
		LinkedListElements[ptr + 1] = FreeHeadIndex;
		FreeHeadIndex = ptr;
	}


	// remove val from the linked-list attached to block_ptr
	bool RemoveFromLinkedList(int32 block_ptr, int32 val);


public:
	inline FString MemoryUsage()
	{
		return FString::Printf(TEXT("ListSize %d  Blocks Count %d  Free %d  Mem %dkb   Linked Mem %dkb"),
			ListHeads.GetLength(), AllocatedCount, (FreeBlocks.GetLength() * sizeof(int32) / 1024),
			ListBlocks.GetLength(), (LinkedListElements.GetLength() * sizeof(int32) / 1024));
	}


};
