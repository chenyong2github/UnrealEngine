// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMMemory.generated.h"

/**
 * The type of memory used. Typically we differentiate between
 * Work (Mutable) and Literal (Constant) memory.
 */
UENUM()
enum class ERigVMMemoryType: uint8
{
	Work, // Mutable state
	Literal, // Const / fixed state
	Invalid
};

/**
 * The FRigVMArgument represents an argument used for an operator
 * within the virtual machine. Arguments provide information about
 * which memory needs to be referred to, which register within the
 * memory all the way to the actual byte address in memory.
 * The FRigVMArgument is a light weight address for a register in
 * a FRigVMMemoryContainer.
 */
struct RIGVM_API FRigVMArgument
{
public:

	FRigVMArgument()
		: MemoryType(ERigVMMemoryType::Work)
		, RegisterIndex(INDEX_NONE)
		, ByteIndex(INDEX_NONE)
	{
	}

	FRigVMArgument(ERigVMMemoryType InMemoryType, int32 InRegisterIndex, int32 InByteIndex)
		: MemoryType(InMemoryType)
		, RegisterIndex(InRegisterIndex)
		, ByteIndex(InByteIndex)
	{
	}

	// returns the memory type of this argument
	FORCEINLINE ERigVMMemoryType GetMemoryType() const { return MemoryType; }

	// returns the index of the container of this argument
	FORCEINLINE int32 GetContainerIndex() const { return (int32)MemoryType; }

	// returns the index of the register of this argument
	FORCEINLINE uint16 GetRegisterIndex() const { return RegisterIndex; }

	// returns the index of the byte inside of the memory of this argument
	FORCEINLINE uint64 GetByteIndex() const { return ByteIndex; }

private:

	ERigVMMemoryType MemoryType;
	uint16 RegisterIndex;
	uint64 ByteIndex;
};

typedef TArrayView<FRigVMArgument> FRigVMArgumentArray;

/**
 * The type of register within the memory.
 */
UENUM()
enum class ERigVMRegisterType : uint8
{
	Plain, // bool, int32, float, FVector etc.
	String, // FString
	Name, // FName
	Struct, // Any USTRUCT
	Invalid
};

// The register represents an address with the VM's memory. Within a register
// we can store arbitrary data, so it provides a series of properties to
// describe the memory location.
// Registers also support the notion of slices. A slice is a complete copy of
// the memory - so for example if you register stores 4 Vectors, then a slice 
// would contain 48 bytes (4 * 3 * 4). The register can however store multiple
// slices / copies of that if needed. Slices can be used to provide 
// per-invocation memory to functions within the same register.
// An integrator for example that needs to store a simulated position
// might want access to a separate memory memory per loop iteration.
USTRUCT()
struct RIGVM_API FRigVMRegister
{
	GENERATED_BODY()

	FRigVMRegister()
		: Type(ERigVMRegisterType::Invalid)
		, ByteIndex(INDEX_NONE)
		, ElementSize(0)
		, ElementCount(0)
		, SliceIndex(0)
		, SliceCount(1)
		, AlignmentBytes(0)
		, TrailingBytes(0)
		, Name(NAME_None)
		, ScriptStructIndex(INDEX_NONE)
	{
	}

	// The type of register (plain, name, string, etc.)
	UPROPERTY()
	ERigVMRegisterType Type;

	// The index of the first work byte
	UPROPERTY()
	uint32 ByteIndex;

	// The size of each store element
	UPROPERTY()
	uint16 ElementSize;

	// The number of elements in this register
	UPROPERTY()
	uint16 ElementCount;

	// The currently active slice index
	UPROPERTY()
	uint16 SliceIndex;

	// The number of slices (complete copies)
	UPROPERTY()
	uint16 SliceCount;

	// The number of leading bytes for alignment
	UPROPERTY()
	uint8 AlignmentBytes;

	// The numbber of trailing bytes.
	// These originate after shrinking a register.
	UPROPERTY()
	uint16 TrailingBytes;

	// The name of the register (can be None)
	UPROPERTY()
	FName Name;

	// For struct registers this is the index of the
	// struct used - otherwise INDEX_NONE
	UPROPERTY()
	int32 ScriptStructIndex;

	bool Serialize(FArchive& Ar);
	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FRigVMRegister& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	// returns the current address of the register within the data byte array.
	// this can change over time - as the register is moving through slices.
	// use GetFirstAllocatedByte to get the fixed first byte.
	FORCEINLINE uint64 GetWorkByteIndex() const { return ByteIndex; }

	// returns the first allocated byte in the data byte array
	FORCEINLINE uint64 GetFirstAllocatedByte() const { return ByteIndex - (uint64)AlignmentBytes - (uint64)(SliceIndex * GetNumBytesPerSlice()); }

	// Returns the leading alignment bytes
	FORCEINLINE uint8 GetAlignmentBytes() const { return AlignmentBytes; }

	// Returns true if the register stores more than one element
	FORCEINLINE bool IsArray() const { return ElementCount > 1; }

	// Returns true if the register stores plain / shallow memory
	FORCEINLINE bool IsPlain() const { return ScriptStructIndex == INDEX_NONE; }

	// Returns the number of allocated bytes (inclusing alignment + trailing bytes)
	FORCEINLINE uint16 GetAllocatedBytes() const { return ElementCount * ElementSize * SliceCount + (uint16)AlignmentBytes + TrailingBytes; }

	// Returns the number of bytes for a complete slice
	FORCEINLINE uint16 GetNumBytesPerSlice() const { return ElementCount * ElementSize; }

	// Returns the number of bytes for all slices
	FORCEINLINE uint16 GetNumBytesAllSlices() const { return ElementCount * ElementSize * SliceCount; }

	// Returns the total number of elements (elementcount * slicecount) in the register
	FORCEINLINE uint32 GetTotalElementCount() const { return (uint32)ElementCount * (uint32)SliceCount; }
	
	// Moves the register address to the first slice
	FORCEINLINE void MoveToFirstSlice() { ByteIndex -= SliceIndex * GetNumBytesPerSlice(); SliceIndex = 0; }

	// Moves the register address to the next slice. This also wraps around - so when the register reaches 
	// the end it will move to the first slice again.
	FORCEINLINE void MoveToNextSlice()
	{
		if(SliceCount <= 1)
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

/**
 * The FRigVMMemoryContainer provides a heterogeneous memory container to store arbitrary
 * data. Each element stored can be referred to using a FRigVMRegister.
 * Elements can be accessed by index (index of the register), FRigVMArgument or by name.
 * Name access is optional and is specified upon construction of the container.
 * The memory container provides a series of templated functions to add and get data.
 * Plain types (shallow types without the need for construction) can be added an retrieved
 * using the methods suffixed with 'Plain'.
 *
 * For example:
 * 		int32 Index = Container.AddPlain<float>(4.f);
 *      float& ValueRef = Container.GetRef<float>(Index);
 *
 * This can also be done with arrays:
 *      TArray<float> MyArray = {3.f, 4.f, 5.f};
 * 		int32 Index = Container.AddPlainArray<float>(MyArray);
 *      TArrayView<float> ArrayView = Container.GetArray<float>(Index);
 *
 * More complex data structures such as USTRUCT types need to be store with the
 * 'Struct' suffixed methods - since the require a constructor / destructor to be called.
 *
 * For example:
 *
 *		FMyStruct MyStruct;
 *		int32 Index = Container.AddStruct<FMyStruct>(MyStruct);
 *      FMyStruct& ValueRef = Container.GetRef<FMyStruct>(Index);
 */
USTRUCT()
struct RIGVM_API FRigVMMemoryContainer
{
	GENERATED_BODY()
	
public:

	FRigVMMemoryContainer(bool bInUseNames = true);
	FRigVMMemoryContainer(const FRigVMMemoryContainer& Other);
	~FRigVMMemoryContainer();

	FRigVMMemoryContainer& operator= (const FRigVMMemoryContainer &InOther);

	// returns the memory type of this container
	FORCEINLINE ERigVMMemoryType GetMemoryType() const { return MemoryType;  }

	// sets the memory type. should only be used when the container is empty
	FORCEINLINE void SetMemoryType(ERigVMMemoryType InMemoryType) { MemoryType = InMemoryType; }

	// returns true if this container supports name based lookup
	FORCEINLINE bool SupportsNames() const { return bUseNameMap;  }

	// returns the number of registers in this container
	FORCEINLINE int32 Num() const { return Registers.Num(); }

	// resets the container and removes all storage.
	void Reset();

	// const accessor for a register based on index
	FORCEINLINE const FRigVMRegister& operator[](int32 InIndex) const { return Registers[InIndex]; }

	// accessor for a register based on index
	FORCEINLINE FRigVMRegister& operator[](int32 InIndex) { return Registers[InIndex]; }

	// const accessor for a register based on an argument
	FORCEINLINE const FRigVMRegister& operator[](const FRigVMArgument& InArg) const { return Registers[InArg.GetRegisterIndex()]; }

	// accessor for a register based on an argument
	FORCEINLINE FRigVMRegister& operator[](const FRigVMArgument& InArg) { return Registers[InArg.GetRegisterIndex()]; }

	// const accessor for a register based on a a name. note: only works if SupportsNames() == true
	FORCEINLINE const FRigVMRegister& operator[](const FName& InName) const { return Registers[GetIndex(InName)]; }

	// accessor for a register based on a a name. note: only works if SupportsNames() == true
	FORCEINLINE FRigVMRegister& operator[](const FName& InName) { return Registers[GetIndex(InName)]; }

	FORCEINLINE TArray<FRigVMRegister>::RangedForIteratorType      begin() { return Registers.begin(); }
	FORCEINLINE TArray<FRigVMRegister>::RangedForConstIteratorType begin() const { return Registers.begin(); }
	FORCEINLINE TArray<FRigVMRegister>::RangedForIteratorType      end() { return Registers.end(); }
	FORCEINLINE TArray<FRigVMRegister>::RangedForConstIteratorType end() const { return Registers.end(); }

	bool Serialize(FArchive& Ar);
	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FRigVMMemoryContainer& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	// Returns an argument for a given register.
	// This is typically used to store a light weight address for use within a VM.
	FORCEINLINE FRigVMArgument GetArgument(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		return FRigVMArgument(MemoryType, InRegisterIndex, Registers[InRegisterIndex].ByteIndex);
	}

	// Returns the current const data pointer for a given register.
	// Note: This refers to the active slice - and can change over time.
	FORCEINLINE const uint8* GetData(const FRigVMRegister& Register) const
	{
		ensure(Register.ElementCount > 0);
		return &Data[Register.GetWorkByteIndex()];
	}

	// Returns the current const data pointer for a given register index.
	// Note: This refers to the active slice - and can change over time.
	FORCEINLINE const uint8* GetData(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetData(Register);
	}

	// Returns the current data pointer for a given register.
	// Note: This refers to the active slice - and can change over time.
	FORCEINLINE uint8* GetData(FRigVMRegister& Register, bool bMoveToNextSlice = false)
	{
		ensure(Register.ElementCount > 0);
		if(bMoveToNextSlice)
		{
			Register.MoveToNextSlice();
		}
		return &Data[Register.GetWorkByteIndex()];
	}

	// Returns the current data pointer for a given register index.
	// Note: This refers to the active slice - and can change over time.
	FORCEINLINE uint8* GetData(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetData(Register, bMoveToNextSlice);
	}

	// Returns the current const typed pointer for a given register.
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE const T* Get(const FRigVMRegister& Register) const
	{
		ensure(Register.ElementCount > 0);
		return (const T*)&Data[Register.GetWorkByteIndex()];
	}

	// Returns the current const typed pointer for a given register index.
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE const T* Get(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return Get<T>(Register);
	}

	// Returns the current const typed pointer for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE const T* Get(const FRigVMArgument& InArgument) const
	{
		return Get<T>(InArgument.GetRegisterIndex());
	}

	// Returns the current const typed reference for a given register
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE const T& GetRef(const FRigVMRegister& Register) const
	{
		return *Get<T>(Register);
	}

	// Returns the current const typed reference for a given register index
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE const T& GetRef(int32 InRegisterIndex) const
	{
		return *Get<T>(InRegisterIndex);
	}

	// Returns the current const typed reference for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE const T& GetRef(const FRigVMArgument& InArgument) const
	{
		return GetRef<T>(InArgument.GetRegisterIndex());
	}

	// Returns the current typed pointer for a given register
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE T* Get(FRigVMRegister& Register, bool bMoveToNextSlice = false)
	{
		ensure(Register.ElementCount > 0);
		if(bMoveToNextSlice)
		{
			Register.MoveToNextSlice();
		}
		return (T*)&Data[Register.GetWorkByteIndex()];
	}

	// Returns the current typed pointer for a given register index
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE T* Get(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return Get<T>(Register, bMoveToNextSlice);
	}

	// Returns the current typed pointer for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE T* Get(const FRigVMArgument& InArgument, bool bMoveToNextSlice = false)
	{
		return Get<T>(InArgument.GetRegisterIndex(), bMoveToNextSlice);
	}

	// Returns the current typed reference for a given register
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE T& GetRef(FRigVMRegister& Register, bool bMoveToNextSlice = false)
	{
		return *Get<T>(Register, bMoveToNextSlice);
	}

	// Returns the current typed reference for a given register index
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE T& GetRef(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		return *Get<T>(InRegisterIndex, bMoveToNextSlice);
	}

	// Returns the current typed reference for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<class T>
	FORCEINLINE T& GetRef(const FRigVMArgument& InArgument, bool bMoveToNextSlice = false)
	{
		return GetRef<T>(InArgument.GetRegisterIndex(), bMoveToNextSlice);
	}

	// Returns an array view for all elements of the current slice for a given register.
	template<class T>
	FORCEINLINE TArrayView<T> GetArray(FRigVMRegister& Register, bool bMoveToNextSlice = false)
	{
		ensure(Register.ElementCount > 0);
		if(bMoveToNextSlice)
		{
			Register.MoveToNextSlice();
		}
		return TArrayView<T>((T*)&Data[Register.GetWorkByteIndex()], Register.ElementCount);
	}

	// Returns an array view for all elements of the current slice for a given register index.
	template<class T>
	FORCEINLINE TArrayView<T> GetArray(int32 InRegisterIndex, bool bMoveToNextSlice = false)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetArray<T>(Register, bMoveToNextSlice);
	}
	
	// Returns an array view for all elements of the current slice for a given argument.
	template<class T>
	FORCEINLINE TArrayView<T> GetArray(const FRigVMArgument& InArgument, bool bMoveToNextSlice = false)
	{
		return GetArray<T>(InArgument.GetRegisterIndex(), bMoveToNextSlice);
	}

	// Returns the script struct used for a given register (can be nullptr for non-struct-registers).
	FORCEINLINE UScriptStruct* GetScriptStruct(const FRigVMRegister& Register) const
	{
		if (Register.ScriptStructIndex != INDEX_NONE)
		{
			ensure(ScriptStructs.IsValidIndex(Register.ScriptStructIndex));
			return ScriptStructs[Register.ScriptStructIndex];
		}
		return nullptr;
	}

	// Returns the script struct used for a given register index (can be nullptr for non-struct-registers).
	FORCEINLINE UScriptStruct* GetScriptStruct(int32 InRegisterIndex) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetScriptStruct(Register);
	}

	// Copies the content of a source register to a target register.
	// The source register can optionally be referencing a specific source memory container.
	bool Copy(
		int32 InSourceRegisterIndex,
		int32 InTargetRegisterIndex,
		const FRigVMMemoryContainer* InSourceMemory = nullptr,
		int32 InSourceByteOffset = INDEX_NONE,
		int32 InTargetByteOffset = INDEX_NONE,
		int32 InNumBytes = INDEX_NONE);

	// Copies the content of a source register to a target register.
	// The source register can optionally be referencing a specific source memory container.
	// Note: This only works if SupportsNames() == true
	bool Copy(
		const FName& InSourceName,
		const FName& InTargetName,
		const FRigVMMemoryContainer* InSourceMemory = nullptr,
		int32 InSourceByteOffset = INDEX_NONE,
		int32 InTargetByteOffset = INDEX_NONE,
		int32 InNumBytes = INDEX_NONE);

	// Returns the index of a register based on the register name.
	// Note: This only works if SupportsNames() == true
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

	// Returns true if a given name is available for a new register.
	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const
	{
		if (!bUseNameMap)
		{
			return false;
		}
		return GetIndex(InPotentialNewName) == INDEX_NONE;
	}

	// Adds a new named register for a plain array from a data pointer.
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		return Allocate(InNewName, InElementSize, InCount, InSliceCount, InDataPtr);
	}

	// Adds a new named register for a typed plain array from a data pointer.
	template<class T>
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, int32 InCount, const T* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		return Allocate(InNewName, sizeof(T), InCount, InSliceCount, (const void*)InDataPtr);
	}

	// Adds a new named register for a typed plain array from a TArray.
	template<class T>
	FORCEINLINE int32 AddPlainArray(const FName& InNewName, const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddPlainArray<T>(InNewName, InArray.Num(), InArray.GetData(), InSliceCount);
	}

	// Adds a new unnamed register for a typed plain array from a TArray.
	template<class T>
	FORCEINLINE int32 AddPlainArray(const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddPlainArray<T>(NAME_None, InArray, InSliceCount);
	}

	// Adds a new named register for a plain value from a data pointer.
	FORCEINLINE int32 AddPlain(const FName& InNewName, int32 InElementSize, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddPlainArray(InNewName, InElementSize, 1, InValuePtr, InSliceCount);
	}

	// Adds a new unnamed register for a  plain value from a data pointer.
	FORCEINLINE int32 AddPlain(int32 InElementSize, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddPlain(NAME_None, InElementSize, InValuePtr, InSliceCount);
	}

	// Adds a new named register for a typed plain value from a value reference.
	template<class T>
	FORCEINLINE int32 AddPlain(const FName& InNewName, const T& InValue, int32 InSliceCount = 1)
	{
		return AddPlainArray<T>(InNewName, 1, &InValue, InSliceCount);
	}

	// Adds a new unnamed register for a typed plain value from a value reference.
	template<class T>
	FORCEINLINE int32 AddPlain(const T& InValue, int32 InSliceCount = 1)
	{
		return AddPlain<T>(NAME_None, InValue, InSliceCount);
	}

	// Adds a new named register for a struct given a scriptstruct and a data pointer.
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

	// Adds a new unnamed register for a struct given a scriptstruct and a data pointer.
	FORCEINLINE int32 AddStructArray(UScriptStruct* InScriptStruct, int32 InCount, const void* InDataPtr = nullptr, int32 InSliceCount = 1)
	{
		return AddStructArray(NAME_None, InScriptStruct, InCount, InDataPtr, InSliceCount);
	}

	// Adds a new named register for a typed struct given a data pointer.
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

	// Adds a new named register for a typed struct array given a value reference.
	template<class T>
	FORCEINLINE int32 AddStructArray(const FName& InNewName, const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddStructArray<T>(InNewName, InArray.Num(), InArray.GetData(), InSliceCount);
	}

	// Adds a new named register for a typed struct array given a TArray
	template<class T>
	FORCEINLINE int32 AddStructArray(const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddStructArray<T>(NAME_None, InArray, InSliceCount);
	}

	// Adds a new named register for a struct given a scriptstruct and a value ptr
	FORCEINLINE int32 AddStruct(const FName& InNewName, UScriptStruct* InScriptStruct, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddStructArray(InNewName, InScriptStruct, 1, InValuePtr, InSliceCount);
	}

	// Adds a new unnamed register for a struct given a scriptstruct and value ptr
	FORCEINLINE int32 AddStruct(UScriptStruct* InScriptStruct, const void* InValuePtr, int32 InSliceCount = 1)
	{
		return AddStruct(NAME_None, InScriptStruct, InValuePtr, InSliceCount);
	}

	// Adds a new named register for a typed struct given a value reference
	template<class T>
	FORCEINLINE int32 AddStruct(const FName& InNewName, const T& InValue, int32 InSliceCount = 1)
	{
		return AddStructArray<T>(InNewName, 1, &InValue, InSliceCount);
	}

	// Adds a new unnamed register for a typed struct given a value reference
	template<class T>
	FORCEINLINE int32 AddStruct(const T& InValue, int32 InSliceCount = 1)
	{
		return AddStruct<T>(NAME_None, InValue, InSliceCount);
	}

	// Remove a register given its index
	bool Remove(int32 InRegisterIndex);

	// Remove a register given its name
	// Note: This only works if SupportsNames() == true
	bool Remove(const FName& InRegisterName);

	// Remove a register given its index
	// Note: This only works if SupportsNames() == true
	FName Rename(int32 InRegisterIndex, const FName& InNewName);

	// Remove a register given its old name
	// Note: This only works if SupportsNames() == true
	FName Rename(const FName& InOldName, const FName& InNewName);

	// Resize a register given its index
	bool Resize(int32 InRegisterIndex, int32 InNewElementCount, int32 InNewSliceCount = 1);

	// Resize a register given its name
	// Note: This only works if SupportsNames() == true
	bool Resize(const FName& InRegisterName, int32 InNewElementCount, int32 InNewSliceCount = 1);

	// Changes the type of a register provided a new type and a data pointer.
	// This can be used to store different content a register for re-using the memory,
	// but this only works for new elements smaller or of the same size (since the register can't grow).
	bool ChangeRegisterType(int32 InRegisterIndex, ERigVMRegisterType InNewType, int32 InElementSize, const void* InDataPtr = nullptr, int32 InNewElementCount = 1, int32 InNewSliceCount = 1);

	// Changes the type of a register provided a new type and a data pointer.
	// This can be used to store different content a register for re-using the memory,
	// but this only works for new elements smaller or of the same size (since the register can't grow).
	template<class T>
	FORCEINLINE bool ChangeRegisterType(int32 InRegisterIndex, const T* InDataPtr = nullptr, int32 InNewElementCount = 1, int32 InNewSliceCount = 1)
	{
		return ChangeRegisterType(InRegisterIndex, ERigVMRegisterType::Plain, sizeof(T), InDataPtr, InNewElementCount, InNewSliceCount);
	}

private:

	// Updates internal data for topological changes
	void UpdateRegisters();

	// Allocates a new named register
	int32 Allocate(const FName& InNewName, int32 InElementSize, int32 InElementCount, int32 InSliceCount, const void* InDataPtr = nullptr, bool bUpdateRegisters = true);

	// Allocates a new unnamed register
	int32 Allocate(int32 InElementSize, int32 InElementCount, int32 InSliceCount, const void* InDataPtr = nullptr, bool bUpdateRegisters = true);

	// Performs optional construction of data within a struct register
	bool Construct(int32 InRegisterIndex, int32 InElementIndex = INDEX_NONE);

	// Performs optional destruction of data within a struct register
	bool Destroy(int32 InRegisterIndex, int32 InElementIndex = INDEX_NONE);

	// Fills a register with zero memory
	void FillWithZeroes(int32 InRegisterIndex);

	// Ensures to add a script struct to the internal map if needed
	int32 FindOrAddScriptStruct(UScriptStruct* InScriptStruct);

	UPROPERTY()
	bool bUseNameMap;

	UPROPERTY()
	ERigVMMemoryType MemoryType;

	UPROPERTY()
	TArray<FRigVMRegister> Registers;

	UPROPERTY(transient)
	TArray<uint8> Data;

	UPROPERTY()
	TArray<UScriptStruct*> ScriptStructs;

	UPROPERTY(transient)
	TMap<FName, int32> NameMap;
};

template<>
FORCEINLINE bool FRigVMMemoryContainer::ChangeRegisterType(int32 InRegisterIndex, const FName* InDataPtr, int32 InNewElementCount, int32 InNewSliceCount)
{
	if (!ChangeRegisterType(InRegisterIndex, ERigVMRegisterType::Name, sizeof(FName), nullptr, InNewElementCount, InNewSliceCount))
	{
		return false;
	}

	if (InDataPtr)
	{
		Registers[InRegisterIndex].MoveToFirstSlice();
		for (uint16 SliceIndex = 0; SliceIndex < Registers[InRegisterIndex].SliceCount; SliceIndex++)
		{
			FName* DataPtr = (FName*)GetData(InRegisterIndex);
			for (int32 Index = 0; Index < InNewElementCount; Index++)
			{
				DataPtr[Index] = InDataPtr[Index];
			}
			Registers[InRegisterIndex].MoveToNextSlice();
		}
		Registers[InRegisterIndex].MoveToFirstSlice();
	}

	return true;
}

template<>
FORCEINLINE bool FRigVMMemoryContainer::ChangeRegisterType(int32 InRegisterIndex, const FString* InDataPtr, int32 InNewElementCount, int32 InNewSliceCount)
{
	if (!ChangeRegisterType(InRegisterIndex, ERigVMRegisterType::String, sizeof(FString), nullptr, InNewElementCount, InNewSliceCount))
	{
		return false;
	}

	if (InDataPtr)
	{
		Registers[InRegisterIndex].MoveToFirstSlice();
		for (uint16 SliceIndex = 0; SliceIndex < Registers[InRegisterIndex].SliceCount; SliceIndex++)
		{
			FString* DataPtr = (FString*)GetData(InRegisterIndex);
			for (int32 Index = 0; Index < InNewElementCount; Index++)
			{
				DataPtr[Index] = InDataPtr[Index];
			}
			Registers[InRegisterIndex].MoveToNextSlice();
		}
		Registers[InRegisterIndex].MoveToFirstSlice();
	}

	return true;
}

template<>
FORCEINLINE int32 FRigVMMemoryContainer::AddPlainArray(const FName& InNewName, int32 InCount, const FName* InDataPtr, int32 InSliceCount)
{
	int32 Register = Allocate(InNewName, sizeof(FName), InCount, InSliceCount, nullptr);
	Registers[Register].Type = ERigVMRegisterType::Name;

	Construct(Register);

	if (InDataPtr)
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

template<>
FORCEINLINE int32 FRigVMMemoryContainer::AddPlainArray(const FName& InNewName, int32 InCount, const FString* InDataPtr, int32 InSliceCount)
{
	int32 Register = Allocate(InNewName, sizeof(FString), InCount, InSliceCount, nullptr);
	Registers[Register].Type = ERigVMRegisterType::String;

	Construct(Register);

	if (InDataPtr)
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

typedef FRigVMMemoryContainer* FRigVMMemoryContainerPtr;
typedef TArrayView<FRigVMMemoryContainerPtr> FRigVMMemoryContainerPtrArray;