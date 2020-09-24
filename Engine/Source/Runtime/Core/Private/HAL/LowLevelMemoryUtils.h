// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"

#if ENABLE_LOW_LEVEL_MEM_TRACKER

#define LLM_PAGE_SIZE (16*1024)

#if WITH_EDITOR && PLATFORM_64BITS
// When cooking, the number of simultaneous allocations can reach the danger zone of tens of millions, and our margin*capacity calculation ~ 100*capacity will rise over MAX_uint32
typedef uint64 LLMNumAllocsType;
#else
// Even in our 64 bit runtimes, the number of simultaneous allocations we have never gets over a few million, so we don't reach the danger zone of 100*capacity > MAX_uInt32
typedef uint32 LLMNumAllocsType;
#endif

// POD types only
template<typename T, typename SizeType=uint32>
class FLLMArray
{
public:
	FLLMArray()
		: Array(StaticArray)
		, Count(0)
		, Capacity(StaticArrayCapacity)
		, Allocator(NULL)
	{
	}

	~FLLMArray()
	{
		Clear(true);
	}

	void SetAllocator(FLLMAllocator* InAllocator)
	{
		Allocator = InAllocator;
	}

	SizeType Num() const
	{
		return Count;
	}

	void Clear(bool ReleaseMemory = false)
	{
		if (ReleaseMemory)
		{
			if (Array != StaticArray)
			{
				Allocator->Free(Array, Capacity * sizeof(T));
				Array = StaticArray;
			}
			Capacity = StaticArrayCapacity;
		}
		Count = 0;
	}

	void Add(const T& Item)
	{
		if (Count == Capacity)
		{
			SizeType NewCapacity = DefaultCapacity;
			if (Capacity)
			{
				NewCapacity = Capacity + (Capacity / 2);
				ensureMsgf(NewCapacity > Capacity, TEXT("Unsigned integer overflow."));
			}
			Reserve(NewCapacity);
		}

		Array[Count] = Item;
		++Count;
	}

	T RemoveLast()
	{
		LLMCheck(Count > 0);
		--Count;
		T Last = Array[Count];

		return Last;
	}

	T& operator[](SizeType Index)
	{
		LLMCheck(Index >= 0 && Index < Count);
		return Array[Index];
	}

	const T& operator[](SizeType Index) const
	{
		LLMCheck(Index >= 0 && Index < Count);
		return Array[Index];
	}

	const T* GetData() const {
		return Array;
	}

	T& GetLast()
	{
		LLMCheck(Count > 0);
		return Array[Count - 1];
	}

	void Reserve(SizeType NewCapacity)
	{
		if (NewCapacity == Capacity)
		{
			return;
		}

		if (NewCapacity <= StaticArrayCapacity)
		{
			if (Array != StaticArray)
			{
				if (Count)
				{
					memcpy(StaticArray, Array, Count * sizeof(T));
				}
				Allocator->Free(Array, Capacity * sizeof(T));

				Array = StaticArray;
				Capacity = StaticArrayCapacity;
			}
		}
		else
		{
			NewCapacity = AlignArbitrary(NewCapacity, ItemsPerPage);

			T* NewArray = (T*)Allocator->Alloc(NewCapacity * sizeof(T));

			if (Count)
			{
				memcpy(NewArray, Array, Count * sizeof(T));
			}
			if (Array != StaticArray)
			{
				Allocator->Free(Array, Capacity * sizeof(T));
			}

			Array = NewArray;
			Capacity = NewCapacity;
		}
	}

	void operator=(const FLLMArray<T>& Other)
	{
		Clear();
		Reserve(Other.Count);
		memcpy(Array, Other.Array, Other.Count * sizeof(T));
		Count = Other.Count;
	}

	void Trim()
	{
		// Trim if usage has dropped below 3/4 of the total capacity
		if (Array != StaticArray && Count < (Capacity - (Capacity / 4)))
		{
			Reserve(Count);
		} 
	}

private:
	T* Array;
	SizeType Count;
	SizeType Capacity;

	FLLMAllocator* Allocator;

	static const int StaticArrayCapacity = 64;	// because the default size is so large this actually saves memory
	T StaticArray[StaticArrayCapacity];

	static const int ItemsPerPage = LLM_PAGE_SIZE / sizeof(T);
	static const int DefaultCapacity = ItemsPerPage;
};

// calls constructor/destructor
template<class T>
class FLLMObjectAllocator
{
	struct Block
	{
		Block* Next;
	};

public:
	FLLMObjectAllocator()
		: BlockList(NULL)
		, FreeList(NULL)
	{
	}

	~FLLMObjectAllocator()
	{
		Clear();
	}

	void Clear()
	{
		Block* BlockIter = BlockList;
		while (BlockIter)
		{
			Block* Next = BlockIter->Next;
			Allocator->Free(BlockIter, BlockSize);
			BlockIter = Next;
		}

		BlockList = NULL;
		FreeList = NULL;
	}

	T* New()
	{
		T* Item = FreeList;

		if (!Item)
		{
			AllocNewFreeList();
			Item = FreeList;
		}

		FreeList = *(T**)Item;
		new (Item)T();
		return Item;
	}

	void Delete(T* Item)
	{
		Item->~T();
		*(T**)Item = FreeList;
		FreeList = Item;
	}

	void SetAllocator(FLLMAllocator* InAllocator)
	{
		Allocator = InAllocator;
	}

private:
	void AllocNewFreeList()
	{
		Block* NewBlock = (Block*)Allocator->Alloc(BlockSize);
		NewBlock->Next = BlockList;
		BlockList = NewBlock;

		LLMNumAllocsType FirstOffset = sizeof(Block);
		LLMNumAllocsType ItemCount = (BlockSize - FirstOffset) / sizeof(T);
		FreeList = (T*)((char*)NewBlock + FirstOffset);
		T* Item = FreeList;
		for (LLMNumAllocsType i = 0; i+1 < ItemCount; ++i, ++Item)
		{
			*(T**)Item = Item + 1;
		}
		*(T**)Item = NULL;
	}

private:
	static const int BlockSize = LLM_PAGE_SIZE;
	Block* BlockList;
	T* FreeList;

	FLLMAllocator* Allocator;
};

/*
* hash map
*/
template<typename TKey, typename TValue1, typename TValue2, typename SizeType=int32>
class LLMMap
{
public:
	struct Values
	{
		TValue1 Value1;
		TValue2 Value2;
	};

	LLMMap()
		: Allocator(NULL)
		, Map(NULL)
		, Count(0)
		, Capacity(0)
#ifdef PROFILE_LLMMAP
		, IterAcc(0)
		, IterCount(0)
#endif
	{
	}

	~LLMMap()
	{
		Clear();
	}

	void SetAllocator(FLLMAllocator* InAllocator, SizeType InDefaultCapacity = DefaultCapacity)
	{
		FScopeLock Lock(&CriticalSection);

		Allocator = InAllocator;

		Keys.SetAllocator(Allocator);
		KeyHashes.SetAllocator(Allocator);
		Values1.SetAllocator(Allocator);
		Values2.SetAllocator(Allocator);
		FreeKeyIndices.SetAllocator(Allocator);

		Reserve(InDefaultCapacity);
	}

	void Clear()
	{
		FScopeLock Lock(&CriticalSection);

		Keys.Clear(true);
		KeyHashes.Clear(true);
		Values1.Clear(true);
		Values2.Clear(true);
		FreeKeyIndices.Clear(true);

		Allocator->Free(Map, Capacity * sizeof(SizeType));
		Map = NULL;
		Count = 0;
		Capacity = 0;
	}

	// Add a value to this set.
	// If this set already contains the value does nothing.
	void Add(const TKey& Key, const TValue1& Value1, const TValue2& Value2)
	{
		LLMCheck(Map);

		SizeType KeyHash = Key.GetHashCode();

		FScopeLock Lock(&CriticalSection);

		SizeType MapIndex = GetMapIndex(Key, KeyHash);
		SizeType KeyIndex = Map[MapIndex];

		if (KeyIndex != InvalidIndex)
		{
			static bool ShownWarning = false;
			if (!ShownWarning)
			{
				FPlatformMisc::LowLevelOutputDebugString(TEXT("LLM WARNING: Replacing allocation in tracking map. Alloc/Free Mismatch.\n"));
				ShownWarning = true;
			}

			Values1[KeyIndex] = Value1;
			Values2[KeyIndex] = Value2;
		}
		else
		{
			SizeType MaxCount = (Margin * Capacity) / 256U;
			if (Count >= MaxCount)
			{
				if (Count > MaxCount)
				{
					// This shouldn't happen, because Count is only incremented here, and Capacity is only changed here, and Margin does not change, so Count should equal MaxCount before it goes over it
					FPlatformMisc::LowLevelOutputDebugString(TEXT("LLM Error: Integer overflow in LLMap::Add, Count > MaxCount.\n"));
					// Trying to issue a check statement here will cause reentry into this function, use PLATFORM_BREAK directly instead
					PLATFORM_BREAK();
				}
				Grow();
				MapIndex = GetMapIndex(Key, KeyHash);
			}

			if (FreeKeyIndices.Num())
			{
				SizeType FreeIndex = FreeKeyIndices.RemoveLast();
				Map[MapIndex] = FreeIndex;
				Keys[FreeIndex] = Key;
				KeyHashes[FreeIndex] = KeyHash;
				Values1[FreeIndex] = Value1;
				Values2[FreeIndex] = Value2;
			}
			else
			{
				Map[MapIndex] = Keys.Num();
				Keys.Add(Key);
				KeyHashes.Add(KeyHash);
				Values1.Add(Value1);
				Values2.Add(Value2);
			}

			++Count;
		}
	}

	Values GetValue(const TKey& Key)
	{
		SizeType KeyHash = Key.GetHashCode();

		FScopeLock Lock(&CriticalSection);

		SizeType MapIndex = GetMapIndex(Key, KeyHash);

		SizeType KeyIndex = Map[MapIndex];
		LLMCheck(KeyIndex != InvalidIndex);

		Values RetValues;
		RetValues.Value1 = Values1[KeyIndex];
		RetValues.Value2 = Values2[KeyIndex];

		return RetValues;
	}

	Values Remove(const TKey& Key)
	{
		SizeType KeyHash = Key.GetHashCode();

		LLMCheck(Map);

		FScopeLock Lock(&CriticalSection);

		SizeType MapIndex = GetMapIndex(Key, KeyHash);
		if (!LLMEnsure(IsItemInUse(MapIndex)))
		{
			return Values();
		}

		SizeType KeyIndex = Map[MapIndex];

		Values RetValues;
		RetValues.Value1 = Values1[KeyIndex];
		RetValues.Value2 = Values2[KeyIndex];

		if (KeyIndex == Keys.Num() - 1)
		{
			Keys.RemoveLast();
			KeyHashes.RemoveLast();
			Values1.RemoveLast();
			Values2.RemoveLast();
		}
		else
		{
			FreeKeyIndices.Add(KeyIndex);
		}

		// find first index in this array
		SizeType IndexIter = MapIndex;
		SizeType FirstIndex = MapIndex;
		if (!IndexIter)
		{
			IndexIter = Capacity;
		}
		--IndexIter;
		while (IsItemInUse(IndexIter))
		{
			FirstIndex = IndexIter;
			if (!IndexIter)
			{
				IndexIter = Capacity;
			}
			--IndexIter;
		}

		bool Found = false;
		for (;;)
		{
			// find the last item in the array that can replace the item being removed
			SizeType IndexIter2 = (MapIndex + 1) & (Capacity - 1);

			SizeType SwapIndex = InvalidIndex;
			while (IsItemInUse(IndexIter2))
			{
				SizeType SearchKeyIndex = Map[IndexIter2];
				const SizeType SearchHashCode = KeyHashes[SearchKeyIndex];
				const SizeType SearchInsertIndex = SearchHashCode & (Capacity - 1);

				if (InRange(SearchInsertIndex, FirstIndex, MapIndex))
				{
					SwapIndex = IndexIter2;
					Found = true;
				}

				IndexIter2 = (IndexIter2 + 1) & (Capacity - 1);
			}

			// swap the item
			if (Found)
			{
				Map[MapIndex] = Map[SwapIndex];
				MapIndex = SwapIndex;
				Found = false;
			}
			else
			{
				break;
			}
		}

		// remove the last item
		Map[MapIndex] = InvalidIndex;

		--Count;

		return RetValues;
	}

	SizeType Num() const
	{
		FScopeLock Lock(&CriticalSection);

		return Count;
	}

	bool HasKey(const TKey& Key)
	{
		SizeType KeyHash = Key.GetHashCode();

		FScopeLock Lock(&CriticalSection);

		SizeType MapIndex = GetMapIndex(Key, KeyHash);
		return IsItemInUse(MapIndex);
	}

	void Trim()
	{
		FScopeLock Lock(&CriticalSection);

		Keys.Trim();
		KeyHashes.Trim();
		Values1.Trim();
		Values2.Trim();
		FreeKeyIndices.Trim();
	}

private:
	void Reserve(SizeType NewCapacity)
	{
		NewCapacity = GetNextPow2(NewCapacity);

		// keep a copy of the old map
		SizeType* OldMap = Map;
		SizeType OldCapacity = Capacity;

		// allocate the new table
		Capacity = NewCapacity;
		Map = (SizeType*)Allocator->Alloc(NewCapacity * sizeof(SizeType));

		for (SizeType Index = 0; Index < NewCapacity; ++Index)
			Map[Index] = InvalidIndex;

		// copy the values from the old to the new table
		SizeType* OldItem = OldMap;
		for (SizeType Index = 0; Index < OldCapacity; ++Index, ++OldItem)
		{
			SizeType KeyIndex = *OldItem;

			if (KeyIndex != InvalidIndex)
			{
				SizeType MapIndex = GetMapIndex(Keys[KeyIndex], KeyHashes[KeyIndex]);
				Map[MapIndex] = KeyIndex;
			}
		}

		Allocator->Free(OldMap, OldCapacity * sizeof(SizeType));
	}

	static SizeType GetNextPow2(SizeType value)
	{
		SizeType p = 2;
		while (p < value)
			p *= 2;
		return p;
	}

	bool IsItemInUse(SizeType MapIndex) const
	{
		return Map[MapIndex] != InvalidIndex;
	}

	SizeType GetMapIndex(const TKey& Key, SizeType Hash) const
	{
		SizeType Mask = Capacity - 1;
		SizeType MapIndex = Hash & Mask;
		SizeType KeyIndex = Map[MapIndex];

		while (KeyIndex != InvalidIndex && !(Keys[KeyIndex] == Key))
		{
			MapIndex = (MapIndex + 1) & Mask;
			KeyIndex = Map[MapIndex];
#ifdef PROFILE_LLMMAP
			++IterAcc;
#endif
		}

#ifdef PROFILE_LLMMAP
		++IterCount;
		double Average = IterAcc / (double)IterCount;
		if (Average > 2.0)
		{
			static double LastWriteTime = 0.0;
			double Now = FPlatformTime::Seconds();
			if (Now - LastWriteTime > 5)
			{
				LastWriteTime = Now;
				UE_LOG(LogStats, Log, TEXT("WARNING: LLMMap average: %f\n"), (float)Average);
			}
		}
#endif
		return MapIndex;
	}

	// Increase the capacity of the map
	void Grow()
	{
		SizeType NewCapacity = Capacity ? 2 * Capacity : DefaultCapacity;
		Reserve(NewCapacity);
	}

	static bool InRange(
		const SizeType Index,
		const SizeType StartIndex,
		const SizeType EndIndex)
	{
		return (StartIndex <= EndIndex) ?
			Index >= StartIndex && Index <= EndIndex :
			Index >= StartIndex || Index <= EndIndex;
	}

	// data
private:
	enum { DefaultCapacity = 1024 * 1024 };
	enum { InvalidIndex = -1 };
	static const SizeType Margin = (30 * 256) / 100;

	mutable FCriticalSection CriticalSection;

	FLLMAllocator* Allocator;

	SizeType* Map;
	SizeType Count;
	SizeType Capacity;

	// all these arrays must be kept in sync and are accessed by MapIndex
	FLLMArray<TKey, SizeType> Keys;
	FLLMArray<SizeType, SizeType> KeyHashes;
	FLLMArray<TValue1, SizeType> Values1;
	FLLMArray<TValue2, SizeType> Values2;

	FLLMArray<SizeType, SizeType> FreeKeyIndices;

#ifdef PROFILE_LLMMAP
	mutable int64 IterAcc;
	mutable int64 IterCount;
#endif
};

// Pointer key for hash map
struct PointerKey
{
	PointerKey() : Pointer(NULL) {}
	PointerKey(const void* InPointer) : Pointer(InPointer) {}
	LLMNumAllocsType GetHashCode() const
	{
		return GetHashCodeImpl<sizeof(LLMNumAllocsType), sizeof(void*)>();
	}
	bool operator==(const PointerKey& other) const { return Pointer == other.Pointer; }
	const void* Pointer;

private:
	template <int HashSize, int PointerSize>
	LLMNumAllocsType GetHashCodeImpl() const
	{
		static_assert(HashSize == 0 && PointerSize == 0, "Converting void* to a LLMNumAllocsType - sized hashkey is not implemented for the current sizes.");
		return (LLMNumAllocsType)(intptr_t)Pointer;
	}

	template <>
	LLMNumAllocsType GetHashCodeImpl<8,8>() const
	{
		// 64 bit pointer to 64 bit hash
		uint64 Key = (uint64)Pointer;
		Key = (~Key) + (Key << 21);
		Key = Key ^ (Key >> 24);
		Key = Key * 265;
		Key = Key ^ (Key >> 14);
		Key = Key * 21;
		Key = Key ^ (Key >> 28);
		Key = Key + (Key << 31);
		return (LLMNumAllocsType)Key;
	}

	template <>
	LLMNumAllocsType GetHashCodeImpl<4, 8>() const
	{
		// 64 bit pointer to 32 bit hash
		uint64 Key = (uint64)Pointer;
		Key = (~Key) + (Key << 21);
		Key = Key ^ (Key >> 24);
		Key = Key * 265;
		Key = Key ^ (Key >> 14);
		Key = Key * 21;
		Key = Key ^ (Key >> 28);
		Key = Key + (Key << 31);
		return (LLMNumAllocsType)Key;
	}

	template <>
	LLMNumAllocsType GetHashCodeImpl<4, 4>() const
	{
		// 32 bit pointer to 32 bit Hash
		uint64 Key = (uint64)Pointer;
		Key = (~Key) + (Key << 18);
		Key = Key ^ (Key >> 31);
		Key = Key * 21;
		Key = Key ^ (Key >> 11);
		Key = Key + (Key << 6);
		Key = Key ^ (Key >> 22);
		return (LLMNumAllocsType)Key;
	}
};

#endif		// #if ENABLE_LOW_LEVEL_MEM_TRACKER

