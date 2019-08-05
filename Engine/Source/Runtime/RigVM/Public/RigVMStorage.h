// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMStorage.generated.h"

struct RIGVM_API FRigVMArgument
{
public:

	FRigVMArgument()
		: bIsLiteral(false)
		, RegisterIndex(INDEX_NONE)
		, ByteIndex(INDEX_NONE)
	{
	}

	FRigVMArgument(bool InIsLiteral, int32 InRegisterIndex, int32 InByteIndex)
		: bIsLiteral(InIsLiteral)
		, RegisterIndex(InRegisterIndex)
		, ByteIndex(InByteIndex)
	{
	}

	FORCEINLINE bool IsLiteral() const { return bIsLiteral; }
	FORCEINLINE int32 StorageType() const { return bIsLiteral ? 1 : 0; }
	FORCEINLINE int32 GetRegisterIndex() const { return RegisterIndex; }
	FORCEINLINE int32 GetByteIndex() const { return ByteIndex; }

private:

	bool bIsLiteral;
	int32 RegisterIndex;
	int32 ByteIndex;
};

typedef TArrayView<FRigVMArgument> FRigVMArgumentArray;

UENUM()
enum class ERigVMAddressType : uint8
{
	Plain,
	String,
	Name,
	Struct,
	Invalid
};

USTRUCT()
struct RIGVM_API FRigVMAddress
{
	GENERATED_USTRUCT_BODY()

		FRigVMAddress()
		: Type(ERigVMAddressType::Invalid)
		, Pointer(nullptr)
		, ByteIndex(INDEX_NONE)
		, ElementSize(0)
		, ElementCount(0)
		, AlignmentBytes(0)
		, Name(NAME_None)
		, ScriptStructIndex(INDEX_NONE)
	{
	}

	UPROPERTY()
	ERigVMAddressType Type;

	void* Pointer;

	UPROPERTY()
	int32 ByteIndex;

	UPROPERTY()
	int32 ElementSize;

	UPROPERTY()
	int32 ElementCount;

	UPROPERTY()
	int32 AlignmentBytes;

	UPROPERTY()
	FName Name;

	UPROPERTY()
	int32 ScriptStructIndex;

	FORCEINLINE int32 FirstByte() const { return ByteIndex + AlignmentBytes; }
	FORCEINLINE bool IsArray() const { return ElementCount > 1; }
	FORCEINLINE bool IsPlain() const { return ScriptStructIndex == INDEX_NONE;  }
	FORCEINLINE int32 NumBytes(bool bIncludeAlignment = true) const { return ElementCount * ElementSize + (bIncludeAlignment ? AlignmentBytes : 0); }

	template<class T>
	FORCEINLINE const T* Get() const
	{
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
		ensure(ElementCount > 0);
		return TArrayView<T>((T*)Pointer, ElementCount);
	}
};

typedef TArrayView<FRigVMAddress> FRigVMAddressArray;

USTRUCT()
struct RIGVM_API FRigVMStorage
{
	GENERATED_USTRUCT_BODY()
	
public:

	FRigVMStorage(bool bInUseNames = true);
	FRigVMStorage(const FRigVMStorage& Other);
	~FRigVMStorage();

	FRigVMStorage& operator= (const FRigVMStorage &InOther);

	FORCEINLINE bool IsLiteralStorage() const { return bIsLiteralStorage;  }
	FORCEINLINE void SetLiteralStorage(bool InIsLiteralStorage = true) { bIsLiteralStorage = InIsLiteralStorage; }
	FORCEINLINE bool SupportsNames() const { return bUseNameMap;  }
	FORCEINLINE int32 Num() const { return Addresses.Num(); }
	FORCEINLINE const FRigVMAddress& operator[](int32 InIndex) const { return Addresses[InIndex]; }
	FORCEINLINE FRigVMAddress& operator[](int32 InIndex) { return Addresses[InIndex]; }
	FORCEINLINE const FRigVMAddress& operator[](const FRigVMArgument& InArg) const { return Addresses[InArg.GetRegisterIndex()]; }
	FORCEINLINE FRigVMAddress& operator[](const FRigVMArgument& InArg) { return Addresses[InArg.GetRegisterIndex()]; }
	FORCEINLINE const FRigVMAddress& operator[](const FName& InName) const { return Addresses[GetIndex(InName)]; }
	FORCEINLINE FRigVMAddress& operator[](const FName& InName) { return Addresses[GetIndex(InName)]; }

	FORCEINLINE TArray<FRigVMAddress>::RangedForIteratorType      begin() { return Addresses.begin(); }
	FORCEINLINE TArray<FRigVMAddress>::RangedForConstIteratorType begin() const { return Addresses.begin(); }
	FORCEINLINE TArray<FRigVMAddress>::RangedForIteratorType      end() { return Addresses.end(); }
	FORCEINLINE TArray<FRigVMAddress>::RangedForConstIteratorType end() const { return Addresses.end(); }

	FORCEINLINE FRigVMArgument GetArgument(int32 InAddressIndex) const
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		return FRigVMArgument(IsLiteralStorage(), InAddressIndex, Addresses[InAddressIndex].ByteIndex);
	}

	FORCEINLINE const void* GetData(int32 InAddressIndex) const
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		const FRigVMAddress& Address = Addresses[InAddressIndex];
		ensure(Address.ElementCount > 0);
		return (const void*)&Data[Address.FirstByte()];
	}

	FORCEINLINE void* GetData(int32 InAddressIndex)
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		const FRigVMAddress& Address = Addresses[InAddressIndex];
		ensure(Address.ElementCount > 0);
		return (void*)&Data[Address.FirstByte()];
	}

	template<class T>
	FORCEINLINE const T* Get(int32 InAddressIndex) const
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		const FRigVMAddress& Address = Addresses[InAddressIndex];
		ensure(Address.ElementCount > 0);
		return (const T*)&Data[Address.FirstByte()];
	}

	template<class T>
	FORCEINLINE const T* Get(const FRigVMArgument& InArgument) const
	{
		return Get<T>(InArgument.Index());
	}

	template<class T>
	FORCEINLINE const T& GetRef(int32 InAddressIndex) const
	{
		return *Get<T>(InAddressIndex);
	}

	template<class T>
	FORCEINLINE const T& GetRef(const FRigVMArgument& InArgument) const
	{
		return GetRef<T>(InArgument.Index());
	}

	template<class T>
	FORCEINLINE T* Get(int32 InAddressIndex)
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		const FRigVMAddress& Address = Addresses[InAddressIndex];
		ensure(Address.ElementCount > 0);
		return (T*)&Data[Address.FirstByte()];
	}

	template<class T>
	FORCEINLINE T* Get(const FRigVMArgument& InArgument)
	{
		return Get<T>(InArgument.Index());
	}

	template<class T>
	FORCEINLINE T& GetRef(int32 InAddressIndex)
	{
		return *Get<T>(InAddressIndex);
	}

	template<class T>
	FORCEINLINE T& GetRef(const FRigVMArgument& InArgument)
	{
		return GetRef<T>(InArgument.GetRegisterIndex());
	}

	template<class T>
	FORCEINLINE TArrayView<T> GetArray(int32 InAddressIndex)
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		const FRigVMAddress& Address = Addresses[InAddressIndex];
		ensure(Address.ElementCount > 0);
		return TArrayView<T>((T*)&Data[Address.FirstByte()], Address.ElementCount);
	}
	
	template<class T>
	FORCEINLINE TArrayView<T> GetArray(const FRigVMArgument& InArgument)
	{
		return GetArray<T>(InArgument.GetRegisterIndex());
	}

	FORCEINLINE UScriptStruct* GetScriptStruct(int32 InAddressIndex) const
	{
		ensure(Addresses.IsValidIndex(InAddressIndex));
		const FRigVMAddress& Address = Addresses[InAddressIndex];
		if (Address.ScriptStructIndex != INDEX_NONE)
		{
			ensure(ScriptStructs.IsValidIndex(Address.ScriptStructIndex));
			return ScriptStructs[Address.ScriptStructIndex];
		}
		return nullptr;
	}

	bool Copy(
		int32 InSourceAddressIndex,
		int32 InTargetAddressIndex,
		const FRigVMStorage* InSourceStorage = nullptr,
		int32 InSourceByteOffset = INDEX_NONE,
		int32 InTargetByteOffset = INDEX_NONE,
		int32 InNumBytes = INDEX_NONE);

	bool Copy(
		const FName& InSourceName,
		const FName& InTargetName,
		const FRigVMStorage* InSourceStorage = nullptr,
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

	FORCEINLINE int32 AddPlainArray(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InDataPtr = nullptr)
	{
		return Allocate(InNewName, InElementSize, InCount, InDataPtr);
	}

	template<class T>
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, int32 InCount, const T* InDataPtr = nullptr)
	{
		return Allocate(InNewName, sizeof(T), InCount, (const void*)InDataPtr);
	}

	template<class T>
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, const TArray<T>& InArray)
	{
		return AddPlainArray<T>(InNewName, InArray.Num(), InArray.GetData());
	}

	template<class T>
	FORCEINLINE int32 AddPlainArray(const TArray<T>& InArray)
	{
		return AddPlainArray<T>(NAME_None, InArray);
	}

	FORCEINLINE int32 AddPlain(const FName& InNewName, int32 InElementSize, const void* InValuePtr)
	{
		return AddPlainArray(InNewName, InElementSize, 1, InValuePtr);
	}

	FORCEINLINE int32 AddPlain(int32 InElementSize, const void* InValuePtr)
	{
		return AddPlain(NAME_None, InElementSize, InValuePtr);
	}

	template<class T>
	FORCEINLINE int32 AddPlain(const FName& InNewName, const T& InValue)
	{
		return AddPlainArray<T>(InNewName, 1, &InValue);
	}

	template<class T>
	FORCEINLINE int32 AddPlain(const T& InValue)
	{
		return AddPlain<T>(NAME_None, InValue);
	}

	FORCEINLINE int32 AddNameArray(const FName& InNewName, int32 InCount, const FName* InDataPtr = nullptr)
	{
		int32 Address = Allocate(InNewName, sizeof(FName), InCount, nullptr);
		Addresses[Address].Type = ERigVMAddressType::Name;

		Construct(Address);

		if(InDataPtr)
		{
			FName* DataPtr = (FName*)GetData(Address);
			for (int32 Index = 0; Index < InCount; Index++)
			{
				DataPtr[Index] = InDataPtr[Index];
			}
		}

		return Address;
	}

	FORCEINLINE int32 AddNameArray(const FName& InNewName, const TArray<FName>& InArray)
	{
		return AddNameArray(InNewName, InArray.Num(), InArray.GetData());
	}

	FORCEINLINE int32 AddNameArray(const TArray<FName>& InArray)
	{
		return AddNameArray(NAME_None, InArray);
	}

	FORCEINLINE int32 AddName(const FName& InNewName, const FName& InValue)
	{
		return AddNameArray(InNewName, 1, &InValue);
	}

	FORCEINLINE int32 AddName(const FName& InValue)
	{
		return AddName(NAME_None, InValue);
	}

	FORCEINLINE int32 AddStringArray(const FName& InNewName, int32 InCount, const FString* InDataPtr = nullptr)
	{
		int32 Address = Allocate(InNewName, sizeof(FString), InCount, nullptr);
		Addresses[Address].Type = ERigVMAddressType::String;

		Construct(Address);

		if(InDataPtr)
		{
			FString* DataPtr = (FString*)GetData(Address);
			for (int32 Index = 0; Index < InCount; Index++)
			{
				DataPtr[Index] = InDataPtr[Index];
			}
		}

		return Address;
	}

	FORCEINLINE int32 AddStringArray(const FName& InNewName, const TArray<FString>& InArray)
	{
		return AddStringArray(InNewName, InArray.Num(), InArray.GetData());
	}

	FORCEINLINE int32 AddStringArray(const TArray<FString>& InArray)
	{
		return AddStringArray(NAME_None, InArray);
	}

	FORCEINLINE int32 AddString(const FName& InNewName, const FString& InValue)
	{
		return AddStringArray(InNewName, 1, &InValue);
	}

	FORCEINLINE int32 AddString(const FString& InValue)
	{
		return AddString(NAME_None, InValue);
	}


	FORCEINLINE int32 AddStructArray(const FName& InNewName, UScriptStruct* InScriptStruct, int32 InCount, const void* InDataPtr = nullptr)
	{
		int32 Address = Allocate(InNewName, InScriptStruct->GetStructureSize(), InCount, nullptr, false);
		if (Address == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		Addresses[Address].Type = ERigVMAddressType::Struct;
		Addresses[Address].ScriptStructIndex = FindOrAddScriptStruct(InScriptStruct);

		UpdateAddresses();

		// construct the content
		Construct(Address);

		// copy values from the provided data
		if (InDataPtr != nullptr)
		{
			InScriptStruct->CopyScriptStruct(GetData(Address), InDataPtr, InCount);
		}

		return Address;
	}

	FORCEINLINE int32 AddStructArray(UScriptStruct* InScriptStruct, int32 InCount, const void* InDataPtr = nullptr)
	{
		return AddStructArray(NAME_None, InScriptStruct, InCount, InDataPtr);
	}

	template<class T>
	FORCEINLINE int32 AddStructArray(const FName& InNewName, int32 InCount, const T* InDataPtr = nullptr)
	{
		// if you are hitting this - you might need to use AddPlainArray instead!
		UScriptStruct* Struct = T::StaticStruct();
		if (Struct == nullptr)
		{
			return INDEX_NONE;
		}
	
		return AddStructArray(InNewName, Struct, InCount, InDataPtr);
	}

	template<class T>
	FORCEINLINE int32 AddStructArray(const FName& InNewName, const TArray<T>& InArray)
	{
		return AddStructArray<T>(InNewName, InArray.Num(), InArray.GetData());
	}

	template<class T>
	FORCEINLINE int32 AddStructArray(const TArray<T>& InArray)
	{
		return AddStructArray<T>(NAME_None, InArray);
	}

	FORCEINLINE int32 AddStruct(const FName& InNewName, UScriptStruct* InScriptStruct, const void* InValuePtr)
	{
		return AddStructArray(InNewName, InScriptStruct, 1, InValuePtr);
	}

	FORCEINLINE int32 AddStruct(UScriptStruct* InScriptStruct, const void* InValuePtr)
	{
		return AddStruct(NAME_None, InScriptStruct, InValuePtr);
	}

	template<class T>
	FORCEINLINE int32 AddStruct(const FName& InNewName, const T& InValue)
	{
		return AddStructArray<T>(InNewName, 1, &InValue);
	}

	template<class T>
	FORCEINLINE int32 AddStruct(const T& InValue)
	{
		return AddStruct<T>(NAME_None, InValue);
	}

	bool Remove(int32 InAddressIndex);
	bool Remove(const FName& InAddressName);
	FName Rename(int32 InAddressIndex, const FName& InNewName);
	FName Rename(const FName& InOldName, const FName& InNewName);
	bool Resize(int32 InAddressIndex, int32 InNewElementCount);
	bool Resize(const FName& InAddressName, int32 InNewElementCount);

	void UpdateAddresses();

private:

	int32 Allocate(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InDataPtr = nullptr, bool bUpdateAddresses = true);
	int32 Allocate(int32 InElementSize, int32 InCount, const void* InDataPtr = nullptr, bool bUpdateAddresses = true);
	bool Construct(int32 InAddressIndex, int32 InElementIndex = INDEX_NONE);
	bool Destroy(int32 InAddressIndex, int32 InElementIndex = INDEX_NONE);

	void FillWithZeroes(int32 InAddressIndex);
	int32 FindOrAddScriptStruct(UScriptStruct* InScriptStruct);

	UPROPERTY()
	bool bUseNameMap;

	UPROPERTY()
	bool bIsLiteralStorage;

	UPROPERTY()
	TArray<FRigVMAddress> Addresses;

	UPROPERTY()
	TArray<uint8> Data;

	UPROPERTY()
	TArray<UScriptStruct*> ScriptStructs;

	UPROPERTY(transient)
	TMap<FName, int32> NameMap;
};

typedef FRigVMStorage* FRigVMStoragePtr;
typedef TArrayView<FRigVMStoragePtr> FRigVMStoragePtrArray;