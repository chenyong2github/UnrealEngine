// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MultiplexStorage.generated.h"

USTRUCT()
struct ANIMATIONCORE_API FMultiplexAddress
{
	GENERATED_USTRUCT_BODY()

	FMultiplexAddress()
		: Pointer(nullptr)
		, ByteIndex(INDEX_NONE)
		, ElementSize(0)
		, ElementCount(0)
		, Name(NAME_None)
	{
	}

	void* Pointer;

	UPROPERTY()
	int32 ByteIndex;

	UPROPERTY()
	int32 ElementSize;

	UPROPERTY()
	int32 ElementCount;

	UPROPERTY()
		FName Name;

	FORCEINLINE bool IsArray() const { return ElementCount > 1; }
	FORCEINLINE int32 NumBytes() const { return ElementCount * ElementSize;  }

	template<class T>
	FORCEINLINE const T* Get() const
	{
		ensure(sizeof(T) == ElementSize);
		ensure(ElementCount > 0);
		return (const T*)Pointer;
	}

	template<class T>
	FORCEINLINE const T& GetRef() const
	{
		return *Get<T>();
	}

	template<class T>
	FORCEINLINE T* Get()
	{
		ensure(sizeof(T) == ElementSize);
		ensure(ElementCount > 0);
		return (T*)Pointer;
	}

	template<class T>
	FORCEINLINE T& GetRef()
	{
		return *Get<T>();
	}

	template<class T>
	FORCEINLINE TArrayView<T> GetArray()
	{
		ensure(sizeof(T) == ElementSize);
		ensure(ElementCount > 0);
		return TArrayView<T>((T*)Pointer, ElementCount);
	}
};

USTRUCT()
struct ANIMATIONCORE_API FMultiplexStorage
{
	GENERATED_USTRUCT_BODY()
	
public:

	FMultiplexStorage(bool bInUseNames = true);
	~FMultiplexStorage();

	FMultiplexStorage& operator= (const FMultiplexStorage &InOther);

	FORCEINLINE bool SupportsNames() const { return bUseNameMap;  }
	FORCEINLINE int32 Num() const { return Addresses.Num(); }
	FORCEINLINE const FMultiplexAddress& operator[](int32 InIndex) const { return Addresses[InIndex]; }
	FORCEINLINE FMultiplexAddress& operator[](int32 InIndex) { return Addresses[InIndex]; }
	FORCEINLINE const FMultiplexAddress& operator[](const FName& InName) const { return Addresses[GetIndex(InName)]; }
	FORCEINLINE FMultiplexAddress& operator[](const FName& InName) { return Addresses[GetIndex(InName)]; }

	FORCEINLINE TArray<FMultiplexAddress>::RangedForIteratorType      begin() { return Addresses.begin(); }
	FORCEINLINE TArray<FMultiplexAddress>::RangedForConstIteratorType begin() const { return Addresses.begin(); }
	FORCEINLINE TArray<FMultiplexAddress>::RangedForIteratorType      end() { return Addresses.end(); }
	FORCEINLINE TArray<FMultiplexAddress>::RangedForConstIteratorType end() const { return Addresses.end(); }

	template<class T>
	FORCEINLINE const T* Get(int32 InAddressIndex) const
	{
		const FMultiplexAddress& Address = Addresses[InAddressIndex];
		ensure(sizeof(T) == Address.ElementSize);
		ensure(Address.ElementCount > 0);
		return (const T*)&Data[Address.ByteIndex];
	}

	template<class T>
	FORCEINLINE const T& GetRef(int32 InAddressIndex) const
	{
		return *Get<T>(InAddressIndex);
	}

	template<class T>
	FORCEINLINE T* Get(int32 InAddressIndex)
	{
		const FMultiplexAddress& Address = Addresses[InAddressIndex];
		ensure(sizeof(T) == Address.ElementSize);
		ensure(Address.ElementCount > 0);
		return (T*)&Data[Address.ByteIndex];
	}

	template<class T>
	FORCEINLINE T& GetRef(int32 InAddressIndex)
	{
		return *Get<T>(InAddressIndex);
	}

	template<class T>
	FORCEINLINE TArrayView<T> GetArray(int32 InAddressIndex)
	{
		const FMultiplexAddress& Address = Addresses[InAddressIndex];
		ensure(sizeof(T) == Address.ElementSize);
		ensure(Address.ElementCount > 0);
		return TArrayView<T>((T*)&Data[Address.ByteIndex], Address.ElementCount);
	}

	bool Copy(
		int32 InSourceAddressIndex,
		int32 InTargetAddressIndex,
		int32 InSourceByteOffset = INDEX_NONE,
		int32 InTargetByteOffset = INDEX_NONE,
		int32 InNumBytes = INDEX_NONE);

	bool Copy(
		const FName& InSourceName,
		const FName& InTargetName,
		int32 InSourceByteOffset = INDEX_NONE,
		int32 InTargetByteOffset = INDEX_NONE,
		int32 InNumBytes = INDEX_NONE);

	FORCEINLINE int32 GetIndex(const FName& InName) const
	{
		if (!bUseNameMap)
		{
			return INDEX_NONE;
		}

		if (NameMap.Num() != Addresses.Num())
		{
			for (int32 Index = 0; Index < Addresses.Num(); Index++)
			{
				if (Addresses[Index].Name == InName)
				{
					return Index;
				}
			}
		}
		else
		{
			const int32* Index = NameMap.Find(InName);
			if (Index != nullptr)
			{
				return *Index;
			}
		}

		return INDEX_NONE;
	}

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const\
	{
		if (!bUseNameMap)
		{
			return false;
		}
		return GetIndex(InPotentialNewName) == INDEX_NONE;
	}

	void Reset();

	int32 Add(int32 InElementSize, int32 InCount, const void* InData = nullptr);

	int32 Add(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InData = nullptr);

	template<class T>
	int32 Add(const T& InValue)
	{
		return Add(sizeof(T), 1, (const void*)&InValue);
	}

	template<class T>
	int32 Add(const FName& InNewName, const T& InValue)
	{
		return Add(InNewName, sizeof(T), 1, (const void*)&InValue);
	}

	template<class T>
	int32 AddArray(const TArray<T>& InArray)
	{
		return Add(sizeof(T), InArray.Num(), (const void*)InArray.GetData());
	}

	template<class T>
	int32 AddArray(const FName& InNewName, const TArray<T>& InArray)
	{
		return Add(InNewName, sizeof(T), InArray.Num(), (const void*)InArray.GetData());
	}

	bool Remove(int32 InAddressIndex);
	bool Remove(const FName& InAddressName);
	FName Rename(int32 InAddressIndex, const FName& InNewName);
	FName Rename(const FName& InOldName, const FName& InNewName);
	bool Resize(int32 InAddressIndex, int32 InNewElementCount);
	bool Resize(const FName& InAddressName, int32 InNewElementCount);

	void UpdateAddresses();

private:

	// disable copy constructor
	FMultiplexStorage(const FMultiplexStorage& Other) {}

	UPROPERTY()
	bool bUseNameMap;

	UPROPERTY()
	TArray<FMultiplexAddress> Addresses;

	UPROPERTY()
	TArray<uint8> Data;

	UPROPERTY(transient)
	TMap<FName, int32> NameMap;
};
