// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Containers/Array.h"
#include "GeometryCollection/GeometryCollectionSection.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"
#include "Chaos/ChaosArchive.h"
#include "UObject/DestructionObjectVersion.h"

class FManagedArrayCollection;
DEFINE_LOG_CATEGORY_STATIC(UManagedArrayLogging, NoLogging, All);

template <typename T>
void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<T>& Array)
{
	Ar << Array;
}

//Note: see TArray::BulkSerialize for requirements
inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<FVector>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<FGuid>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<FIntVector>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<FVector2D>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<float>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<FQuat>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<bool>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<int32>& Array)
{
	Array.BulkSerialize(Ar);
}

inline void TryBulkSerializeManagedArray(Chaos::FChaosArchive& Ar, TArray<uint8>& Array)
{
	Array.BulkSerialize(Ar);
}

/***
*  Managed Array Base
*
*  The ManagedArrayBase allows a common base class for the
*  the template class ManagedArray<T>. (see ManagedArray)
*
*/
class FManagedArrayBase : public FNoncopyable
{
	friend FManagedArrayCollection;
protected:
	/**
	* Protected access to array resizing. Only the managers of the Array
	* are allowed to perform a resize. (see friend list above).
	*/
	virtual void Resize(const int32 Num) {};

	/**
	* Protected access to array reservation. Only the managers of the Array
	* are allowed to perform a reserve. (see friend list above).
	*/
	virtual void Reserve(const int32 Num) {};

	/**
	 * Reorder elements given a new ordering. Sizes must match
	 */
	virtual void Reorder(const TArray<int32>& NewOrder) = 0;

	/** 
	* Reindex given a lookup table
	*/
	//todo: this should really assert, but material is currently relying on both faces and vertices
	virtual void ReindexFromLookup(const TArray<int32>& NewOrder) { }

	/**
	* Init from a predefined Array
	*/
	virtual void Init(const FManagedArrayBase& ) {};

public:

	virtual ~FManagedArrayBase() {}

	
	//todo(ocohen): these should all be private with friend access to managed collection
	
	/** Perform a memory move between the two arrays */
	virtual void ExchangeArrays(FManagedArrayBase& Src) = 0;

	/** Remove elements */
	virtual void RemoveElements(const TArray<int32>& SortedDeletionList)
	{
		check(false);
	}

	/** Return unmanaged copy of array with input indices. */
	virtual FManagedArrayBase * NewCopy(const TArray<int32> & DeletionList)
	{
		check(false);
		return nullptr;
	}

	/** The length of the array.*/
	virtual int32 Num() const 
	{
		return 0; 
	};

	/** The reserved length of the array.*/
	virtual int32 Max() const
	{
		return 0;
	};

	/** Serialization */
	virtual void Serialize(Chaos::FChaosArchive& Ar) 
	{
		check(false);
	}

	/** TypeSize */
	virtual size_t GetTypeSize() const
	{
		return 0;
	}

	/**
	* Reindex - Adjust index dependent elements.  
	*   Offsets is the size of the dependent group;
	*   Final is post resize of dependent group used for bounds checking on remapped indices.
	*/
	virtual void Reindex(const TArray<int32> & Offsets, const int32 & FinalSize, const TArray<int32> & SortedDeletionList) { }

#if 0 //not needed until per instance serialization
	/** Swap elements*/
	virtual void Swap(int32 Index1, int32 Index2) = 0;
#endif

};

template <typename T>
class TManagedArrayBase;

template <typename T>
void InitHelper(TArray<T>& Array, const TManagedArrayBase<T>& NewTypedArray, int32 Size);
template <typename T>
void InitHelper(TArray<TUniquePtr<T>>& Array, const TManagedArrayBase<TUniquePtr<T>>& NewTypedArray, int32 Size);

/***
*  Managed Array
*
*  Restricts clients ability to resize the array external to the containing manager. 
*/
template<class InElementType>
class TManagedArrayBase : public FManagedArrayBase
{

public:

	using ElementType = InElementType;

	/**
	* Constructor (default) Build an empty shared array
	*
	*/	
	FORCEINLINE TManagedArrayBase()
	{}

	/**
	* Constructor (TArray)
	*
	*/
	FORCEINLINE TManagedArrayBase(const TArray<ElementType>& Other)
		: Array(Other)
	{}

	/**
	* Copy Constructor (default)
	*/
	FORCEINLINE TManagedArrayBase(const TManagedArrayBase<ElementType>& Other) = delete;

	/**
	* Move Constructor
	*/
	FORCEINLINE TManagedArrayBase(TManagedArrayBase<ElementType>&& Other)
		: Array(MoveTemp(Other.Array))
	{}

	/**
	* Assignment operator
	*/
	FORCEINLINE TManagedArrayBase& operator=(TManagedArrayBase<ElementType>&& Other)
	{
		Array = MoveTemp(Other.Array);
		return *this;
	}



	/**
	* Virtual Destructor 
	*
	*/
	virtual ~TManagedArrayBase()
	{}


	virtual void RemoveElements(const TArray<int32>& SortedDeletionList) override
	{
		if (SortedDeletionList.Num() == 0)
		{
			return;
		}

		int32 RangeStart = SortedDeletionList.Last();
		for (int32 ii = SortedDeletionList.Num()-1 ; ii > -1 ; --ii)
		{
			if (ii == 0)
			{
				Array.RemoveAt(SortedDeletionList[0], RangeStart - SortedDeletionList[0] + 1, false);

			}
			else if (SortedDeletionList[ii] != (SortedDeletionList[ii - 1]+1)) // compare this and previous values to make sure the difference is only 1.
			{
				Array.RemoveAt(SortedDeletionList[ii], RangeStart - SortedDeletionList[ii] + 1, false);
				RangeStart = SortedDeletionList[ii-1];
			}
		}

 		Array.Shrink();
	}


	/**
	* Init from a predefined Array of matching type
	*/
	virtual void Init(const FManagedArrayBase& NewArray) override
	{
		ensureMsgf(NewArray.GetTypeSize() == GetTypeSize(),TEXT("TManagedArrayBase<T>::Init : Invalid array types."));
		const TManagedArrayBase<ElementType> & NewTypedArray = static_cast< const TManagedArrayBase<ElementType>& >(NewArray);
		int32 Size = NewTypedArray.Num();

		Resize(Size);
		InitHelper(Array, NewTypedArray, Size);
	};

#if 0
	virtual void Swap(int32 Index1, int32 Index2) override
	{
		Exchange(Array[Index1], Array[Index2]);
	}
#endif

	virtual void ExchangeArrays(FManagedArrayBase& NewArray) override
	{
		//It's up to the caller to make sure that the two arrays are of the same type
		ensureMsgf(NewArray.GetTypeSize() == GetTypeSize(), TEXT("TManagedArrayBase<T>::Exchange : Invalid array types."));
		TManagedArrayBase<ElementType>& NewTypedArray = static_cast<TManagedArrayBase<ElementType>& >(NewArray);

		Exchange(*this, NewTypedArray);
	};

	/**
	* Returning a reference to the element at index.
	*
	* @returns Array element reference
	*/
	FORCEINLINE ElementType & operator[](int Index)
	{
		// @todo : optimization
		// TArray->operator(Index) will perform checks against the 
		// the array. It might be worth implementing the memory
		// management directly on the ManagedArray, to avoid the
		// overhead of the TArray.
		return Array[Index];
	}
	FORCEINLINE const ElementType & operator[](int Index) const
	{
		return Array[Index];
	}

	/**
	* Helper function for returning a typed pointer to the first array entry.
	*
	* @returns Pointer to first array entry or nullptr if ArrayMax == 0.
	*/
	FORCEINLINE ElementType* GetData()
	{
		return &Array.operator[](0);
	}

	/**
	* Helper function for returning a typed pointer to the first array entry.
	*
	* @returns Pointer to first array entry or nullptr if ArrayMax == 0.
	*/
	FORCEINLINE const ElementType * GetData() const
	{
		return &Array.operator[](0);
	}

	/**
	* Helper function returning the size of the inner type.
	*
	* @returns Size in bytes of array type.
	*/
	FORCEINLINE size_t GetTypeSize() const override
	{
		return sizeof(ElementType);
	}

	/**
	* Returning the size of the array
	*
	* @returns Array size
	*/
	FORCEINLINE int32 Num() const override
	{
		return Array.Num();
	}

	FORCEINLINE int32 Max() const override
	{
		return Array.Max();
	}

	FORCEINLINE bool Contains(const ElementType& Item) const
	{
		return Array.Contains(Item);
	}

	/**
	* Find first index of the element
	*/
	int32 Find(const ElementType& Item) const
	{
		return Array.Find(Item);
	}


	/**
	* Checks if index is in array range.
	*
	* @param Index Index to check.
	*/
	FORCEINLINE void RangeCheck(int32 Index) const
	{
		checkf((Index >= 0) & (Index < Array.Num()), TEXT("Array index out of bounds: %i from an array of size %i"), Index, Array.Num());
	}

	/**
	* Serialization Support
	*
	* @param Chaos::FChaosArchive& Ar
	*/
	virtual void Serialize(Chaos::FChaosArchive& Ar)
	{		
		Ar.UsingCustomVersion(FDestructionObjectVersion::GUID);
		int Version = 1;
		Ar << Version;
	
		if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::BulkSerializeArrays)
		{
			Ar << Array;
		}
		else
		{
			TryBulkSerializeManagedArray(Ar, Array);
		}
	}

	// @todo Add RangedFor support. 


	// TARRAY_RANGED_FOR_CHECKS Is defined in Array.h based on build state.
#if TARRAY_RANGED_FOR_CHECKS
	// @todo: What is the appropriate size type?
	typedef TCheckedPointerIterator<      ElementType, int32> RangedForIteratorType;
	typedef TCheckedPointerIterator<const ElementType, int32> RangedForConstIteratorType;
#else
	typedef       ElementType* RangedForIteratorType;
	typedef const ElementType* RangedForConstIteratorType;
#endif

private:

	/**
	* DO NOT USE DIRECTLY
	* STL-like iterators to enable range-based for loop support.
	*/
#if TARRAY_RANGED_FOR_CHECKS
	FORCEINLINE friend RangedForIteratorType      begin(      TManagedArrayBase& ManagedArray) { return RangedForIteratorType     (ManagedArray.Num(), ManagedArray.GetData()); }
	FORCEINLINE friend RangedForConstIteratorType begin(const TManagedArrayBase& ManagedArray) { return RangedForConstIteratorType(ManagedArray.Num(), ManagedArray.GetData()); }
	FORCEINLINE friend RangedForIteratorType      end  (      TManagedArrayBase& ManagedArray) { return RangedForIteratorType     (ManagedArray.Num(), ManagedArray.GetData() + ManagedArray.Num()); }
	FORCEINLINE friend RangedForConstIteratorType end  (const TManagedArrayBase& ManagedArray) { return RangedForConstIteratorType(ManagedArray.Num(), ManagedArray.GetData() + ManagedArray.Num()); }
#else
	FORCEINLINE friend RangedForIteratorType      begin(      TManagedArrayBase& ManagedArray) { return ManagedArray.GetData(); }
	FORCEINLINE friend RangedForConstIteratorType begin(const TManagedArrayBase& ManagedArray) { return ManagedArray.GetData(); }
	FORCEINLINE friend RangedForIteratorType      end  (      TManagedArrayBase& ManagedArray) { return ManagedArray.GetData() + ManagedArray.Num(); }
	FORCEINLINE friend RangedForConstIteratorType end  (const TManagedArrayBase& ManagedArray) { return ManagedArray.GetData() + ManagedArray.Num(); }
#endif

	/**
	* Protected Resize to prevent external resizing of the array
	*
	* @param New array size.
	*/
	void Resize(const int32 Size) 
	{ 
		Array.SetNum(Size,true);
	}

	/**
	* Protected Reserve to prevent external reservation of the array
	*
	* @param New array reservation size.
	*/
	void Reserve(const int32 Size)
	{
		Array.Reserve(Size);
	}

	void Reorder(const TArray<int32>& NewOrder) override
	{
		const int32 NumElements = Num();
		check(NewOrder.Num() == NumElements);
		TArray<InElementType> NewArray;
		NewArray.AddDefaulted(NumElements);
		for (int32 OriginalIdx = 0; OriginalIdx < NumElements; ++OriginalIdx)
		{
			NewArray[OriginalIdx] = MoveTemp(Array[NewOrder[OriginalIdx]]);
		}
		Exchange(Array, NewArray);
	}

	TArray<InElementType> Array;

};

template <typename T>
void InitHelper(TArray<T>& Array, const TManagedArrayBase<T>& NewTypedArray, int32 Size)
{
	for (int32 Index = 0; Index < Size; Index++)
	{
		Array[Index] = NewTypedArray[Index];
	}
}

template <typename T>
void InitHelper(TArray<TUniquePtr<T>>& Array, const TManagedArrayBase<TUniquePtr<T>>& NewTypedArray, int32 Size)
{
	check(false);	//Cannot make copies of a managed array with unique pointers. Typically used for shared data
}


template<class InElementType>
class TManagedArray : public TManagedArrayBase<InElementType>
{
public:
	FORCEINLINE TManagedArray()
	{}

	FORCEINLINE TManagedArray(const TArray<InElementType>& Other)
		: TManagedArrayBase<InElementType>(Other)
	{}

	FORCEINLINE TManagedArray(TManagedArray<InElementType>&& Other)
		: TManagedArrayBase<InElementType>(MoveTemp(Other))
	{}

	FORCEINLINE TManagedArray& operator=(TManagedArray<InElementType>&& Other)
	{
		TManagedArrayBase<InElementType>::operator=(MoveTemp(Other));
		return *this;
	}

	FORCEINLINE TManagedArray(const TManagedArray<InElementType>& Other) = delete;

	virtual ~TManagedArray()
	{}
};

template<>
class TManagedArray<int32> : public TManagedArrayBase<int32>
{
public:
    using TManagedArrayBase<int32>::Num;

	FORCEINLINE TManagedArray()
	{}

	FORCEINLINE TManagedArray(const TArray<int32>& Other)
		: TManagedArrayBase<int32>(Other)
	{}

	FORCEINLINE TManagedArray(const TManagedArray<int32>& Other) = delete;
	FORCEINLINE TManagedArray(TManagedArray<int32>&& Other) = default;
	FORCEINLINE TManagedArray& operator=(TManagedArray<int32>&& Other) = default;

	virtual ~TManagedArray()
	{}

	virtual void Reindex(const TArray<int32> & Offsets, const int32 & FinalSize, const TArray<int32> & SortedDeletionList) override
	{
		UE_LOG(UManagedArrayLogging, Log, TEXT("TManagedArray<int32>[%p]::Reindex()"),this);
	
		int32 ArraySize = Num(), MaskSize = Offsets.Num();
		for (int32 Index = 0; Index < ArraySize; Index++)
		{
			int32 RemapVal = this->operator[](Index);
			if (0 <= RemapVal)
			{
				ensure(RemapVal < MaskSize);
				this->operator[](Index) -= Offsets[RemapVal];
				ensure(-1 <= this->operator[](Index) && this->operator[](Index) < FinalSize);
			}
		}
	}

	virtual void ReindexFromLookup(const TArray<int32>& NewOrder) override
	{
		const int32 ArraySize = Num();
		for (int32 Index = 0; Index < ArraySize; ++Index)
		{
			int32& Mapping = this->operator[](Index);
			if (Mapping >= 0)
			{
				Mapping = NewOrder[Mapping];
			}
		}
	}
};


template<>
class TManagedArray<TSet<int32>> : public TManagedArrayBase<TSet<int32>>
{
public:
	using TManagedArrayBase<TSet<int32>>::Num;

	FORCEINLINE TManagedArray()
	{}

	FORCEINLINE TManagedArray(const TArray<TSet<int32>>& Other)
		: TManagedArrayBase< TSet<int32> >(Other)
	{}

	FORCEINLINE TManagedArray(const TManagedArray<TSet<int32>>& Other) = delete;
	FORCEINLINE TManagedArray(TManagedArray<TSet<int32>>&& Other) = default;
	FORCEINLINE TManagedArray& operator=(TManagedArray<TSet<int32>>&& Other) = default;

	virtual ~TManagedArray()
	{}
	
	virtual void Reindex(const TArray<int32> & Offsets, const int32 & FinalSize, const TArray<int32> & SortedDeletionList) override
	{
		UE_LOG(UManagedArrayLogging, Log, TEXT("TManagedArray<TArray<int32>>[%p]::Reindex()"), this);
		
		int32 ArraySize = Num(), MaskSize = Offsets.Num();
		TSet<int32> SortedDeletionSet(SortedDeletionList);

		for (int32 Index = 0; Index < ArraySize; Index++)
		{
			TSet<int32>& NewSet = this->operator[](Index);
			
			for (int32 Del : SortedDeletionList)
			{
				NewSet.Remove(Del);
			}

			TSet<int32> OldSet = this->operator[](Index);	//need a copy since we're modifying the entries in the set (can't edit in place because value desyncs from hash)
			NewSet.Reset();	//maybe we should remove 

			for (int32 StaleEntry : OldSet)
			{
				const int32 NewEntry = StaleEntry - Offsets[StaleEntry];
				NewSet.Add(NewEntry);
			}
		}
	}

	virtual void ReindexFromLookup(const TArray<int32> & NewOrder) override
	{

		int32 ArraySize = Num();
		
		for (int32 Index = 0; Index < ArraySize; Index++)
		{
			TSet<int32>& NewSet = this->operator[](Index);
			TSet<int32> OldSet = this->operator[](Index);	//need a copy since we're modifying the entries in the set
			NewSet.Reset();	//maybe we should remove 

			for (int32 StaleEntry : OldSet)
			{
				const int32 NewEntry = StaleEntry >= 0 ? NewOrder[StaleEntry] : StaleEntry;	//only remap if valid
				NewSet.Add(NewEntry);
			}
		}
	}
};

template<>
class TManagedArray<FIntVector> : public TManagedArrayBase<FIntVector>
{
public:
    using TManagedArrayBase<FIntVector>::Num;

	FORCEINLINE TManagedArray()
	{}

	FORCEINLINE TManagedArray(const TArray<FIntVector>& Other)
		: TManagedArrayBase<FIntVector>(Other)
	{}

	FORCEINLINE TManagedArray(const TManagedArray<FIntVector>& Other) = delete;
	FORCEINLINE TManagedArray(TManagedArray<FIntVector>&& Other) = default;
	FORCEINLINE TManagedArray& operator=(TManagedArray<FIntVector>&& Other) = default;

	virtual ~TManagedArray()
	{}

	virtual void Reindex(const TArray<int32> & Offsets, const int32 & FinalSize, const TArray<int32> & SortedDeletionList) override
	{
		UE_LOG(UManagedArrayLogging, Log, TEXT("TManagedArray<FIntVector>[%p]::Reindex()"), this);
		int32 ArraySize = Num(), MaskSize = Offsets.Num();
		for (int32 Index = 0; Index < ArraySize; Index++)
		{
			const FIntVector & RemapVal = this->operator[](Index);
			for (int i = 0; i < 3; i++)
			{
				if (0 <= RemapVal[i])
				{
					ensure(RemapVal[i] < MaskSize);
					this->operator[](Index)[i] -= Offsets[RemapVal[i]];
					ensure(-1 <= this->operator[](Index)[i] && this->operator[](Index)[i] <= FinalSize);
				}
			}
		}
	}

	virtual void ReindexFromLookup(const TArray<int32> & NewOrder) override
	{
		int32 ArraySize = Num();
		for (int32 Index = 0; Index < ArraySize; Index++)
		{
			FIntVector& RemapVal = this->operator[](Index);
			for (int i = 0; i < 3; i++)
			{
				if (RemapVal[i] >= 0)
				{
					RemapVal[i] = NewOrder[RemapVal[i]];
				}
			}
		}
	}
};

