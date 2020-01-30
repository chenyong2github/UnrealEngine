// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderParameterMetadata.h: Meta data about shader parameter structures
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/List.h"
#include "Containers/StaticArray.h"
#include "RHI.h"

namespace EShaderPrecisionModifier
{
	enum Type
	{
		Float,
		Half,
		Fixed
	};
};

/** Each entry in a resource table is provided to the shader compiler for creating mappings. */
struct FResourceTableEntry
{
	/** The name of the uniform buffer in which this resource exists. */
	FString UniformBufferName;
	/** The type of the resource (EUniformBufferBaseType). */
	uint16 Type;
	/** The index of the resource in the table. */
	uint16 ResourceIndex;
};

/** Simple class that registers a uniform buffer static slot in the constructor. */
class RENDERCORE_API FUniformBufferStaticSlotRegistrar
{
public:
	FUniformBufferStaticSlotRegistrar(const TCHAR* InName);
};

/** Registry for uniform buffer static slots. */
class RENDERCORE_API FUniformBufferStaticSlotRegistry
{
public:
	static FUniformBufferStaticSlotRegistry& Get();

	void RegisterSlot(FName SlotName);

	inline int32 GetSlotCount() const
	{
		return SlotNames.Num();
	}

	inline FString GetDebugDescription(FUniformBufferStaticSlot Slot) const
	{
		return FString::Printf(TEXT("[Name: %s, Slot: %u]"), *GetSlotName(Slot).ToString(), Slot);
	}

	inline FName GetSlotName(FUniformBufferStaticSlot Slot) const
	{
		checkf(Slot < SlotNames.Num(), TEXT("Requesting name for an invalid slot: %u."), Slot);
		return SlotNames[Slot];
	}

	inline FUniformBufferStaticSlot FindSlotByName(FName SlotName) const
	{
		// Brute force linear search. The search space is small and the find operation should not be critical path.
		for (int32 Index = 0; Index < SlotNames.Num(); ++Index)
		{
			if (SlotNames[Index] == SlotName)
			{
				return FUniformBufferStaticSlot(Index);
			}
		}
		return MAX_UNIFORM_BUFFER_STATIC_SLOTS;
	}

private:
	TArray<FName> SlotNames;
};

/** A uniform buffer struct. */
class RENDERCORE_API FShaderParametersMetadata
{
public:
	/** The use case of the uniform buffer structures. */
	enum class EUseCase
	{
		/** Stand alone shader parameter struct used for render passes and shader parameters. */
		ShaderParameterStruct,

		/** Uniform buffer definition authored at compile-time. */
		UniformBuffer,

		/** Uniform buffer generated from assets, such as material parameter collection or Niagara. */
		DataDrivenUniformBuffer,
	};

	/** Shader binding name of the uniform buffer that contains the root shader parameters. */
	static constexpr const TCHAR* kRootUniformBufferBindingName = TEXT("_RootShaderParameters");
	
	/** A member of a shader parameter structure. */
	class FMember
	{
	public:

		/** Initialization constructor. */
		FMember(
			const TCHAR* InName,
			const TCHAR* InShaderType,
			uint32 InOffset,
			EUniformBufferBaseType InBaseType,
			EShaderPrecisionModifier::Type InPrecision,
			uint32 InNumRows,
			uint32 InNumColumns,
			uint32 InNumElements,
			const FShaderParametersMetadata* InStruct
			)
		:	Name(InName)
		,	ShaderType(InShaderType)
		,	Offset(InOffset)
		,	BaseType(InBaseType)
		,	Precision(InPrecision)
		,	NumRows(InNumRows)
		,	NumColumns(InNumColumns)
		,	NumElements(InNumElements)
		,	Struct(InStruct)
		{}

		/** Returns the string of the name of the element or name of the array of elements. */
		const TCHAR* GetName() const { return Name; }

		/** Returns the string of the type. */
		const TCHAR* GetShaderType() const { return ShaderType; }

		/** Returns the offset of the element in the shader parameter struct in bytes. */
		uint32 GetOffset() const { return Offset; }

		/** Returns the type of the elements, int, UAV... */
		EUniformBufferBaseType GetBaseType() const { return BaseType; }

		/** Floating point the element is being stored. */
		EShaderPrecisionModifier::Type GetPrecision() const { return Precision; }

		/** Returns the number of row in the element. For instance FMatrix would return 4, or FVector would return 1. */
		uint32 GetNumRows() const { return NumRows; }

		/** Returns the number of column in the element. For instance FMatrix would return 4, or FVector would return 3. */
		uint32 GetNumColumns() const { return NumColumns; }

		/** Returns the number of elements in array, or 0 if this is not an array. */
		uint32 GetNumElements() const { return NumElements; }

		/** Returns the metadata of the struct. */
		const FShaderParametersMetadata* GetStructMetadata() const { return Struct; }

		/** Returns the size of the member. */
		inline uint32 GetMemberSize() const
		{
			check(BaseType == UBMT_FLOAT32 || BaseType == UBMT_INT32 || BaseType == UBMT_UINT32);
			uint32 ElementSize = sizeof(uint32) * NumRows * NumColumns;

			/** If this an array, the alignment of the element are changed. */
			if (NumElements > 0)
			{
				return Align(ElementSize, SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) * NumElements;
			}
			return ElementSize;
		}

	private:

		const TCHAR* Name;
		const TCHAR* ShaderType;
		uint32 Offset;
		EUniformBufferBaseType BaseType;
		EShaderPrecisionModifier::Type Precision;
		uint32 NumRows;
		uint32 NumColumns;
		uint32 NumElements;
		const FShaderParametersMetadata* Struct;
	};

	/** Initialization constructor. */
	FShaderParametersMetadata(
		EUseCase UseCase,
		const FName& InLayoutName,
		const TCHAR* InStructTypeName,
		const TCHAR* InShaderVariableName,
		const TCHAR* InStaticSlotName,
		uint32 InSize,
		const TArray<FMember>& InMembers);

	virtual ~FShaderParametersMetadata();

	void GetNestedStructs(TArray<const FShaderParametersMetadata*>& OutNestedStructs) const;

	void AddResourceTableEntries(TMap<FString, FResourceTableEntry>& ResourceTableMap, TMap<FString, uint32>& ResourceTableLayoutHashes, TMap<FString, FString>& ResourceTableLayoutSlots) const;

	const TCHAR* GetStructTypeName() const { return StructTypeName; }
	const TCHAR* GetShaderVariableName() const { return ShaderVariableName; }
	const TCHAR* GetStaticSlotName() const { return StaticSlotName; }

	bool HasStaticSlot() const { return StaticSlotName != nullptr; }

	uint32 GetSize() const { return Size; }
	EUseCase GetUseCase() const { return UseCase; }
	const FRHIUniformBufferLayout& GetLayout() const 
	{ 
		check(bLayoutInitialized);
		return Layout; 
	}
	const TArray<FMember>& GetMembers() const { return Members; }

	/** Find a member for a given offset. */
	void FindMemberFromOffset(
		uint16 MemberOffset,
		const FShaderParametersMetadata** OutContainingStruct,
		const FShaderParametersMetadata::FMember** OutMember,
		int32* ArrayElementId, FString* NamePrefix) const;

	/** Returns the full C++ member name from it's byte offset in the structure. */
	FString GetFullMemberCodeName(uint16 MemberOffset) const;

	static TLinkedList<FShaderParametersMetadata*>*& GetStructList();
	/** Speed up finding the uniform buffer by its name */
	static TMap<FName, FShaderParametersMetadata*>& GetNameStructMap();

	/** Initialize all the global shader parameter structs. */
	static void InitializeAllUniformBufferStructs();

	/** Returns a hash about the entire layout of the structure. */
	uint32 GetLayoutHash() const
	{
		check(UseCase == EUseCase::ShaderParameterStruct || UseCase == EUseCase::UniformBuffer);
		check(bLayoutInitialized);
		return LayoutHash;	
	}

private:
	/** Name of the structure type in C++ and shader code. */
	const TCHAR* const StructTypeName;

	/** Name of the shader variable name for global shader parameter structs. */
	const TCHAR* const ShaderVariableName;

	/** Name of the static slot to use for the uniform buffer (or null). */
	const TCHAR* const StaticSlotName;

	/** Size of the entire struct in bytes. */
	const uint32 Size;

	/** The use case of this shader parameter struct. */
	const EUseCase UseCase;

	/** Layout of all the resources in the shader parameter struct. */
	FRHIUniformBufferLayout Layout;
	
	/** List of all members. */
	TArray<FMember> Members;

	/** Shackle elements in global link list of globally named shader parameters. */
	TLinkedList<FShaderParametersMetadata*> GlobalListLink;

	/** Whether the layout is actually initialized yet or not. */
	uint32 bLayoutInitialized : 1;

	/** Hash about the entire memory layout of the structure. */
	uint32 LayoutHash = 0;


	void InitializeLayout();

	void AddResourceTableEntriesRecursive(const TCHAR* UniformBufferName, const TCHAR* Prefix, uint16& ResourceIndex, TMap<FString, FResourceTableEntry>& ResourceTableMap) const;
};
