// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Shader/ShaderTypes.h"
#include "Containers/ArrayView.h"

namespace UE
{
namespace Shader
{

/**
 * Mirrors Shader::FType, but stores StructType as a hash rather than a pointer to facilitate serialization
 * Struct's flattened component types are stored directly, as that is the primary thing needed at runtime
 */
struct FPreshaderType
{
	FPreshaderType() = default;
	FPreshaderType(const FType& InType);
	FPreshaderType(EValueType InType);

	bool IsStruct() const { return StructTypeHash != 0u; }
	int32 GetNumComponents() const { return StructTypeHash != 0u ? StructComponentTypes.Num() : GetValueTypeDescription(ValueType).NumComponents; }
	EValueComponentType GetComponentType(int32 Index) const;

	uint64 StructTypeHash = 0u;
	TArrayView<const EValueComponentType> StructComponentTypes;
	EValueType ValueType = EValueType::Void;
};

/** Mirrors Shader::FValue, except 'Component' array references memory owned by stack, rather than inline storage */
struct FPreshaderValue
{
	FPreshaderType Type;
	TArrayView<FValueComponent> Component;

	/** Converts to a regular 'FValue', uses the type registry to resolve StructType hashes into pointers */
	FValue AsShaderValue(const FStructTypeRegistry* TypeRegistry = nullptr) const;
};

class FPreshaderStack
{
public:
	int32 Num() const { return Values.Num(); }

	void CheckEmpty() const
	{
		check(Values.Num() == 0);
		check(Components.Num() == 0);
	}

	void PushValue(const FValue& InValue);
	void PushValue(const FPreshaderValue& InValue);
	void PushValue(const FPreshaderType& InType, TArrayView<const FValueComponent> InComponents);
	TArrayView<FValueComponent> PushEmptyValue(const FPreshaderType& InType);

	/** Returned values are invalidated when anything is pushed onto the stack */
	FPreshaderValue PopValue();
	FPreshaderValue PeekValue(int32 Offset = 0);

	void Reset()
	{
		Values.Reset();
		Components.Reset();
	}

private:
	TArray<FPreshaderType, TInlineAllocator<8>> Values;
	TArray<FValueComponent, TInlineAllocator<64>> Components;
};

} // namespace Shader
} // namespace UE