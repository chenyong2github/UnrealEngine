// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMStorage.generated.h"

UENUM()
enum class ERigVMStorageType: uint8
{
	Work,
	Literal,
	Invalid
};

struct RIGVM_API FRigVMArgument
{
public:

	FRigVMArgument()
		: StorageType(ERigVMStorageType::Work)
		, RegisterIndex(INDEX_NONE)
		, ByteIndex(INDEX_NONE)
	{
	}

	FRigVMArgument(ERigVMStorageType InStorageType, int32 InRegisterIndex, int32 InByteIndex)
		: StorageType(InStorageType)
		, RegisterIndex(InRegisterIndex)
		, ByteIndex(InByteIndex)
	{
	}

	FORCEINLINE ERigVMStorageType GetStorageType() const { return StorageType; }
	FORCEINLINE int32 GetStorageIndex() const { return (int32)StorageType; }
	FORCEINLINE uint16 GetRegisterIndex() const { return RegisterIndex; }
	FORCEINLINE uint64 GetByteIndex() const { return ByteIndex; }

private:

	ERigVMStorageType StorageType;
	uint16 RegisterIndex;
	uint64 ByteIndex;
};

typedef TArrayView<FRigVMArgument> FRigVMArgumentArray;

UENUM()
enum class ERigVMRegisterType : uint8
{
	Plain,
	String,
	Name,
	Struct,
	Invalid
};

USTRUCT()
struct RIGVM_API FRigVMRegister
{
	GENERATED_USTRUCT_BODY()

		FRigVMRegister()
		: Type(ERigVMRegisterType::Invalid)
		, ByteIndex(INDEX_NONE)
		, ElementSize(0)
		, ElementCount(0)
		, SliceIndex(0)
		, SliceCount(1)
		, AlignmentBytes(0)
		, Name(NAME_None)
		, ScriptStructIndex(INDEX_NONE)
	{
	}

	UPROPERTY()
		ERigVMRegisterType Type;

	UPROPERTY()
		uint32 ByteIndex;

	UPROPERTY()
		uint16 ElementSize;

	UPROPERTY()
		uint16 ElementCount;

	UPROPERTY()
		uint16 SliceIndex;

	UPROPERTY()
		uint16 SliceCount;

	UPROPERTY()
		uint8 AlignmentBytes;

	UPROPERTY()
		FName Name;

	UPROPERTY()
		int32 ScriptStructIndex;

	FORCEINLINE uint64 GetWorkByteIndex() const { return ByteIndex; }
	FORCEINLINE uint64 GetStorageByteIndex() const { return ByteIndex - (uint64)AlignmentBytes - (uint64)(SliceIndex * GetNumBytesPerSlice()); }
	FORCEINLINE uint8 GetAlignmentBytes() const { return AlignmentBytes; }
	FORCEINLINE bool IsArray() const { return ElementCount > 1; }
	FORCEINLINE bool IsPlain() const { return ScriptStructIndex == INDEX_NONE; }
	FORCEINLINE uint16 GetAllocatedBytes() const { return ElementCount * ElementSize * SliceCount + (uint16)AlignmentBytes; }
	FORCEINLINE uint16 GetNumBytesPerSlice() const { return ElementCount * ElementSize; }
	FORCEINLINE uint16 GetNumBytesAllSlices() const { return ElementCount * ElementSize * SliceCount; }
	FORCEINLINE uint32 GetTotalElementCount() const { return (uint32)ElementCount * (uint32)SliceCount; }
	FORCEINLINE void MoveToFirstSlice() { ByteIndex -= SliceIndex * GetNumBytesPerSlice(); SliceIndex = 0; }
	FORCEINLINE void MoveToNextSlice()
	{
		if(SliceCount == 1)
		{
			return;
		}
		
		if(SliceIndex == SliceCount - 1)
		{
			MoveToFirstSlice();
		}
		else
		{
			ByteIndex += GetNumBytesPerSlice();
			SliceIndex++;
		}
	}
};

typedef TArrayView<FRigVMRegister> FRigVMRegisterArray;

USTRUCT()
struct RIGVM_API FRigVMStorage
{
	GENERATED_USTRUCT_BODY()
	
public:

	FRigVMStorage(bool bInUseNames = true);
	FRigVMStorage(const FRigVMStorage& Other);
	~FRigVMStorage();

	FRigVMStorage& operator= (const FRigVMStorage &InOther);

	FORCEINLINE ERigVMStorageType GetStorageType() const { return StorageType;  }
	FORCEINLINE void SetStorageType(ERigVMStorageType InStorageType) { StorageType = InStorageType; }
	FORCEINLINE bool SupportsNames() const { return bUseNameMap;  }
	FORCEINLINE int32 Num() const { return Registers.Num(); }
	FORCEINLINE const FRigVMRegister& operator[](int32 InIndex) const { return Registers[InIndex]; }
	FORCEINLINE FRigVMRegister& operator[](int32 InIndex) { return Registers[InIndex]; }
	FORCEINLINE const FRigVMRegister& operator[](const FRigVMArgument& InArg) const { return Registers[InArg.GetRegisterIndex()]; }
	FORCEINLINE FRigVMRegister& operator[](const FRigVMArgument& InArg) { return Registers[InArg.GetRegisterIndex()]; }
	FORCEINLINE const FRigVMRegister& operator[](const FName& InName) const { return Registers[GetIndex(InName)]; }
	FORCEINLINE FRigVMRegister& operator[](const FName& InName) { return Registers[GetIndex(InName)]; }

	FORCEINLINE TArray<FRigVMRegister>::RangedForIteratorType      begin() { return Registers.begin(); }
	FORCEINLINE TArray<FRigVMRegister>::RangedForConstIteratorType begin() const { return Registers.begin(); }
	FORCEINLINE TArray<FRigVMRegister>::RangedForIteratorType      end() { return Registers.end(); }
	FORCEINLINE TArray<FRigVMRegister>::RangedForConstIteratorType end() const { return Registers.end(); }

	FORCEINLINE FRigVMArgument GetArgument(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		return FRigVMArgument(StorageType, InRegisterIndex, Registers[InRegisterIndex].ByteIndex);
	}

	FORCEINLINE const void* GetData(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		ensure(Register.ElementCount > 0);
		return (const void*)&Data[Register.GetWorkByteIndex()];
	}

	FORCEINLINE void* GetData(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		ensure(Register.ElementCount > 0);
		if(bMoveToNextSlice)
		{
			Register.MoveToNextSlice();
		}
		return (void*)&Data[Register.GetWorkByteIndex()];
	}

	template<class T>
	FORCEINLINE const T* Get(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		ensure(Register.ElementCount > 0);
		return (const T*)&Data[Register.GetWorkByteIndex()];
	}

	template<class T>
	FORCEINLINE const T* Get(const FRigVMArgument& InArgument) const
	{
		return Get<T>(InArgument.GetRegisterIndex());
	}

	template<class T>
	FORCEINLINE const T& GetRef(int32 InRegisterIndex) const
	{
		return *Get<T>(InRegisterIndex);
	}

	template<class T>
	FORCEINLINE const T& GetRef(const FRigVMArgument& InArgument) const
	{
		return GetRef<T>(InArgument.GetRegisterIndex());
	}

	template<class T>
	FORCEINLINE T* Get(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		ensure(Register.ElementCount > 0);
		if(bMoveToNextSlice)
		{
			Register.MoveToNextSlice();
		}
		return (T*)&Data[Register.GetWorkByteIndex()];
	}

	template<class T>
	FORCEINLINE T* Get(const FRigVMArgument& InArgument, bool bMoveToNextSlice = false)
	{
		return Get<T>(InArgument.GetRegisterIndex(), bMoveToNextSlice);
	}

	template<class T>
	FORCEINLINE T& GetRef(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		return *Get<T>(InRegisterIndex, bMoveToNextSlice);
	}

	template<class T>
	FORCEINLINE T& GetRef(const FRigVMArgument& InArgument, bool bMoveToNextSlice = false)
	{
		return GetRef<T>(InArgument.GetRegisterIndex(), bMoveToNextSlice);
	}

	template<class T>
	FORCEINLINE TArrayView<T> GetArray(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		ensure(Register.ElementCount > 0);
		if(bMoveToNextSlice)
		{
			Register.MoveToNextSlice();
		}
		return TArrayView<T>((T*)&Data[Register.GetWorkByteIndex()], Register.ElementCount);
	}
	
	template<class T>
	FORCEINLINE TArrayView<T> GetArray(const FRigVMArgument& InArgument, bool bMoveToNextSlice = false)
	{
		return GetArray<T>(InArgument.GetRegisterIndex(), bMoveToNextSlice);
	}

	FORCEINLINE UScriptStruct* GetScriptStruct(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		if (Register.ScriptStructIndex != INDEX_NONE)
		{
			ensure(ScriptStructs.IsValidIndex(Register.ScriptStructIndex));
			return ScriptStructs[Register.ScriptStructIndex];
		}
		return nullptr;
	}

	bool Copy(
		int32 InSourceRegisterIndex,
		int32 InTargetRegisterIndex,
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

		if (NameMap.Num() != Registers.Num())
		{
			for (int32 Index = 0; Index < Registers.Num(); Index++)
			{
				if (Registers[Index].Name == InName)
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

	FORCEINLINE int32 AddPlainArray(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		return Allocate(InNewName, InElementSize, InCount, InSliceCount, InDataPtr);
	}

	template<class T>
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, int32 InCount, const T* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		return Allocate(InNewName, sizeof(T), InCount, InSliceCount, (const void*)InDataPtr);
	}

	template<class T>
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddPlainArray<T>(InNewName, InArray.Num(), InArray.GetData(), InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddPlainArray(const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddPlainArray<T>(NAME_None, InArray, InSliceCount);
	}

	FORCEINLINE int32 AddPlain(const FName& InNewName, int32 InElementSize, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddPlainArray(InNewName, InElementSize, 1, InValuePtr, InSliceCount);
	}

	FORCEINLINE int32 AddPlain(int32 InElementSize, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddPlain(NAME_None, InElementSize, InValuePtr, InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddPlain(const FName& InNewName, const T& InValue, int32 InSliceCount = 1)
	{
		return AddPlainArray<T>(InNewName, 1, &InValue, InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddPlain(const T& InValue, int32 InSliceCount = 1)
	{
		return AddPlain<T>(NAME_None, InValue, InSliceCount);
	}

	FORCEINLINE int32 AddNameArray(const FName& InNewName, int32 InCount, const FName* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		int32 Register = Allocate(InNewName, sizeof(FName), InCount, InSliceCount, nullptr);
		Registers[Register].Type = ERigVMRegisterType::Name;

		Construct(Register);

		if(InDataPtr)
		{
			Registers[Register].MoveToFirstSlice();
			for (uint16 SliceIndex = 0; SliceIndex < Registers[Register].SliceCount; SliceIndex++)
			{
				FName* DataPtr = (FName*)GetData(Register);
				for (int32 Index = 0; Index < InCount; Index++)
				{
					DataPtr[Index] = InDataPtr[Index];
				}
				Registers[Register].MoveToNextSlice();
			}
			Registers[Register].MoveToFirstSlice();
		}

		return Register;
	}

	FORCEINLINE int32 AddNameArray(const FName& InNewName, const TArray<FName>& InArray, int32 InSliceCount = 1)
	{
		return AddNameArray(InNewName, InArray.Num(), InArray.GetData(), InSliceCount);
	}

	FORCEINLINE int32 AddNameArray(const TArray<FName>& InArray, int32 InSliceCount = 1)
	{
		return AddNameArray(NAME_None, InArray, InSliceCount);
	}

	FORCEINLINE int32 AddName(const FName& InNewName, const FName& InValue, int32 InSliceCount = 1)
	{
		return AddNameArray(InNewName, 1, &InValue, InSliceCount);
	}

	FORCEINLINE int32 AddName(const FName& InValue, int32 InSliceCount = 1)
	{
		return AddName(NAME_None, InValue, InSliceCount);
	}

	FORCEINLINE int32 AddStringArray(const FName& InNewName, int32 InCount, const FString* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		int32 Register = Allocate(InNewName, sizeof(FString), InCount, InSliceCount, nullptr);
		Registers[Register].Type = ERigVMRegisterType::String;

		Construct(Register);

		if(InDataPtr)
		{
			Registers[Register].MoveToFirstSlice();
			for (uint16 SliceIndex = 0; SliceIndex < Registers[Register].SliceCount; SliceIndex++)
			{
				FString* DataPtr = (FString*)GetData(Register);
				for (int32 Index = 0; Index < InCount; Index++)
				{
					DataPtr[Index] = InDataPtr[Index];
				}
				Registers[Register].MoveToNextSlice();
			}
			Registers[Register].MoveToFirstSlice();
		}

		return Register;
	}

	FORCEINLINE int32 AddStringArray(const FName& InNewName, const TArray<FString>& InArray, int32 InSliceCount = 1)
	{
		return AddStringArray(InNewName, InArray.Num(), InArray.GetData(), InSliceCount);
	}

	FORCEINLINE int32 AddStringArray(const TArray<FString>& InArray, int32 InSliceCount = 1)
	{
		return AddStringArray(NAME_None, InArray, InSliceCount);
	}

	FORCEINLINE int32 AddString(const FName& InNewName, const FString& InValue, int32 InSliceCount = 1)
	{
		return AddStringArray(InNewName, 1, &InValue, InSliceCount);
	}

	FORCEINLINE int32 AddString(const FString& InValue, int32 InSliceCount = 1)
	{
		return AddString(NAME_None, InValue, InSliceCount);
	}


	FORCEINLINE int32 AddStructArray(const FName& InNewName, UScriptStruct* InScriptStruct, int32 InCount, const void* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		int32 Register = Allocate(InNewName, InScriptStruct->GetStructureSize(), InCount, InSliceCount, nullptr, false);
		if (Register == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		Registers[Register].Type = ERigVMRegisterType::Struct;
		Registers[Register].ScriptStructIndex = FindOrAddScriptStruct(InScriptStruct);

		UpdateRegisters();

		// construct the content
		Construct(Register);

		// copy values from the provided data
		if (InDataPtr != nullptr)
		{
			Registers[Register].MoveToFirstSlice();
			for (uint16 SliceIndex = 0; SliceIndex < Registers[Register].SliceCount; SliceIndex++)
			{
				InScriptStruct->CopyScriptStruct(GetData(Register), InDataPtr, InCount);
				Registers[Register].MoveToNextSlice();
			}
			Registers[Register].MoveToFirstSlice();
		}

		return Register;
	}

	FORCEINLINE int32 AddStructArray(UScriptStruct* InScriptStruct, int32 InCount, const void* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		return AddStructArray(NAME_None, InScriptStruct, InCount, InDataPtr, InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddStructArray(const FName& InNewName, int32 InCount, const T* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		// if you are hitting this - you might need to use AddPlainArray instead!
		UScriptStruct* Struct = T::StaticStruct();
		if (Struct == nullptr)
		{
			return INDEX_NONE;
		}
	
		return AddStructArray(InNewName, Struct, InCount, InDataPtr, InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddStructArray(const FName& InNewName, const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddStructArray<T>(InNewName, InArray.Num(), InArray.GetData(), InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddStructArray(const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddStructArray<T>(NAME_None, InArray, InSliceCount);
	}

	FORCEINLINE int32 AddStruct(const FName& InNewName, UScriptStruct* InScriptStruct, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddStructArray(InNewName, InScriptStruct, 1, InValuePtr, InSliceCount);
	}

	FORCEINLINE int32 AddStruct(UScriptStruct* InScriptStruct, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddStruct(NAME_None, InScriptStruct, InValuePtr, InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddStruct(const FName& InNewName, const T& InValue, int32 InSliceCount = 1)
	{
		return AddStructArray<T>(InNewName, 1, &InValue, InSliceCount);
	}

	template<class T>
	FORCEINLINE int32 AddStruct(const T& InValue, int32 InSliceCount = 1)
	{
		return AddStruct<T>(NAME_None, InValue, InSliceCount);
	}

	bool Remove(int32 InRegisterIndex);
	bool Remove(const FName& InRegisterName);
	FName Rename(int32 InRegisterIndex, const FName& InNewName);
	FName Rename(const FName& InOldName, const FName& InNewName);
	bool Resize(int32 InRegisterIndex, int32 InNewElementCount, int32 InNewSliceCount = 1);
	bool Resize(const FName& InRegisterName, int32 InNewElementCount, int32 InNewSliceCount = 1);

	void UpdateRegisters();

private:

	int32 Allocate(const FName& InNewName, int32 InElementSize, int32 InElementCount, int32 InSliceCount, const void* InDataPtr = nullptr, bool bUpdateRegisters = true);
	int32 Allocate(int32 InElementSize, int32 InElementCount, int32 InSliceCount, const void* InDataPtr = nullptr, bool bUpdateRegisters = true);
	bool Construct(int32 InRegisterIndex, int32 InElementIndex = INDEX_NONE);
	bool Destroy(int32 InRegisterIndex, int32 InElementIndex = INDEX_NONE);

	void FillWithZeroes(int32 InRegisterIndex);
	int32 FindOrAddScriptStruct(UScriptStruct* InScriptStruct);

	UPROPERTY()
	bool bUseNameMap;

	UPROPERTY()
	ERigVMStorageType StorageType;

	UPROPERTY()
	TArray<FRigVMRegister> Registers;

	UPROPERTY()
	TArray<uint8> Data;

	UPROPERTY()
	TArray<UScriptStruct*> ScriptStructs;

	UPROPERTY(transient)
	TMap<FName, int32> NameMap;
};

typedef FRigVMStorage* FRigVMStoragePtr;
typedef TArrayView<FRigVMStoragePtr> FRigVMStoragePtrArray;