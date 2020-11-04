// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Algo/Rotate.h"


template <typename AttributeType>
class TAttributeArrayContainer
{
public:
	TAttributeArrayContainer() = default;

	explicit TAttributeArrayContainer(const AttributeType& InDefaultValue)
		: DefaultValue(InDefaultValue)
	{}

	/** Return size of container */
	int32 Num() const { return NumElements; }

	/** Initializes the array to the given size with the default value */
	void Initialize(const int32 ElementCount, const AttributeType& Default)
	{
		// For unbounded arrays, the default value is an empty subarray.
		check(ElementCount >= 0);
		int32 NumChunks = (ElementCount + ChunkSize - 1) >> ChunkBits;
		Chunks.Empty(NumChunks);
		Chunks.SetNum(NumChunks);
		NumElements = ElementCount;
	}

	/** Sets the number of elements, each element itself being a subarray of items of type AttributeType. */
	void SetNum(const int32 ElementCount, const AttributeType& Default)
	{
		check(ElementCount >= 0);

		int32 NumChunks = (ElementCount + ChunkSize - 1) >> ChunkBits;
		Chunks.SetNum(NumChunks);

		if (ElementCount < NumElements)
		{
			// Case where we're shrinking the unbounded array
			int32 IndexInLastChunk = ElementCount & ChunkMask;
			if (IndexInLastChunk > 0)
			{
				FChunk& LastChunk = Chunks.Last();
				int32 LastIndex = LastChunk.StartIndex[IndexInLastChunk - 1] + LastChunk.MaxCount[IndexInLastChunk - 1];
				LastChunk.Data.SetNum(LastIndex);
				for (; IndexInLastChunk < ChunkSize; ++IndexInLastChunk)
				{
					LastChunk.StartIndex[IndexInLastChunk] = LastIndex;
					LastChunk.Count[IndexInLastChunk] = 0;
					LastChunk.MaxCount[IndexInLastChunk] = 0;
				}
			}
		}
		else
		{
			// If we're growing the unbounded array, there's nothing to do;
			// the excess indices in the old last chunk will already be set up with zero length, pointing at the off-the-end element of the data array
		}

		NumElements = ElementCount;
	}

	uint32 GetHash(uint32 Crc = 0) const
	{
		for (const FChunk& Chunk : Chunks)
		{
			Crc = FCrc::MemCrc32(Chunk.Data.GetData(), Chunk.Data.Num() * sizeof(AttributeType), Crc);
		}
		return Crc;
	}

	/** Expands the array if necessary so that the passed element index is valid. Newly created elements will be assigned the default value. */
	void Insert(const int32 Index, const AttributeType& Default)
	{
		check(Index >= 0);

		int32 EndIndex = Index + 1;
		if (EndIndex > NumElements)
		{
			int32 NumChunks = (EndIndex + ChunkSize - 1) >> ChunkBits;
			Chunks.SetNum(NumChunks);
		}

		NumElements = EndIndex;
	}

	/** Fills the index with the default value */
	void SetToDefault(const int32 Index, const AttributeType& Default)
	{
		checkSlow(Index >= 0 && Index < NumElements);
		int32 ChunkIndex = Index >> ChunkBits;
		int32 ElementIndex = Index & ChunkMask;

		// Default value is an empty array. We do not compact the allocations when shrinking the data.
		Chunks[ChunkIndex].Count[ElementIndex] = 0;
	}

	/** Remaps elements according to the passed remapping table */
	void Remap(const TSparseArray<int32>& IndexRemap, const AttributeType& Default);

	friend FArchive& operator <<(FArchive& Ar, TAttributeArrayContainer<AttributeType>& Array)
	{
		Ar << Array.Chunks;
		Ar << Array.NumElements;
		Ar << Array.DefaultValue;
		return Ar;
	}

	/** Gets the attribute array at the given index as a TArrayView */
	TArrayView<AttributeType> Get(int32 Index)
	{
		checkSlow(Index >= 0 && Index < NumElements);
		int32 ChunkIndex = Index >> ChunkBits;
		int32 ElementIndex = Index & ChunkMask;
		FChunk& Chunk = Chunks[ChunkIndex];
		return TArrayView<AttributeType>(Chunk.Data.GetData() + Chunk.StartIndex[ElementIndex], Chunk.Count[ElementIndex]);
	}

	/** Gets the attribute array at the given index as a TArrayView */
	TArrayView<const AttributeType> Get(int32 Index) const
	{
		checkSlow(Index >= 0 && Index < NumElements);
		int32 ChunkIndex = Index >> ChunkBits;
		int32 ElementIndex = Index & ChunkMask;
		const FChunk& Chunk = Chunks[ChunkIndex];
		return TArrayView<const AttributeType>(Chunk.Data.GetData() + Chunk.StartIndex[ElementIndex], Chunk.Count[ElementIndex]);
	}

	/** Sets the attribute array at the given index to the given TArrayView */
	void Set(int32 Index, TArrayView<const AttributeType> Value)
	{
		TArrayView<AttributeType> Element = SetElementCount(Index, Value.Num(), false);

		for (int32 I = 0; I < Value.Num(); I++)
		{
			Element[I] = Value[I];
		}
	}

	/** Sets the given attribute array element to have the given number of subarray elements. */
	TArrayView<AttributeType> SetElementCount(int32 Index, int32 Size, bool bSetDefault = true)
	{
		checkSlow(Index >= 0 && Index < NumElements);
		int32 ChunkIndex = Index >> ChunkBits;
		int32 ElementIndex = Index & ChunkMask;
		FChunk& Chunk = Chunks[ChunkIndex];

		AttributeType* BasePtr = Chunk.Data.GetData();

		int32 Extra = Size - Chunk.MaxCount[ElementIndex];
		if (Extra > 0)
		{
			// If we are requesting that MaxCount grows for this element, we have to insert new data elements,
			// and adjust subsequent start indices accordingly.
			Chunk.Data.SetNum(Chunk.Data.Num() + Extra);

			AttributeType* StartPtr = BasePtr + Chunk.StartIndex[ElementIndex] + Extra;
			AttributeType* EndPtr = BasePtr + Chunk.Data.Num();
			FMemory::Memmove(StartPtr, StartPtr - Extra, (EndPtr - StartPtr) * sizeof(AttributeType));

			Chunk.MaxCount[ElementIndex] = Size;
			for (int32 I = ElementIndex + 1; I < ChunkSize; I++)
			{
				Chunk.StartIndex[I] += Extra;
			}
		}

		if (bSetDefault)
		{
			// Set any newly created members to the default value
			for (int32 I = Chunk.Count[ElementIndex]; I < Chunk.MaxCount[ElementIndex]; I++)
			{
				Chunk.Data[Chunk.StartIndex[ElementIndex] + I] = DefaultValue;
			}
		}

		Chunk.Count[ElementIndex] = Size;

		return TArrayView<AttributeType>(BasePtr + Chunk.StartIndex[ElementIndex], Chunk.Count[ElementIndex]);
	}

	TArrayView<AttributeType> InsertIntoElement(int32 Index, int32 SubArrayIndex, int32 InsertCount = 1)
	{
		checkSlow(Index >= 0 && Index < NumElements);
		int32 ChunkIndex = Index >> ChunkBits;
		int32 ElementIndex = Index & ChunkMask;
		FChunk& Chunk = Chunks[ChunkIndex];

		int32 CurrentCount = Chunk.Count[ElementIndex];
		checkSlow(SubArrayIndex >= 0 && SubArrayIndex <= CurrentCount);
		int32 NewCount = CurrentCount + InsertCount;

		// Resize the allocated space for this element if necessary
		TArrayView<AttributeType> Element = SetElementCount(ElementIndex, NewCount, true);

		// Insert the new subarray elements
		AttributeType* SpanStart = Element.GetData() + SubArrayIndex;
		int32 SpanSize = NewCount - SubArrayIndex;
		Algo::Rotate(TArrayView<AttributeType>(SpanStart, SpanSize), SpanSize - InsertCount);

		return Element;
	}

	TArrayView<AttributeType> RemoveFromElement(int32 ElementIndex, int32 SubArrayIndex, int32 Count = 1)
	{
		TArrayView<AttributeType> Element = Get(ElementIndex);
		for (int32 I = SubArrayIndex + Count; I < Element.Num(); I++)
		{
			Element[I - Count] = Element[I];
		}

		return SetElementCount(ElementIndex, Element.Num() - Count, false);
	}


private:

	static const int32 ChunkBits = 8;
	static const int32 ChunkSize = (1 << ChunkBits);
	static const int32 ChunkMask = ChunkSize - 1;

	struct FChunk
	{
		FChunk()
		{
			Data.Reserve(ChunkSize);
		}

		friend FArchive& operator <<(FArchive& Ar, FChunk& Chunk)
		{
			Ar << Chunk.Data;
			Ar << Chunk.StartIndex;
			Ar << Chunk.Count;
			Ar << Chunk.MaxCount;
			return Ar;
		}

		// All the data for each element in the chunk, packed contiguously
		TArray<AttributeType> Data;

		// Start, count and allocated count in the Data array for each element in the chunk.
		// Arranged as SoA for cache optimization, since the most frequent operation is
		// adding a fixed amount to all the start indices when a value is inserted.
		TStaticArray<int32, ChunkSize> StartIndex = TStaticArray<int32, ChunkSize>(0);
		TStaticArray<int32, ChunkSize> Count = TStaticArray<int32, ChunkSize>(0);
		TStaticArray<int32, ChunkSize> MaxCount = TStaticArray<int32, ChunkSize>(0);
	};

	TArray<FChunk> Chunks;
	int32 NumElements = 0;
	AttributeType DefaultValue;
};



template <typename AttributeType>
void TAttributeArrayContainer<AttributeType>::Remap(const TSparseArray<int32>& IndexRemap, const AttributeType& Default)
{
	TAttributeArrayContainer<AttributeType> NewAttributeArray(Default);

	for (typename TSparseArray<int32>::TConstIterator It(IndexRemap); It; ++It)
	{
		const int32 OldElementIndex = It.GetIndex();
		const int32 NewElementIndex = IndexRemap[OldElementIndex];

		NewAttributeArray.Insert(NewElementIndex, Default);
		NewAttributeArray.Set(NewElementIndex, Get(OldElementIndex));
	}

	*this = MoveTemp(NewAttributeArray);
}


/**
 * Proxy object which fields access to an unbounded array attribute container.
 */
template <typename AttributeType>
class TArrayAttribute
{
	template <typename T> friend class TArrayAttribute;
	using ArrayType = typename TCopyQualifiersFromTo<AttributeType, TAttributeArrayContainer<typename TRemoveCV<AttributeType>::Type>>::Type;

public:
	explicit TArrayAttribute(ArrayType& InArray, int32 InIndex)
		: Array(InArray),
		  Index(InIndex)
	{}

	/**
	 * Construct a TArrayAttribute<const T> from a TArrayAttribute<T>. 
	 */
	template <typename T = AttributeType, typename TEnableIf<TIsSame<T, const T>::Value, int>::Type = 0>
	TArrayAttribute(TArrayAttribute<typename TRemoveCV<T>::Type> InValue)
		: Array(InValue.Array),
		  Index(InValue.Index)
	{}

	/**
	 * Helper function for returning a typed pointer to the first array attribute entry.
	 */
	AttributeType* GetData() const
	{
		return Array.Get(Index).GetData();
	}

	/**
	 * Tests if index is valid, i.e. than or equal to zero, and less than the number of elements in the array attribute.
	 *
	 * @param Index Index to test.
	 *
	 * @returns True if index is valid. False otherwise.
	 */
	bool IsValidIndex(int32 ArrayAttributeIndex) const
	{
		return Array.Get(Index).IsValidIndex(ArrayAttributeIndex);
	}

	/**
	 * Returns true if the array attribute is empty and contains no elements. 
	 *
	 * @returns True if the array attribute is empty.
	 */
	bool IsEmpty() const
	{
		return Array.Get(Index).IsEmpty();
	}

	/**
	 * Returns number of elements in the array attribute.
	 *
	 * @returns Number of elements in array attribute.
	 */
	int32 Num() const
	{
		return Array.Get(Index).Num();
	}

	/**
	 * Array bracket operator. Returns reference to array attribute element at given index.
	 *
	 * @returns Reference to indexed element.
	 */
	AttributeType& operator[](int32 ArrayAttributeIndex) const
	{
		return Array.Get(Index)[ArrayAttributeIndex];
	}

	/**
	 * Returns n-th last element from the array attribute.
	 *
	 * @param IndexFromTheEnd (Optional) Index from the end of array.
	 *                        Default is 0.
	 *
	 * @returns Reference to n-th last element from the array.
	 */
	AttributeType& Last(int32 IndexFromTheEnd = 0) const
	{
		return Array.Get(Index).Last();
	}

	/**
	 * Sets the number of elements in the array attribute.
	 */
	void SetNum(int32 Num)
	{
		Array.SetElementCount(Index, Num, true);
	}

	/**
	 * Inserts a number of elements in the array attribute
	 */
	void Insert(int32 StartIndex, int32 Count)
	{
		Array.InsertIntoElement(Index, StartIndex, Count);
	}

	/**
	 * Removes a number of elements from the array attribute
	 */
	void Remove(int32 StartIndex, int32 Count)
	{
		Array.RemoveFromElement(Index, StartIndex, Count);
	}

	/**
	 * Return a TArrayView representing this array attribute.
	 */
	TArrayView<AttributeType> ToArrayView() { return Array.Get(Index); }

	/**
	 * Implicitly coerce an array attribute to a TArrayView.
	 */
	operator TArrayView<AttributeType>() { return Array.Get(Index); }

public:
	/**
	* DO NOT USE DIRECTLY
	* STL-like iterators to enable range-based for loop support.
	*/
	FORCEINLINE AttributeType* begin() const { return GetData(); }
	FORCEINLINE AttributeType* end  () const { return GetData() + Num(); }

private:
	ArrayType& Array;
	int32 Index;
};


