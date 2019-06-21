// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Templates/UnrealTypeTraits.h"
#include "Delegates/IntegerSequence.h"

/** An array with a static number of elements. */
template <typename InElementType, uint32 NumElements, uint32 Alignment = alignof(InElementType)>
class alignas(Alignment) TStaticArray
{
public:
	using ElementType = InElementType;

	TStaticArray() 
		: Storage()
	{}

	explicit TStaticArray(const InElementType& DefaultElement)
		: Storage(TMakeIntegerSequence<uint32, NumElements>(), DefaultElement)
	{}

	TStaticArray(TStaticArray&& Other) = default;
	TStaticArray(const TStaticArray& Other) = default;
	TStaticArray& operator=(TStaticArray&& Other) = default;
	TStaticArray& operator=(const TStaticArray& Other) = default;

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar,TStaticArray& StaticArray)
	{
		for(uint32 Index = 0;Index < NumElements;++Index)
		{
			Ar << StaticArray[Index];
		}
		return Ar;
	}

	// Accessors.
	FORCEINLINE_DEBUGGABLE InElementType& operator[](uint32 Index)
	{
		checkSlow(Index < NumElements);
		return Storage.Elements[Index].Element;
	}

	FORCEINLINE_DEBUGGABLE const InElementType& operator[](uint32 Index) const
	{
		checkSlow(Index < NumElements);
		return Storage.Elements[Index].Element;
	}

	// Comparisons.
	friend bool operator==(const TStaticArray& A,const TStaticArray& B)
	{
		for(uint32 ElementIndex = 0;ElementIndex < NumElements;++ElementIndex)
		{
			if(!(A[ElementIndex] == B[ElementIndex]))
			{
				return false;
			}
		}
		return true;
	}

	friend bool operator!=(const TStaticArray& A,const TStaticArray& B)
	{
		for(uint32 ElementIndex = 0;ElementIndex < NumElements;++ElementIndex)
		{
			if(!(A[ElementIndex] == B[ElementIndex]))
			{
				return true;
			}
		}
		return false;
	}

	/** The number of elements in the array. */
	FORCEINLINE_DEBUGGABLE int32 Num() const { return NumElements; }

	/** A pointer to the first element of the array */
	FORCEINLINE_DEBUGGABLE       InElementType* GetData()       { return Storage.Elements; }
	FORCEINLINE_DEBUGGABLE const InElementType* GetData() const { return Storage.Elements; }

	/** Hash function. */
	friend uint32 GetTypeHash(const TStaticArray& Array)
	{
		uint32 Result = 0;
		for(uint32 ElementIndex = 0;ElementIndex < NumElements;++ElementIndex)
		{
			Result ^= GetTypeHash(Array[ElementIndex]);
		}
		return Result;
	}

private:

	struct alignas(Alignment) TArrayStorageElementAligned
	{
		TArrayStorageElementAligned() {}
		TArrayStorageElementAligned(const InElementType& InElement)
			: Element(InElement)
		{
		}

		InElementType Element;
	};

	struct TArrayStorage
	{
		TArrayStorage()
			: Elements()
		{
		}

		template<uint32... Indices>
		TArrayStorage(TIntegerSequence<uint32, Indices...>, const InElementType& DefaultElement)
			: Elements { ((void)Indices, DefaultElement)... } //Integer Sequence pack expansion duplicates DefaultElement NumElements times and the comma operator throws away the index
		{
		}

		TArrayStorageElementAligned Elements[NumElements];
	};

	TArrayStorage Storage;
};

/** Creates a static array filled with the specified value. */
template <typename InElementType, uint32 NumElements>
TStaticArray<InElementType,NumElements> MakeUniformStaticArray(typename TCallTraits<InElementType>::ParamType InValue)
{
	TStaticArray<InElementType,NumElements> Result;
	for(uint32 ElementIndex = 0;ElementIndex < NumElements;++ElementIndex)
	{
		Result[ElementIndex] = InValue;
	}
	return Result;
}

template <typename ElementType, uint32 NumElements, uint32 Alignment>
struct TIsContiguousContainer<TStaticArray<ElementType, NumElements, Alignment>>
{
	enum { Value = true };
};
