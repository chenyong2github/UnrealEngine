// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMTraits.h"

typedef TArray<uint8> FRigVMByteArray;
typedef TArray<FRigVMByteArray> FRigVMNestedByteArray;

/**
 * The FRigVMDynamicArray is used as an array wrapping a generic TArray.
 * FRigVMDynamicArray is copied by reference.
 */
template<class T>
struct FRigVMDynamicArray
{
public:

	typedef       T* RangedForIteratorType;
	typedef const T* RangedForConstIteratorType;

	FORCEINLINE_DEBUGGABLE FRigVMDynamicArray(FRigVMByteArray& InStorage)
	: Storage(InStorage)
	{
	}

	// copy constructor
	FORCEINLINE_DEBUGGABLE FRigVMDynamicArray(const FRigVMDynamicArray& InOther)
	: Storage(InOther.Storage)
	{
	}

	// assignment operator
	FORCEINLINE_DEBUGGABLE FRigVMDynamicArray& operator= (const FRigVMDynamicArray &InOther)
	{
		Storage = InOther.Storage;
		return *this;
	}

	// returns the number of elements in this array
	FORCEINLINE_DEBUGGABLE int32 Num() const
	{
		return Storage.Num() / sizeof(T);
	}

	// returns true if a given Index is valid
	FORCEINLINE_DEBUGGABLE bool IsValidIndex(int32 InIndex) const
	{
		return Storage.IsValidIndex(InIndex * sizeof(T));
	}

	// empties the contents of the array
	FORCEINLINE_DEBUGGABLE void Reset()
	{
		if (Num() > 0)
		{
			RigVMDestroy<T>(GetData(), Num());
		}
		Storage.Reset();
	}

	// adds an element to the array
	FORCEINLINE_DEBUGGABLE int32 Add(const T& InValue)
	{
		int32 ElementIndex = Num();
		int32 ByteIndex = Storage.Num();
		Storage.SetNum(ByteIndex + sizeof(T));
		T* Data = (T*)&Storage[ByteIndex];
		*Data = InValue;
		return ElementIndex;
	}

	// appends an array to this storage and returns the first index.
	FORCEINLINE_DEBUGGABLE int32 Append(const FRigVMDynamicArray& InOther)
	{
		if(InOther.Num() == 0)
		{
			return INDEX_NONE;
		}

		int32 FirstIndex = Num();
		for(const T& Element : InOther)
		{
			Add(Element);
		}
		return FirstIndex;
	}

	// appends an array to this storage and returns the first index.
	FORCEINLINE_DEBUGGABLE int32 Append(const TArray<T>& InOther)
	{
		if(InOther.Num() == 0)
		{
			return INDEX_NONE;
		}

		int32 FirstIndex = Num();
		for(const T& Element : InOther)
		{
			Add(Element);
		}
		return FirstIndex;
	}

	// sets the number of elements in this array
	FORCEINLINE_DEBUGGABLE void SetNumUninitialized(int32 InSize)
	{
		if (InSize == 0)
		{
			Reset();
			return;
		}

		int32 LastSize = Num();
		if (LastSize == InSize)
		{
			return;
		}

		if (LastSize > InSize)
		{
			int32 LastByte = LastSize * sizeof(T);
			RigVMDestroy<T>(GetData() + LastSize, LastSize - InSize);
		}
		Storage.SetNumUninitialized(InSize * sizeof(T), false /* no shrinking */);
	}

	// sets the number of elements in this array
	FORCEINLINE_DEBUGGABLE void SetNumZeroed(int32 InSize)
	{
		int32 LastSize = Num();
		if (LastSize == InSize)
		{
			return;
		}

		SetNumUninitialized(InSize);

		if (LastSize < InSize)
		{
			int32 NumBytes = (InSize - LastSize) * sizeof(T);
			FMemory::Memzero(GetData() + LastSize, NumBytes);
		}
	}

	// sets the number of elements in this array
	FORCEINLINE_DEBUGGABLE void SetNum(int32 InSize)
	{
		int32 LastSize = Num();
		if (LastSize == InSize)
		{
			return;
		}

		SetNumUninitialized(InSize);

		if (LastSize < InSize)
		{
			RigVMInitialize<T>(GetData() + LastSize, InSize - LastSize);
		}
	}

	// sets the number of elements in this array
	FORCEINLINE_DEBUGGABLE void EnsureMinimumSize(int32 InSize)
	{
		int32 LastSize = Num();
		if (LastSize >= InSize)
		{
			return;
		}

		SetNumUninitialized(InSize);

		if (LastSize < InSize)
		{
			RigVMInitialize<T>(GetData() + LastSize, InSize - LastSize);
		}
	}

	// copies the contents of one array to this one
	FORCEINLINE_DEBUGGABLE void CopyFrom(const FRigVMDynamicArray<T>& InOther)
	{
		Reset();
		SetNumZeroed(InOther.Num());
		if (Num() > 0)
		{
			RigVMCopy<T>(GetData(), InOther.GetData(), Num());
		}
	}

	// copies the contents of one array to this one
	FORCEINLINE_DEBUGGABLE void CopyFrom(const TArray<T>& InOther)
	{
		Reset();
		SetNumZeroed(InOther.Num());
		if (Num() > 0)
		{
			RigVMCopy<T>(GetData(), InOther.GetData(), Num());
		}
	}

	FORCEINLINE_DEBUGGABLE void CopyTo(FRigVMDynamicArray<T>& InOther) const
	{
		InOther.Reset();
		InOther.SetNumZeroed(Num());
		if (Num() > 0)
		{
			RigVMCopy<T>(InOther.GetData(), GetData(), Num());
		}
	}

	// copies the contents of one array to this one
	FORCEINLINE_DEBUGGABLE void CopyTo(TArray<T>& InOther) const
	{
		InOther.Reset();
		InOther.SetNumZeroed(Num());
		if (Num() > 0)
		{
			RigVMCopy<T>(InOther.GetData(), GetData(), Num());
		}
	}

	FORCEINLINE_DEBUGGABLE const T& operator[](int32 InIndex) const
	{
		return *(const T*)&Storage[InIndex * sizeof(T)];
	}

	FORCEINLINE_DEBUGGABLE T& operator[](int32 InIndex)
	{
		return *(T*)&Storage[InIndex * sizeof(T)];
	}

	FORCEINLINE_DEBUGGABLE bool operator ==(const FRigVMDynamicArray& Other) const
	{
		return Storage == Other.Storage;
	}
	
	FORCEINLINE_DEBUGGABLE bool operator !=(const FRigVMDynamicArray& Other) const
	{
		return Storage != Other.Storage;
	}

	FORCEINLINE_DEBUGGABLE const T* GetData() const
	{
		return (const T*)Storage.GetData();
	}

	FORCEINLINE_DEBUGGABLE T* GetData()
	{
		return (T*)Storage.GetData();
	}

	FORCEINLINE_DEBUGGABLE int32 Find(const T& InItem) const
	{
		for (int32 Index = 0; Index < Num(); Index++)
		{
			if (InItem == operator[](Index))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FORCEINLINE_DEBUGGABLE bool Contains(const T& InItem) const
	{
		for (const T& Item : *this)
		{
			if (Item == InItem)
			{
				return true;
			}
		}
		return false;
	}

	FORCEINLINE_DEBUGGABLE RangedForIteratorType      begin()       { return GetData(); }
	FORCEINLINE_DEBUGGABLE RangedForConstIteratorType begin() const { return GetData(); }
	FORCEINLINE_DEBUGGABLE RangedForIteratorType      end()         { return GetData() + Num(); }
	FORCEINLINE_DEBUGGABLE RangedForConstIteratorType end() const   { return GetData() + Num(); }

	FORCEINLINE_DEBUGGABLE operator TArray<T>() const
	{
		TArray<T> Result;
		Result.Reserve(Num());
		for(const T& Element : *this)
		{
			Result.Add(Element);
		}
		return Result;
	}

private:

	FRigVMByteArray& Storage;
};

/**
 * The FRigVMFixedArray is used as an alternative to TArrayView
 * FRigVMFixedArray is copied by reference.
 */
template<class T>
struct FRigVMFixedArray
{
public:

	typedef       T* RangedForIteratorType;
	typedef const T* RangedForConstIteratorType;

	// default constructor
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray()
	: Data(nullptr)
	, Size(0)
	{
	}

	// constructor from a typed TArray
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray(const TArray<T>& InStorage)
	: Data((T*)InStorage.GetData())
	, Size(InStorage.Num())
	{
	}

	// constructor from a typed TArray
	template<uint32 NumInlineElements>
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray(const TArray<T, TFixedAllocator<NumInlineElements>>& InStorage)
		: Data((T*)InStorage.GetData())
		, Size(NumInlineElements)
	{
	}

	// constructor from direct memory
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray(T* InData, int32 InSize)
	: Data(InData)
	, Size(InSize)
	{
	}

	// constructor from an dynamic array
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray(FRigVMDynamicArray<T> InDynamicArray)
	: Data(InDynamicArray.GetData())
	, Size(InDynamicArray.Num())
	{
	}

	// copy constructor
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray(const FRigVMFixedArray& InOther)
	: Data(InOther.Data)
	, Size(InOther.Size)
	{
	}

	// assignment operator
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray& operator= (const FRigVMFixedArray &InOther)
	{
		Data = InOther.Data;
		Size = InOther.Size;
		return *this;
	}

	// returns the number of elements in this array
	FORCEINLINE_DEBUGGABLE int32 Num() const
	{
		return Size;
	}

	// returns true if a given Index is valid
	FORCEINLINE_DEBUGGABLE bool IsValidIndex(int32 InIndex) const
	{
		return InIndex > INDEX_NONE && InIndex < Size;
	}

	FORCEINLINE_DEBUGGABLE const T& operator[](int32 InIndex) const
	{
		return Data[InIndex];
	}

	FORCEINLINE_DEBUGGABLE T& operator[](int32 InIndex)
	{
		return Data[InIndex];
	}

	FORCEINLINE_DEBUGGABLE bool operator ==(const FRigVMDynamicArray<T>& Other) const
	{
		return Data == Other.Data;
	}
	
	FORCEINLINE_DEBUGGABLE bool operator !=(const FRigVMDynamicArray<T>& Other) const
	{
		return Data != Other.Data;
	}

	FORCEINLINE_DEBUGGABLE const T* GetData() const
	{
		return Data;
	}

	FORCEINLINE_DEBUGGABLE T* GetData()
	{
		return Data;
	}

	FORCEINLINE_DEBUGGABLE const FRigVMFixedArray Slice(int32 StartIndex, int32 Count) const
	{
		return FRigVMFixedArray((T*)GetData() + StartIndex, Count);
	}

	FORCEINLINE_DEBUGGABLE FRigVMFixedArray Slice(int32 StartIndex, int32 Count)
	{
		return FRigVMFixedArray(GetData() + StartIndex, Count);
	}

	FORCEINLINE_DEBUGGABLE int32 Find(const T& InItem) const
	{
		for (int32 Index = 0; Index < Num(); Index++)
		{
			if (InItem == operator[](Index))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FORCEINLINE_DEBUGGABLE bool Contains(const T& InItem) const
	{
		for (const T& Item : *this)
		{
			if (Item == InItem)
			{
				return true;
			}
		}
		return false;
	}

	FORCEINLINE_DEBUGGABLE RangedForIteratorType      begin()       { return GetData(); }
	FORCEINLINE_DEBUGGABLE RangedForConstIteratorType begin() const { return GetData(); }
	FORCEINLINE_DEBUGGABLE RangedForIteratorType      end()         { return GetData() + Num(); }
	FORCEINLINE_DEBUGGABLE RangedForConstIteratorType end() const   { return GetData() + Num(); }

	FORCEINLINE_DEBUGGABLE operator TArray<T>() const
	{
		TArray<T> Result;
		Result.Reserve(Num());
		for(const T& Element : *this)
		{
			Result.Add(Element);
		}
		return Result;
	}

private:

	T* Data;
	int32 Size;
};

