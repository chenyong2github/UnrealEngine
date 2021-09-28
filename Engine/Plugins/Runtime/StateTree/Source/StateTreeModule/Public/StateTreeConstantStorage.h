// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeConstantStorage.generated.h"

/**
 * Storage of all the constants used in the StateTree.
 * StateTree variables are handles can refer to items in StateTree instances memory or constants.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeConstantStorage
{
	GENERATED_BODY()

	FStateTreeConstantStorage()
	: ConstantBaseOffset(0)
	{
	}

	UPROPERTY()
	TArray<uint8> Memory;

	UPROPERTY()
	TArray<EStateTreeVariableType> Types;

	UPROPERTY()
	uint16 ConstantBaseOffset;

	bool GetConstant(FStateTreeHandle Handle, EStateTreeVariableType Type, uint8* Value) const
	{
		const uint16 Offset = Handle.Index - ConstantBaseOffset;
		if (Offset < (uint16)Memory.Num())
		{
			FMemory::Memcpy(Value, &Memory[Offset], FStateTreeVariableHelpers::GetVariableMemoryUsage(Type));
			return true;
		}
		return false;
	}

	float GetConstantFloat(FStateTreeHandle Handle, const float DefaultValue = 0.0f) const
	{
		return GetConstant<float>(Handle, DefaultValue);
	}

	int GetConstantInt(FStateTreeHandle Handle, const int DefaultValue = 0) const
	{
		return GetConstant<int>(Handle, DefaultValue);
	}

	bool GetConstantBool(FStateTreeHandle Handle, const bool DefaultValue = false) const
	{
		return GetConstant<bool>(Handle, DefaultValue);
	}

	const FVector& GetConstantVector(FStateTreeHandle Handle, const FVector& DefaultValue = FVector::ZeroVector) const
	{
		return GetConstant<FVector>(Handle, DefaultValue);
	}

	template <typename T>
	const T& GetConstant(FStateTreeHandle Handle, const T& DefaultValue) const
	{
		const uint16 Offset = Handle.Index - ConstantBaseOffset;
		if (Offset < (uint16)Memory.Num())
		{
			return FStateTreeVariableHelpers::GetValueFromMemory<T>(&Memory[Offset]);
		}
		return DefaultValue;
	}


#if WITH_EDITOR
	void Reset()
	{
		Memory.Reset();
		Types.Reset();
		ConstantBaseOffset = 0;
	}

	FStateTreeHandle AddConstantFloat(const float Value)
	{
		return AddConstant<float, EStateTreeVariableType::Float>(Value);
	}

	FStateTreeHandle AddConstantInt(const int32 Value)
	{
		return AddConstant<int32, EStateTreeVariableType::Int>(Value);
	}

	FStateTreeHandle AddConstantBool(const bool Value)
	{
		return AddConstant<bool, EStateTreeVariableType::Bool>(Value);
	}

	FStateTreeHandle AddConstantVector(const FVector& Value)
	{
		return AddConstant<FVector, EStateTreeVariableType::Vector>(Value);
	}

	template <typename T, EStateTreeVariableType TType>
	FStateTreeHandle AddConstant(const T& Value)
	{
		uint16 Offset = 0;
		for (EStateTreeVariableType Type : Types)
		{
			if (Type == TType && FStateTreeVariableHelpers::GetValueFromMemory<T>(&Memory[Offset]) == Value)
			{
				// Found same constant, return it.
				return FStateTreeHandle(ConstantBaseOffset + Offset);
			}
			Offset += FStateTreeVariableHelpers::GetVariableMemoryUsage(Type);
		}
		// Add new constant
		Offset = (uint16)Memory.Num();
		Memory.AddZeroed(FStateTreeVariableHelpers::GetVariableMemoryUsage(TType));
		FStateTreeVariableHelpers::SetValueInMemory<T>(&Memory[Offset], Value);

		return FStateTreeHandle(ConstantBaseOffset + Offset);
	}
#endif
};

