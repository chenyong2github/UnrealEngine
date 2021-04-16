// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMTraits.h"
#include "RigVMStatistics.h"
#include "RigVMArray.h"
#include "RigVMMemory.generated.h"

#ifdef UE_BUILD_DEBUG
	#define DEBUG_RIGVMMEMORY 0
	//#define DEBUG_RIGVMMEMORY WITH_EDITOR
#else
	#define DEBUG_RIGVMMEMORY 0
#endif

#if DEBUG_RIGVMMEMORY
	RIGVM_API DECLARE_LOG_CATEGORY_EXTERN(LogRigVMMemory, Log, All);
	#define UE_LOG_RIGVMMEMORY(Format, ...) UE_LOG(LogRigVMMemory, Display, (Format), ##__VA_ARGS__)
#else
	#define UE_LOG_RIGVMMEMORY(Format, ...)
#endif

/**
 * The type of memory used. Typically we differentiate between
 * Work (Mutable) and Literal (Constant) memory.
 */
UENUM()
enum class ERigVMMemoryType: uint8
{
	Work, // Mutable state
	Literal, // Const / fixed state
	External, // Unowned external memory
	Invalid
};

/**
 * The FRigVMOperand represents an argument used for an operator
 * within the virtual machine. Operands provide information about
 * which memory needs to be referred to, which register within the
 * memory all the way to the actual byte address in memory.
 * The FRigVMOperand is a light weight address for a register in
 * a FRigVMMemoryContainer.
 */
USTRUCT()
struct RIGVM_API FRigVMOperand
{
	GENERATED_BODY()

public:

	FRigVMOperand()
		: MemoryType(ERigVMMemoryType::Work)
		, RegisterIndex(UINT16_MAX)
		, RegisterOffset(UINT16_MAX)
	{
	}

	FRigVMOperand(ERigVMMemoryType InMemoryType, int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE)
		: MemoryType(InMemoryType)
		, RegisterIndex(InRegisterIndex < 0 ? UINT16_MAX : (uint16)InRegisterIndex)
		, RegisterOffset(InRegisterOffset < 0 ? UINT16_MAX : (uint16)InRegisterOffset)
	{
	}

	FORCEINLINE_DEBUGGABLE bool operator == (const FRigVMOperand& InOther) const
	{
		return MemoryType == InOther.MemoryType &&
			RegisterIndex == InOther.RegisterIndex &&
			RegisterOffset == InOther.RegisterOffset;
	}

	FORCEINLINE_DEBUGGABLE bool operator != (const FRigVMOperand& InOther) const
	{
		return !(*this == InOther);
	}

	// returns the memory type of this argument
	FORCEINLINE_DEBUGGABLE bool IsValid() const { return RegisterIndex != UINT16_MAX; }

	// returns the memory type of this argument
	FORCEINLINE_DEBUGGABLE ERigVMMemoryType GetMemoryType() const { return MemoryType; }

	// returns the index of the container of this argument
	FORCEINLINE_DEBUGGABLE int32 GetContainerIndex() const { return (int32)MemoryType; }

	// returns the index of the register of this argument
	FORCEINLINE_DEBUGGABLE int32 GetRegisterIndex() const { return RegisterIndex == UINT16_MAX ? INDEX_NONE : (int32)RegisterIndex; }

	// returns the index of the register of this argument
	FORCEINLINE_DEBUGGABLE int32 GetRegisterOffset() const { return RegisterOffset == UINT16_MAX ? INDEX_NONE : (int32)RegisterOffset; }

	bool Serialize(FArchive& Ar);
	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FRigVMOperand& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

private:

	UPROPERTY()
	ERigVMMemoryType MemoryType;

	UPROPERTY()
	uint16 RegisterIndex;
	
	UPROPERTY()
	uint16 RegisterOffset;
};

typedef FRigVMFixedArray<FRigVMOperand> FRigVMOperandArray;

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
// the memory - so for example if your register stores 4 Vectors, then a slice 
// would contain 48 bytes (4 * 3 * 4). The register can however store multiple
// slices / copies of that if needed. Slices can be used to provide 
// per-invocation memory to functions within the same register.
// An integrator for example that needs to store a simulated position
// might want access to a separate memory per loop iteration.
USTRUCT()
struct RIGVM_API FRigVMRegister
{
	GENERATED_BODY()

	FRigVMRegister()
		: Type(ERigVMRegisterType::Invalid)
		, ByteIndex(INDEX_NONE)
		, ElementSize(0)
		, ElementCount(0)
		, SliceCount(1)
		, AlignmentBytes(0)
		, TrailingBytes(0)
		, Name(NAME_None)
		, ScriptStructIndex(INDEX_NONE)
		, bIsArray(false)
		, bIsDynamic(false)
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

	// The number of slices (complete copies)
	UPROPERTY()
	uint16 SliceCount;

	// The number of leading bytes for alignment
	UPROPERTY()
	uint8 AlignmentBytes;

	// The number of trailing bytes.
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

	// If true defines this register as an array
	UPROPERTY()
	bool bIsArray;

	// If true defines this register to use dynamic storage
	UPROPERTY()
	bool bIsDynamic;

	bool Serialize(FArchive& Ar);
	FORCEINLINE_DEBUGGABLE friend FArchive& operator<<(FArchive& Ar, FRigVMRegister& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	// returns true if this register is using a dynamic array for storage
	FORCEINLINE_DEBUGGABLE bool IsDynamic() const { return bIsDynamic; }

	// returns true if this register is using a dynamic array for storage
	FORCEINLINE_DEBUGGABLE bool IsNestedDynamic() const { return bIsDynamic && bIsArray; }

	// returns the current address of the register within the data byte array.
	// this can change over time - as the register is moving through slices.
	// use GetFirstAllocatedByte to get the fixed first byte.
	FORCEINLINE_DEBUGGABLE uint64 GetWorkByteIndex(int32 InSliceIndex = 0) const
	{
		ensure(InSliceIndex >= 0);
		return ByteIndex + ((uint64)InSliceIndex * GetNumBytesPerSlice());
	}

	// returns the first allocated byte in the data byte array
	FORCEINLINE_DEBUGGABLE uint64 GetFirstAllocatedByte() const
	{ 
		return ByteIndex - (uint64)AlignmentBytes;
	}

	// Returns the leading alignment bytes
	FORCEINLINE_DEBUGGABLE uint8 GetAlignmentBytes() const { return AlignmentBytes; }

	// Returns true if the register stores more than one element
	FORCEINLINE_DEBUGGABLE bool IsArray() const { return bIsArray || (ElementCount > 1); }

	// Returns true if the register stores shallow memory
	FORCEINLINE_DEBUGGABLE bool IsShallow() const { return ScriptStructIndex == INDEX_NONE; }

	// Returns the number of allocated bytes (including alignment + trailing bytes)
	FORCEINLINE_DEBUGGABLE uint16 GetAllocatedBytes() const { return ElementCount * ElementSize * SliceCount + (uint16)AlignmentBytes + TrailingBytes; }

	// Returns the number of bytes for a complete slice
	FORCEINLINE_DEBUGGABLE uint16 GetNumBytesPerSlice() const { return ElementCount * ElementSize; }

	// Returns the number of bytes for all slices
	FORCEINLINE_DEBUGGABLE uint16 GetNumBytesAllSlices() const { return ElementCount * ElementSize * SliceCount; }

	// Returns the total number of elements (elementcount * slicecount) in the register
	FORCEINLINE_DEBUGGABLE uint32 GetTotalElementCount() const { return (uint32)ElementCount * (uint32)SliceCount; }

};

typedef FRigVMFixedArray<FRigVMRegister> FRigVMRegisterArray;

// The register offset represents a memory offset within a register's memory.
// This can be used to represent memory addresses of array elements within
// a struct, for example.
USTRUCT()
struct RIGVM_API FRigVMRegisterOffset
{
	GENERATED_BODY()

public:

	// default constructor
	FRigVMRegisterOffset()
		: Segments()
		, Type(ERigVMRegisterType::Invalid)
		, CPPType(NAME_None)
		, ScriptStruct(nullptr)
		, ParentScriptStruct(nullptr)
		, ArrayIndex(0)
		, ElementSize(0)
		, CachedSegmentPath() 
	{
	}

	bool Serialize(FArchive& Ar);
	FORCEINLINE_DEBUGGABLE friend FArchive& operator<<(FArchive& Ar, FRigVMRegisterOffset& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	// constructs a path given a struct and a segment path
	FRigVMRegisterOffset(UScriptStruct* InScriptStruct, const FString& InSegmentPath, int32 InInitialOffset = 0, uint16 InElementSize = 0);

	// returns the data pointer within a container
	uint8* GetData(uint8* InContainer) const;

	// returns the segments of this path
	const TArray<int32>& GetSegments() const { return Segments; }

	bool operator == (const FRigVMRegisterOffset& InOther) const;

	FORCEINLINE_DEBUGGABLE ERigVMRegisterType GetType() const { return Type; }
	FORCEINLINE_DEBUGGABLE FName GetCPPType() const { return CPPType; }
	uint16 GetElementSize() const;
	void SetElementSize(uint16 InElementSize) { ElementSize = InElementSize; };
	UScriptStruct* GetScriptStruct() const;

private:

	UPROPERTY()
	TArray<int32> Segments;

	UPROPERTY()
	ERigVMRegisterType Type;

	UPROPERTY()
	FName CPPType;

	UPROPERTY()
	UScriptStruct* ScriptStruct;

	UPROPERTY()
	UScriptStruct* ParentScriptStruct;

	UPROPERTY()
	int32 ArrayIndex;

	UPROPERTY()
	uint16 ElementSize;

	UPROPERTY()
	FString CachedSegmentPath;

	friend struct FRigVMRegisterOffsetBuilder;
	friend class URigVM;
};

/**
 * The FRigVMMemoryHandle is used to access the memory used within a FRigMemoryContainer.
 */
struct FRigVMMemoryHandle
{
public:
	enum FType
	{
		Plain,
		Dynamic,
		NestedDynamic
	};

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle()
		: Ptr(nullptr)
		, Type(FType::Plain)
		, Size(1)
		, Offset(UINT16_MAX)
	{}

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle(uint8* InPtr, uint16 InSize = 1, FType InType = FType::Plain, int32 InRegisterOffset = INDEX_NONE)
		: Ptr(InPtr)
		, Type(InType)
		, Size(InSize)
		, Offset((uint16)InRegisterOffset)
	{}

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle(uint8* InPtr, const FRigVMRegister& InRegister, int32 InRegisterOffset = INDEX_NONE)
		: Ptr(InPtr)
		, Type(FType::Plain)
		, Size(InRegister.ElementSize)
		, Offset((uint16)InRegisterOffset)
	{
		if (InRegister.IsNestedDynamic())
		{
			Type = FType::NestedDynamic;
		}
		else if (InRegister.IsDynamic())
		{
			Type = FType::Dynamic;
		}
		else
		{
			Size = (uint16)InRegister.GetNumBytesPerSlice();
		}
	}

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle(FRigVMByteArray* InPtr, uint16 InSize = 1, int32 InRegisterOffset = INDEX_NONE)
		: Ptr((uint8*)InPtr)
		, Type(FType::Dynamic)
		, Size(InSize)
		, Offset((uint16)InRegisterOffset)
	{}

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle(FRigVMNestedByteArray* InPtr, uint16 InSize = 1, int32 InRegisterOffset = INDEX_NONE)
		: Ptr((uint8*)InPtr)
		, Type(FType::NestedDynamic)
		, Size(InSize)
		, Offset((uint16)InRegisterOffset)
	{}

	FORCEINLINE_DEBUGGABLE operator const uint8*() const
	{
		return GetData();
	}

	FORCEINLINE_DEBUGGABLE operator uint8*()
	{
		return GetData();
	}

	FORCEINLINE_DEBUGGABLE const uint8* GetData(int32 SliceIndex = 0, bool bGetArrayData = false) const
	{
		return GetData_Internal(SliceIndex, bGetArrayData);
	}

	FORCEINLINE_DEBUGGABLE uint8* GetData(int32 SliceIndex = 0, bool bGetArrayData = false)
	{
		return GetData_Internal(SliceIndex, bGetArrayData);
	}

private:

	FORCEINLINE_DEBUGGABLE uint8* GetData_Internal(int32 SliceIndex, bool bGetArrayData = false) const
	{
		ensure(SliceIndex >= 0);

		if (Size == 0 || Ptr == nullptr)
		{
			return nullptr;
		}

		switch (Type)
		{
			case FType::Plain:
			{
				return Ptr + SliceIndex * Size;
			}
			case FType::Dynamic:
			{
				ensure(Offset == UINT16_MAX);
				if (!bGetArrayData)
				{
					return Ptr;
				}

				FRigVMByteArray* ArrayStorage = (FRigVMByteArray*)Ptr;
				if (ArrayStorage->Num() == 0)
				{
					return nullptr;
				}

				if (SliceIndex > 0)
				{
					SliceIndex = FMath::Max(SliceIndex, (ArrayStorage->Num() / Size) - 1);
				}
				return (uint8*)&(*ArrayStorage)[SliceIndex * Size];
			}
			case FType::NestedDynamic:
			{
				ensure(Offset == UINT16_MAX);

				FRigVMNestedByteArray* ArrayStorage = (FRigVMNestedByteArray*)Ptr;
				if (ArrayStorage->Num() == 0 && bGetArrayData)
				{
					return nullptr;
				}

				if (SliceIndex > 0)
				{
					SliceIndex = FMath::Max(SliceIndex, ArrayStorage->Num() - 1);
				}

				if (!bGetArrayData)
				{
					return Ptr;
				}

				return (*ArrayStorage)[SliceIndex].GetData();
			}
			default:
			{
				return nullptr;
			}
		}
	}

	uint8* Ptr;
	FType Type;
	uint16 Size;
	uint16 Offset;

	friend class URigVM;
};

/**
 * The FRigVMMemoryContainer provides a heterogeneous memory container to store arbitrary
 * data. Each element stored can be referred to using a FRigVMRegister.
 * Elements can be accessed by index (index of the register), FRigVMOperand or by name.
 * Name access is optional and is specified upon construction of the container.
 * The memory container provides a series of templated functions to add and get data.
 *
 * For example:
 * 		int32 Index = Container.Add<float>(4.f);
 *      float& ValueRef = Container.GetRef<float>(Index);
 *
 * This can also be done with arrays:
 *      TArray<float> MyArray = {3.f, 4.f, 5.f};
 * 		int32 Index = Container.AddFixedArray<float>(MyArray);
 *      FRigVMFixedArray<float> ArrayView = Container.GetFixedArray<float>(Index);
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
	FORCEINLINE_DEBUGGABLE ERigVMMemoryType GetMemoryType() const { return MemoryType;  }

	// sets the memory type. should only be used when the container is empty
	FORCEINLINE_DEBUGGABLE void SetMemoryType(ERigVMMemoryType InMemoryType) { MemoryType = InMemoryType; }

	// returns true if this container supports name based lookup
	FORCEINLINE_DEBUGGABLE bool SupportsNames() const { return bUseNameMap;  }

	// returns the number of registers in this container
	FORCEINLINE_DEBUGGABLE int32 Num() const { return Registers.Num(); }

	// resets the container but maintains storage.
	void Reset();

	// resets the container and removes all storage.
	void Empty();

	// const accessor for a register based on index
	FORCEINLINE_DEBUGGABLE const FRigVMRegister& operator[](int32 InIndex) const { return GetRegister(InIndex); }

	// accessor for a register based on index
	FORCEINLINE_DEBUGGABLE FRigVMRegister& operator[](int32 InIndex) { return GetRegister(InIndex); }

	// const accessor for a register based on an argument
	FORCEINLINE_DEBUGGABLE const FRigVMRegister& operator[](const FRigVMOperand& InArg) const { return GetRegister(InArg); }

	// accessor for a register based on an argument
	FORCEINLINE_DEBUGGABLE FRigVMRegister& operator[](const FRigVMOperand& InArg) { return GetRegister(InArg); }

	// const accessor for a register based on a a name. note: only works if SupportsNames() == true
	FORCEINLINE_DEBUGGABLE const FRigVMRegister& operator[](const FName& InName) const { return GetRegister(InName); }

	// accessor for a register based on a a name. note: only works if SupportsNames() == true
	FORCEINLINE_DEBUGGABLE FRigVMRegister& operator[](const FName& InName) { return GetRegister(InName); }

	FORCEINLINE_DEBUGGABLE TArray<FRigVMRegister>::RangedForIteratorType      begin() { return Registers.begin(); }
	FORCEINLINE_DEBUGGABLE TArray<FRigVMRegister>::RangedForConstIteratorType begin() const { return Registers.begin(); }
	FORCEINLINE_DEBUGGABLE TArray<FRigVMRegister>::RangedForIteratorType      end() { return Registers.end(); }
	FORCEINLINE_DEBUGGABLE TArray<FRigVMRegister>::RangedForConstIteratorType end() const { return Registers.end(); }

	// const accessor for a register based on index
	FORCEINLINE_DEBUGGABLE const FRigVMRegister& GetRegister(int32 InIndex) const { return Registers[InIndex]; }

	// accessor for a register based on index
	FORCEINLINE_DEBUGGABLE FRigVMRegister& GetRegister(int32 InIndex) { return Registers[InIndex]; }

	// const accessor for a register based on an argument
	FORCEINLINE_DEBUGGABLE const FRigVMRegister& GetRegister(const FRigVMOperand& InArg) const { return Registers[InArg.GetRegisterIndex()]; }

	// accessor for a register based on an argument
	FORCEINLINE_DEBUGGABLE FRigVMRegister& GetRegister(const FRigVMOperand& InArg) { return Registers[InArg.GetRegisterIndex()]; }

	// const accessor for a register based on a a name. note: only works if SupportsNames() == true
	FORCEINLINE_DEBUGGABLE const FRigVMRegister& GetRegister(const FName& InName) const { return Registers[GetIndex(InName)]; }

	// accessor for a register based on a a name. note: only works if SupportsNames() == true
	FORCEINLINE_DEBUGGABLE FRigVMRegister& GetRegister(const FName& InName) { return Registers[GetIndex(InName)]; }

	bool Serialize(FArchive& Ar);
	FORCEINLINE_DEBUGGABLE friend FArchive& operator<<(FArchive& Ar, FRigVMMemoryContainer& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	// Returns an argument for a given register.
	// This is typically used to store a light weight address for use within a VM.
	FORCEINLINE_DEBUGGABLE FRigVMOperand GetOperand(int32 InRegisterIndex, int32 InRegisterOffset)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		return FRigVMOperand(MemoryType, InRegisterIndex, InRegisterOffset);
	}

	// Returns an argument for a given register.
	// This is typically used to store a light weight address for use within a VM.
	FORCEINLINE_DEBUGGABLE FRigVMOperand GetOperand(int32 InRegisterIndex, const FString& InSegmentPath = FString(), int32 InArrayElement = INDEX_NONE)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		// Register offset must hold on to the ScriptStruct such that it can recalculate the struct size after cook
		UScriptStruct* ScriptStruct = GetScriptStruct(InRegisterIndex);

		int32 InitialOffset = 0;
		int32 ElementSize = 0;
		if (InArrayElement != INDEX_NONE)
		{
			InitialOffset = InArrayElement * Registers[InRegisterIndex].ElementSize;
			ElementSize = Registers[InRegisterIndex].ElementSize;
		}

		return GetOperand(InRegisterIndex, GetOrAddRegisterOffset(InRegisterIndex, ScriptStruct, InSegmentPath, InitialOffset, ElementSize));
	}

private:

	FORCEINLINE_DEBUGGABLE uint8* GetDataPtr(const FRigVMRegister& Register, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0, bool bArrayContent = false) const
	{
		if (Register.ElementCount == 0 && !Register.IsNestedDynamic())
		{
			return nullptr;
		}

		uint8* Ptr = nullptr;
		if (Register.IsDynamic())
		{
			Ptr = (uint8*)&Data[Register.GetWorkByteIndex()];

			if (Register.IsNestedDynamic())
			{
				FRigVMNestedByteArray* ArrayStorage = (FRigVMNestedByteArray*)Ptr;
				Ptr = (uint8*)ArrayStorage->GetData();

				if (Ptr)
				{
					Ptr = Ptr + InSliceIndex * sizeof(FRigVMByteArray);
					if (Ptr && bArrayContent)
					{
						Ptr = ((FRigVMByteArray*)Ptr)->GetData();
					}
				}
			}
			else if(bArrayContent)
			{
				FRigVMByteArray* ArrayStorage = (FRigVMByteArray*)Ptr;
				Ptr = (uint8*)ArrayStorage->GetData();

				if (Ptr)
				{
					Ptr = Ptr + InSliceIndex * Register.GetNumBytesPerSlice();
				}
			}
		}
		else
		{
			Ptr = (uint8*)&Data[Register.GetWorkByteIndex(InSliceIndex)];
		}

		if (InRegisterOffset != INDEX_NONE && Ptr != nullptr)
		{
			Ptr = RegisterOffsets[InRegisterOffset].GetData(Ptr);
		}
		return Ptr;
	}

public:

	// Returns a memory handle for a given register
	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle GetHandle(const FRigVMRegister& Register, int32 InRegisterOffset = INDEX_NONE) const
	{
		if (Register.IsDynamic())
		{
			uint8* Ptr = (uint8*)&Data[Register.GetWorkByteIndex()];
			return FRigVMMemoryHandle(Ptr, Register);
		}

		uint8* Ptr = GetDataPtr(Register, InRegisterOffset);
		return FRigVMMemoryHandle(Ptr, Register.GetNumBytesPerSlice());
	}

	// Returns a memory handle for a given register
	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle GetHandle(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetHandle(Register, InRegisterOffset);
	}

	// Returns the number of elements for a given slice
	FORCEINLINE_DEBUGGABLE int32 GetNumElements(const FRigVMRegister& Register, int32 InSliceIndex = 0) const
	{
		if (!Register.IsDynamic())
		{
			return Register.ElementCount;
		}
		
		if (Register.IsNestedDynamic())
		{
			FRigVMNestedByteArray& ArrayStorage = *(FRigVMNestedByteArray*)&Data[Register.GetWorkByteIndex()];
			return ArrayStorage[InSliceIndex].Num() / Register.ElementSize;
		}

		return 1;
	}

	// Returns the number of elements for a given slice
	FORCEINLINE_DEBUGGABLE int32 GetNumElements(int32 InRegisterIndex, int32 InSliceIndex = 0) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetNumElements(Register, InSliceIndex);
	}

	// Returns the current const data pointer for a given register.
	FORCEINLINE_DEBUGGABLE const uint8* GetData(const FRigVMRegister& Register, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0) const
	{
		return GetHandle(Register, InRegisterOffset).GetData(InSliceIndex, true);
	}

	// Returns the current const data pointer for a given register index.
	FORCEINLINE_DEBUGGABLE const uint8* GetData(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetData(Register, InRegisterOffset, InSliceIndex);
	}

	// Returns the current data pointer for a given register.
	FORCEINLINE_DEBUGGABLE uint8* GetData(FRigVMRegister& Register, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		return GetHandle(Register, InRegisterOffset).GetData(InSliceIndex, true);
	}

	// Returns the current data pointer for a given register index.
	// Note: This refers to the active slice - and can change over time.
	FORCEINLINE_DEBUGGABLE uint8* GetData(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetData(Register, InRegisterOffset, InSliceIndex);
	}

	// Returns the current const typed pointer for a given register.
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE const T* Get(const FRigVMRegister& InRegister, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0) const
	{
		return (const T*)GetData(InRegister, InRegisterOffset, InSliceIndex);
	}

	// Returns the current const typed pointer for a given register index.
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE const T* Get(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0) const
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		const FRigVMRegister& Register = Registers[InRegisterIndex];
		return Get<T>(Register, InRegisterOffset, InSliceIndex);
	}

	// Returns the current const typed pointer for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE const T* Get(const FRigVMOperand& InOperand, int32 InSliceIndex = 0) const
	{
		return Get<T>(InOperand.GetRegisterIndex(), InOperand.GetRegisterOffset(), InSliceIndex);
	}

	// Returns the current const typed reference for a given register
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE const T& GetRef(const FRigVMRegister& Register, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0) const
	{
		return *Get<T>(Register, InRegisterOffset, InSliceIndex);
	}

	// Returns the current const typed reference for a given register index
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE const T& GetRef(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0) const
	{
		return *Get<T>(InRegisterIndex, InRegisterOffset, InSliceIndex);
	}

	// Returns the current const typed reference for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE const T& GetRef(const FRigVMOperand& InOperand, int32 InSliceIndex = 0) const
	{
		return GetRef<T>(InOperand.GetRegisterIndex(), InOperand.GetRegisterOffset(), InSliceIndex);
	}

	// Returns the current typed pointer for a given register
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE T* Get(FRigVMRegister& InRegister, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		return (T*)GetData(InRegister, InRegisterOffset, InSliceIndex);
	}

	// Returns the current typed pointer for a given register index
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE T* Get(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return Get<T>(Register, InRegisterOffset, InSliceIndex);
	}

	// Returns the current typed pointer for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE T* Get(const FRigVMOperand& InOperand, int32 InSliceIndex = 0)
	{
		return Get<T>(InOperand.GetRegisterIndex(), InOperand.GetRegisterOffset(), InSliceIndex);
	}

	// Returns the current typed reference for a given register
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE T& GetRef(FRigVMRegister& Register, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		return *Get<T>(Register, InRegisterOffset, InSliceIndex);
	}

	// Returns the current typed reference for a given register index
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE T& GetRef(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		return *Get<T>(InRegisterIndex, InRegisterOffset, InSliceIndex);
	}

	// Returns the current typed reference for a given argument
	// Note: This refers to the active slice - and can change over time.
	template<typename T>
	FORCEINLINE_DEBUGGABLE T& GetRef(const FRigVMOperand& InOperand, int32 InSliceIndex = 0)
	{
		return GetRef<T>(InOperand.GetRegisterIndex(), InOperand.GetRegisterOffset(), InSliceIndex);
	}

	// Returns an array view for all elements of the current slice for a given register.
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray<T> GetFixedArray(FRigVMRegister& InRegister, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		if (InRegisterOffset == INDEX_NONE)
		{
			uint8* Ptr = (uint8*)GetDataPtr(InRegister, InRegisterOffset, InSliceIndex, false);
			if (InRegister.IsNestedDynamic())
			{
				FRigVMByteArray* Storage = (FRigVMByteArray*)Ptr;
				return FRigVMFixedArray<T>(*Storage);
			}
			else if (InRegister.IsDynamic())
			{
				FRigVMByteArray* Storage = (FRigVMByteArray*)Ptr;
				Ptr = Storage->GetData() + InSliceIndex * InRegister.GetNumBytesPerSlice();
			}

			return FRigVMFixedArray<T>((T*)Ptr, InRegister.ElementCount);
		}

		TArray<T>* StoredArray = (TArray<T>*)GetData(InRegister, InRegisterOffset, InSliceIndex);
		return FRigVMFixedArray<T>(StoredArray->GetData(), StoredArray->Num());
	}

	// Returns an array view for all elements of the current slice for a given register index.
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray<T> GetFixedArray(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE, int32 InSliceIndex = 0)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetFixedArray<T>(Register, InRegisterOffset, InSliceIndex);
	}
	
	// Returns an array view for all elements of the current slice for a given argument.
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMFixedArray<T> GetFixedArray(const FRigVMOperand& InOperand, int32 InSliceIndex = 0)
	{
		return GetFixedArray<T>(InOperand.GetRegisterIndex(), InOperand.GetRegisterOffset(), InSliceIndex);
	}

	// Returns an array view for all elements of the current slice for a given register.
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMDynamicArray<T> GetDynamicArray(FRigVMRegister& InRegister, int32 InSliceIndex = 0)
	{
		if (!InRegister.IsDynamic())
		{
			return FRigVMDynamicArray<T>(DefaultByteArray);
		}
		
		FRigVMByteArray* ArrayStorage = (FRigVMByteArray*)GetDataPtr(InRegister, INDEX_NONE, 0, false);
		return FRigVMDynamicArray<T>(*ArrayStorage);
	}

	// Returns an array view for all elements of the current slice for a given register index.
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMDynamicArray<T> GetDynamicArray(int32 InRegisterIndex, int32 InSliceIndex = 0)
	{
		ensure(Registers.IsValidIndex(InRegisterIndex));
		FRigVMRegister& Register = Registers[InRegisterIndex];
		return GetDynamicArray<T>(Register, InSliceIndex);
	}

	// Returns an array view for all elements of the current slice for a given argument.
	template<typename T>
	FORCEINLINE_DEBUGGABLE FRigVMDynamicArray<T> GetDynamicArray(const FRigVMOperand& InOperand, int32 InSliceIndex = 0)
	{
		return GetDynamicArray<T>(InOperand.GetRegisterIndex(), InOperand.GetRegisterOffset(), InSliceIndex);
	}

	// Returns the script struct used for a given register (can be nullptr for non-struct-registers).
	FORCEINLINE_DEBUGGABLE UScriptStruct* GetScriptStruct(const FRigVMRegister& Register) const
	{
		if (Register.ScriptStructIndex != INDEX_NONE)
		{
			ensure(ScriptStructs.IsValidIndex(Register.ScriptStructIndex));
			return ScriptStructs[Register.ScriptStructIndex];
		}
		return nullptr;
	}

	// Returns the script struct used for a given register index (can be nullptr for non-struct-registers).
	FORCEINLINE_DEBUGGABLE UScriptStruct* GetScriptStruct(int32 InRegisterIndex, int32 InRegisterOffset = INDEX_NONE) const
	{
		if (InRegisterOffset == INDEX_NONE)
		{
			ensure(Registers.IsValidIndex(InRegisterIndex));
			const FRigVMRegister& Register = Registers[InRegisterIndex];
			return GetScriptStruct(Register);
		}
		ensure(RegisterOffsets.IsValidIndex(InRegisterOffset));
		const FRigVMRegisterOffset& Path = RegisterOffsets[InRegisterOffset];
		return Path.GetScriptStruct();
	}

	// Copies the content of a source register to a target register.
	// The source register can optionally be referencing a specific source memory container.
	bool Copy(
		int32 InSourceRegisterIndex,
		int32 InTargetRegisterIndex,
		const FRigVMMemoryContainer* InSourceMemory = nullptr,
		int32 InSourceRegisterOffset = INDEX_NONE,
		int32 InTargetRegisterOffset = INDEX_NONE,
		int32 InSourceSliceIndex = 0,
		int32 InTargetSliceIndex = 0);

	// Copies the content of a source register to a target register.
	// The source register can optionally be referencing a specific source memory container.
	// Note: This only works if SupportsNames() == true
	bool Copy(
		const FName& InSourceName,
		const FName& InTargetName,
		const FRigVMMemoryContainer* InSourceMemory = nullptr,
		int32 InSourceRegisterOffset = INDEX_NONE,
		int32 InTargetRegisterOffset = INDEX_NONE,
		int32 InSourceSliceIndex = 0,
		int32 InTargetSliceIndex = 0);

	// Copies the content of a source register to a target register.
	// The source register can optionally be referencing a specific source memory container.
	bool Copy(
		const FRigVMOperand& InSourceOperand,
		const FRigVMOperand& InTargetOperand,
		const FRigVMMemoryContainer* InSourceMemory = nullptr,
		int32 InSourceSliceIndex = 0,
		int32 InTargetSliceIndex = 0);

	// Returns the index of a register based on the register name.
	// Note: This only works if SupportsNames() == true
	FORCEINLINE_DEBUGGABLE int32 GetIndex(const FName& InName) const
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
	FORCEINLINE_DEBUGGABLE bool IsNameAvailable(const FName& InPotentialNewName) const
	{
		if (!bUseNameMap)
		{
			return false;
		}
		return GetIndex(InPotentialNewName) == INDEX_NONE;
	}

	// Adds a new named register for a typed array from an array view (used by compiler)
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddFixedArray(const FName& InNewName, const FRigVMFixedArray<T>& InArrayView, int32 InSliceCount = 1)
	{
		return AddRegisterArray<T>(true, InNewName, InArrayView.Num(), true, (const uint8*)InArrayView.GetData(), InSliceCount);
	}

	// Adds a new unnamed register for a typed array from an array view.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddFixedArray(const FRigVMFixedArray<T>& InArrayView, int32 InSliceCount = 1)
	{
		return AddFixedArray<T>(NAME_None, InArrayView, InSliceCount);
	}

	// Adds a new named register for a typed array from an array view (used by compiler)
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddDynamicArray(const FName& InNewName, const FRigVMFixedArray<T>& InArrayView, int32 InSliceCount = 1)
	{
		return AddRegisterArray<T>(false, InNewName, InArrayView.Num(), true, (const uint8*)InArrayView.GetData(), InSliceCount);
	}

	// Adds a new unnamed register for a typed array from an array view.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddDynamicArray(const FRigVMFixedArray<T>& InArrayView, int32 InSliceCount = 1)
	{
		return AddDynamicArray<T>(NAME_None, InArrayView, InSliceCount);
	}

	// Adds a new named register for a typed array from an array view (used by compiler)
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddDynamicArray(const FName& InNewName, const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddDynamicArray<T>(InNewName, FRigVMFixedArray<T>(InArray), InSliceCount);
	}

	// Adds a new unnamed register for a typed array from an array view.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddDynamicArray(const TArray<T>& InArray, int32 InSliceCount = 1)
	{
		return AddDynamicArray<T>(NAME_None, InArray, InSliceCount);
	}

	// Adds a new named register for a typed value from a value reference.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 Add(const FName& InNewName, const T& InValue, int32 InSliceCount = 1)
	{
		return AddRegisterArray<T>(true, InNewName, 1, false, (const uint8*)&InValue, InSliceCount);
	}

	// Adds a new unnamed register for a typed value from a value reference.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 Add(const T& InValue, int32 InSliceCount = 1)
	{
		return Add<T>(NAME_None, InValue, InSliceCount);
	}

	// Adds a new named register for a typed value from a value reference.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddDynamicValue(const FName& InNewName, const T& InValue, int32 InSliceCount = 1)
	{
		return AddRegisterArray<T>(false, InNewName, 1, false, (const uint8*)&InValue, InSliceCount);
	}

	// Adds a new unnamed register for a typed value from a value reference.
	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddDynamicValue(const T& InValue, int32 InSliceCount = 1)
	{
		return AddDynamicValue<T>(NAME_None, InValue, InSliceCount);
	}

	// Remove a register given its index
	// Note: This only works if SupportsNames() == true
	FName Rename(int32 InRegisterIndex, const FName& InNewName);

	// Remove a register given its old name
	// Note: This only works if SupportsNames() == true
	FName Rename(const FName& InOldName, const FName& InNewName);

	// Adds a register path and returns its index
	int32 GetOrAddRegisterOffset(int32 InRegisterIndex, int32 InArrayElement = 0);

	// Adds a register path and returns its index
	int32 GetOrAddRegisterOffset(int32 InRegisterIndex, const FString& InSegmentPath, int32 InArrayElement = 0);

	// Adds a register path and returns its index
	int32 GetOrAddRegisterOffset(int32 InRegisterIndex, UScriptStruct* InScriptStruct, const FString& InSegmentPath, int32 InInitialOffset = 0, int32 InElementSize = 0);

	void SetRegisterValueFromString(const FRigVMOperand& InOperand, const FString& InCPPType, const UObject* InCPPTypeObject, const TArray<FString>& InDefaultValues);
	TArray<FString> GetRegisterValueAsString(const FRigVMOperand& InOperand, const FString& InCPPType, const UObject* InCPPTypeObject);

	// returns the statistics information
	FRigVMMemoryStatistics GetStatistics() const
	{
		FRigVMMemoryStatistics Statistics;
		Statistics.RegisterCount = Registers.Num();
		Statistics.DataBytes = Data.GetAllocatedSize();
		Statistics.TotalBytes = Data.GetAllocatedSize() + Registers.GetAllocatedSize() + RegisterOffsets.GetAllocatedSize();
		return Statistics;
	}

private:

	// Copies the source memory into a known register
	bool Copy(
		int32 InTargetRegisterIndex,
		int32 InTargetRegisterOffset,
		ERigVMRegisterType InTargetType,
		const uint8* InSourcePtr,
		uint8* InTargetPtr,
		uint16 InNumBytes);

	template<
		typename T,
		typename TEnableIf<TIsArithmetic<T>::Value>::Type* = nullptr
	>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount)
	{
		return AddRegisterArray<T>(bFixed, InNewName, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, ERigVMRegisterType::Plain, nullptr);
	}

	template<
		typename T,
		typename TEnableIf<TRigVMIsName<T>::Value>::Type* = nullptr
	>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount)
	{
		return AddRegisterArray<T>(bFixed, InNewName, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, ERigVMRegisterType::Name, nullptr);
	}

	template<
		typename T,
		typename TEnableIf<TRigVMIsString<T>::Value>::Type* = nullptr
	>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount)
	{
		return AddRegisterArray<T>(bFixed, InNewName, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, ERigVMRegisterType::String, nullptr);
	}

	template<
		typename T,
		typename TEnableIf<TModels<CRigVMUStruct, T>::Value>::Type* = nullptr
	>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount)
	{
		return AddRegisterArray<T>(bFixed, InNewName, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, ERigVMRegisterType::Struct, T::StaticStruct());
	}

	template<
		typename T,
		typename TEnableIf<TIsEnum<T>::Value>::Type* = nullptr
	>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount)
	{
		return AddRegisterArray<T>(bFixed, InNewName, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, ERigVMRegisterType::Plain, nullptr);
	}

	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value, T>::Type* = nullptr
	>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount)
	{
		return AddRegisterArray<T>(bFixed, InNewName, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, ERigVMRegisterType::Plain, TBaseStructure<T>::Get());
	}

	template<typename T>
	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount, ERigVMRegisterType InType, UScriptStruct* InScriptStruct)
	{
		if(bFixed)
		{
			return AddFixedArray(InNewName, sizeof(T), InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, InType, InScriptStruct);
		}
		else
		{
			return AddDynamicArray(InNewName, sizeof(T), InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, InType, InScriptStruct);
		}
	}

	FORCEINLINE_DEBUGGABLE int32 AddRegisterArray(bool bFixed, const FName& InNewName, int32 InElementSize, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount, ERigVMRegisterType InType, UScriptStruct* InScriptStruct)
	{
		if (bFixed)
		{
			return AddFixedArray(InNewName, InElementSize, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, InType, InScriptStruct);
		}
		else
		{
			return AddDynamicArray(InNewName, InElementSize, InCount, bIsArrayPerSlice, InDataPtr, InSliceCount, InType, InScriptStruct);
		}
	}

	// Adds a new named register for a fixed array from a data pointer (used by compiler)
	FORCEINLINE_DEBUGGABLE int32 AddFixedArray(const FName& InNewName, int32 InElementSize, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount, ERigVMRegisterType InType, UScriptStruct* InScriptStruct)	{
		int32 Register = Allocate(InNewName, InElementSize, InCount, InSliceCount, nullptr, false);
		if (!Registers.IsValidIndex(Register))
		{
			return Register;
		}

		Registers[Register].Type = InType;
		Registers[Register].ScriptStructIndex = FindOrAddScriptStruct(InScriptStruct);
		Registers[Register].bIsArray = bIsArrayPerSlice;

		UpdateRegisters();
		Construct(Register);

		if (InDataPtr)
		{
			for (int32 SliceIndex = 0; SliceIndex < InSliceCount; SliceIndex++)
			{
				Copy(Register, INDEX_NONE, InType, InDataPtr, GetData(Registers[Register], INDEX_NONE, SliceIndex), InElementSize * InCount);
			}
		}
		return Register;
	}

	// Adds a new named register for a dynamic array from a data pointer (used by compiler)
	FORCEINLINE_DEBUGGABLE int32 AddDynamicArray(const FName& InNewName, int32 InElementSize, int32 InCount, bool bIsArrayPerSlice, const uint8* InDataPtr, int32 InSliceCount, ERigVMRegisterType InType, UScriptStruct* InScriptStruct)
	{
		int32 Register = INDEX_NONE;
		
		if (bIsArrayPerSlice)
		{
			Register = Allocate(InNewName, sizeof(FRigVMNestedByteArray), 1, 1, nullptr);
			if (!Registers.IsValidIndex(Register))
			{
				return Register;
			}

			Registers[Register].Type = InType;
			Registers[Register].ScriptStructIndex = FindOrAddScriptStruct(InScriptStruct);
			Registers[Register].bIsDynamic = true;
			Registers[Register].bIsArray = bIsArrayPerSlice;
			Registers[Register].ElementSize = InElementSize;
			Registers[Register].ElementCount = InCount;
			Registers[Register].SliceCount = InSliceCount;

			uint8* Ptr = (uint8*)&Data[Registers[Register].GetWorkByteIndex()];
			FRigVMNestedByteArray& Storage = *(FRigVMNestedByteArray*)Ptr;
			Storage.SetNum(InSliceCount);

			for (int32 SliceIndex = 0; SliceIndex < InSliceCount; SliceIndex++)
			{
				Storage[SliceIndex].SetNumZeroed(InCount * InElementSize);
				if (InDataPtr)
				{
					Copy(Register, INDEX_NONE, InType, InDataPtr, Storage[SliceIndex].GetData(), InElementSize * InCount);
				}
			}

			if (InDataPtr == nullptr)
			{
				Construct(Register);
			}
		}
		else
		{
			Register = Allocate(InNewName, sizeof(FRigVMByteArray), 1, 1, nullptr);
			if (!Registers.IsValidIndex(Register))
			{
				return Register;
			}

			Registers[Register].Type = InType;
			Registers[Register].ScriptStructIndex = FindOrAddScriptStruct(InScriptStruct);
			Registers[Register].bIsDynamic = true;
			Registers[Register].bIsArray = bIsArrayPerSlice;
			Registers[Register].ElementSize = InElementSize;
			Registers[Register].ElementCount = InCount;
			Registers[Register].SliceCount = InSliceCount;

			uint8* Ptr = (uint8*)&Data[Registers[Register].GetWorkByteIndex()];
			FRigVMByteArray& Storage = *(FRigVMByteArray*)Ptr;

			Storage.SetNumZeroed(InSliceCount * InElementSize);
			if (InDataPtr)
			{
				for (int32 SliceIndex = 0; SliceIndex < InSliceCount; SliceIndex++)
				{
					Copy(Register, INDEX_NONE, InType, InDataPtr, &Storage[SliceIndex * InElementSize], InElementSize);
				}
			}
			else
			{
				Construct(Register);
			}
		}

		return Register;
	}

	// Updates internal data for topological changes
	void UpdateRegisters();

	// Allocates a new named register
	int32 Allocate(const FName& InNewName, int32 InElementSize, int32 InElementCount, int32 InSliceCount, const uint8* InDataPtr = nullptr, bool bUpdateRegisters = true);

	// Allocates a new unnamed register
	int32 Allocate(int32 InElementSize, int32 InElementCount, int32 InSliceCount, const uint8* InDataPtr = nullptr, bool bUpdateRegisters = true);

	// Performs optional construction of data within a struct register
	bool Construct(int32 InRegisterIndex, int32 InElementIndex = INDEX_NONE, int32 InSliceIndex = 0);

	// Performs optional destruction of data within a struct register
	bool Destroy(int32 InRegisterIndex, int32 InElementIndex = INDEX_NONE, int32 InSliceIndex = 0);

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

	UPROPERTY()
	TArray<FRigVMRegisterOffset> RegisterOffsets;

	UPROPERTY(transient)
	TArray<uint8> Data;

	UPROPERTY(transient)
	TArray<UScriptStruct*> ScriptStructs;

	UPROPERTY(transient)
	TMap<FName, int32> NameMap;

	UPROPERTY(transient)
	bool bEncounteredErrorDuringLoad;

	static FRigVMByteArray DefaultByteArray;

	friend class URigVM;
	friend class URigVMCompiler;
	friend struct FRigVMCompilerWorkData;
};

typedef FRigVMMemoryContainer* FRigVMMemoryContainerPtr;
typedef FRigVMFixedArray<FRigVMMemoryContainer*> FRigVMMemoryContainerPtrArray;