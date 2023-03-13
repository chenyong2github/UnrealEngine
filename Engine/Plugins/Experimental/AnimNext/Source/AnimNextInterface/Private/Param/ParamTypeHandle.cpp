// Copyright Epic Games, Inc. All Rights Reserved.

#include "Param/ParamTypeHandle.h"
#include "Param/ParamType.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/Class.h"

namespace UE::AnimNext
{

// Array of all non built-in types. Index into this array is (CustomTypeIndex - 1)
static TArray<FAnimNextParamType> GCustomTypes;

// Map of type to index in GNonBuiltInTypes array
static TMap<FAnimNextParamType, uint32> GTypeToIndexMap;

// RW lock for types array/map
FRWLock TypesLock;

void FParamTypeHandle::ResetCustomTypes()
{
	FRWScopeLock ScopeLock(TypesLock, SLT_Write);

	GCustomTypes.Empty();
	GTypeToIndexMap.Empty();
}

uint32 FParamTypeHandle::GetOrAllocateCustomTypeIndex(FAnimNextParamType::EValueType InValueType, FAnimNextParamType::EContainerType InContainerType, UObject* InValueTypeObject)
{
	FAnimNextParamType ParameterType;
	ParameterType.ValueTypeObject = InValueTypeObject;
	ParameterType.ValueType = InValueType;
	ParameterType.ContainerType = InContainerType;

	const uint32 Hash = GetTypeHash(ParameterType);

	// NOTE: It is tempting to try to acquire a only read lock here during the FindByHash, but doing so means that the
	// lazy-insert operation is no longer atomic as there is no way to upgrade a read lock into a write lock with
	// FRWScopeLock (@see FRWScopeLock comments), hence the write lock around the map/array access here.
	{
		// See if the type already exists in the map
		FRWScopeLock ScopeLock(TypesLock, SLT_Write);

		if(const uint32* IndexPtr = GTypeToIndexMap.FindByHash(Hash, ParameterType))
		{
			return *IndexPtr + 1;
		}

		// Add a new custom type
		const uint32 Index = GCustomTypes.Add(ParameterType);
		GTypeToIndexMap.AddByHash(Hash, ParameterType, Index);

		checkf((Index + 1) < (1 << 24), TEXT("FParamTypeHandle::GetCustomTypeIndex: Type index overflowed"));
		return Index + 1;
	}
}

bool FParamTypeHandle::ValidateCustomTypeIndex(uint32 InCustomTypeIndex)
{
	FRWScopeLock ScopeLock(TypesLock, SLT_ReadOnly);
	return GCustomTypes.IsValidIndex(InCustomTypeIndex - 1);
}

FAnimNextParamType FParamTypeHandle::GetType() const
{
	FAnimNextParamType ParameterType;

	switch(GetParameterType())
	{
	default:
	case EParamType::None:
		break;
	case EParamType::Bool:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Bool;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Byte:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Byte;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Int32:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Int32;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Int64:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Int64;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Float:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Float;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Double:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Double;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Name:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Name;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::String:
		ParameterType.ValueType = FAnimNextParamType::EValueType::String;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Text:
		ParameterType.ValueType = FAnimNextParamType::EValueType::Text;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Vector:
		ParameterType.ValueTypeObject = TBaseStructure<FVector>::Get();
		ParameterType.ValueType = FAnimNextParamType::EValueType::Struct;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Vector4:
		ParameterType.ValueTypeObject = TBaseStructure<FVector4>::Get();
		ParameterType.ValueType = FAnimNextParamType::EValueType::Struct;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Quat:
		ParameterType.ValueTypeObject = TBaseStructure<FQuat>::Get();
		ParameterType.ValueType = FAnimNextParamType::EValueType::Struct;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Transform:
		ParameterType.ValueTypeObject = TBaseStructure<FTransform>::Get();
		ParameterType.ValueType = FAnimNextParamType::EValueType::Struct;
		ParameterType.ContainerType = FAnimNextParamType::EContainerType::None;
		break;
	case EParamType::Custom:
		{
			FRWScopeLock ScopeLock(TypesLock, SLT_ReadOnly);
			ParameterType = GCustomTypes[GetCustomTypeIndex() - 1];
		}
		break;
	}

	return ParameterType;
}

size_t FParamTypeHandle::GetSize() const
{
	switch(GetParameterType())
	{
	default:
	case EParamType::None:
		return 0;
	case EParamType::Bool:
		return sizeof(bool);
	case EParamType::Byte:
		return sizeof(uint8);
	case EParamType::Int32:
		return sizeof(int32);
	case EParamType::Int64:
		return sizeof(int64);
	case EParamType::Float:
		return sizeof(float);
	case EParamType::Double:
		return sizeof(double);
	case EParamType::Name:
		return sizeof(FName);
	case EParamType::String:
		return sizeof(FString);
	case EParamType::Text:
		return sizeof(FText);
	case EParamType::Vector:
		return sizeof(FVector);
	case EParamType::Vector4:
		return sizeof(FVector4);
	case EParamType::Quat:
		return sizeof(FQuat);
	case EParamType::Transform:
		return sizeof(FTransform);
	case EParamType::Custom:
		return GetType().GetSize();
	}

	return 0;
}

size_t FParamTypeHandle::GetValueTypeSize() const
{
	return GetSize();
}

size_t FParamTypeHandle::GetAlignment() const
{
	switch(GetParameterType())
	{
	default:
	case EParamType::None:
		return 0;
	case EParamType::Bool:
		return alignof(bool);
	case EParamType::Byte:
		return alignof(uint8);
	case EParamType::Int32:
		return alignof(int32);
	case EParamType::Int64:
		return alignof(int64);
	case EParamType::Float:
		return alignof(float);
	case EParamType::Double:
		return alignof(double);
	case EParamType::Name:
		return alignof(FName);
	case EParamType::String:
		return alignof(FString);
	case EParamType::Text:
		return alignof(FText);
	case EParamType::Vector:
		return alignof(FVector);
	case EParamType::Vector4:
		return alignof(FVector4);
	case EParamType::Quat:
		return alignof(FQuat);
	case EParamType::Transform:
		return alignof(FTransform);
	case EParamType::Custom:
		return GetType().GetAlignment();
	}

	return 0;
}

size_t FParamTypeHandle::GetValueTypeAlignment() const
{
	return GetAlignment();
}

FString FParamTypeHandle::ToString() const
{
	return GetType().ToString();
}

}
