// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace UE
{
namespace Shader
{

enum class EValueComponentType : uint8
{
	Void,
	Float,
	Int,
	Bool,
	MaterialAttributes,
};

struct FValueTypeDescription
{
	FValueTypeDescription() : Name(nullptr), ComponentType(EValueComponentType::Void), NumComponents(0) {}
	FValueTypeDescription(const TCHAR* InName, EValueComponentType InComponentType, int8 InNumComponents) : Name(InName), ComponentType(InComponentType), NumComponents(InNumComponents) {}

	const TCHAR* Name;
	EValueComponentType ComponentType;
	int8 NumComponents;
};

enum class EValueType : uint8
{
	Void,

	Float1,
	Float2,
	Float3,
	Float4,

	Int1,
	Int2,
	Int3,
	Int4,

	Bool1,
	Bool2,
	Bool3,
	Bool4,

	MaterialAttributes,
};

FValueTypeDescription GetValueTypeDescription(EValueType Type);
EValueType MakeValueType(EValueComponentType ComponentType, int32 NumComponents);
EValueType MakeValueType(EValueType BaseType, int32 NumComponents);
EValueType MakeArithmeticResultType(EValueType Lhs, EValueType Rhs, FString& OutErrorMessage);
EValueType MakeComparisonResultType(EValueType Lhs, EValueType Rhs, FString& OutErrorMessage);

template<typename T>
struct TValue
{
	inline T& operator[](int32 i) { check(i >= 0 && i < 4); return Component[i]; }
	inline const T& operator[](int32 i) const { check(i >= 0 && i < 4); return Component[i]; }

	T Component[4];
};
using FFloatValue = TValue<float>;
using FIntValue = TValue<int32>;
using FBoolValue = TValue<bool>;

union FValueComponent
{
	FValueComponent() : Packed(0u) {}

	uint32 Packed;
	float Float;
	int32 Int;
	bool Bool;
};
static_assert(sizeof(FValueComponent) == sizeof(uint32), "bad packing");

struct FValue
{
	inline FValue() : ComponentType(EValueComponentType::Void), NumComponents(0) {}

	explicit FValue(EValueType InType)
	{
		const FValueTypeDescription TypeDesc = GetValueTypeDescription(InType);
		ComponentType = TypeDesc.ComponentType;
		NumComponents = TypeDesc.NumComponents;
	}

	inline FValue(EValueComponentType InComponentType, int8 InNumComponents) : ComponentType(InComponentType), NumComponents(InNumComponents) {}

	inline FValue(float v) : ComponentType(EValueComponentType::Float), NumComponents(1)
	{
		Component[0].Float = v;
	}

	inline FValue(float X, float Y) : ComponentType(EValueComponentType::Float), NumComponents(2)
	{
		Component[0].Float = X;
		Component[1].Float = Y;
	}

	inline FValue(float X, float Y, float Z) : ComponentType(EValueComponentType::Float), NumComponents(3)
	{
		Component[0].Float = X;
		Component[1].Float = Y;
		Component[2].Float = Z;
	}

	inline FValue(float X, float Y, float Z, float W) : ComponentType(EValueComponentType::Float), NumComponents(4)
	{
		Component[0].Float = X;
		Component[1].Float = Y;
		Component[2].Float = Z;
		Component[3].Float = W;
	}

	inline FValue(const FLinearColor& Value) : ComponentType(EValueComponentType::Float), NumComponents(4)
	{
		Component[0].Float = Value.R;
		Component[1].Float = Value.G;
		Component[2].Float = Value.B;
		Component[3].Float = Value.A;
	}

	inline FValue(const FVector2D& Value) : ComponentType(EValueComponentType::Float), NumComponents(2)
	{
		Component[0].Float = Value.X;
		Component[1].Float = Value.Y;
	}

	inline FValue(const FVector& Value) : ComponentType(EValueComponentType::Float), NumComponents(3)
	{
		Component[0].Float = Value.X;
		Component[1].Float = Value.Y;
		Component[2].Float = Value.Z;
	}

	inline FValue(const FVector4& Value) : ComponentType(EValueComponentType::Float), NumComponents(4)
	{
		Component[0].Float = Value.X;
		Component[1].Float = Value.Y;
		Component[2].Float = Value.Z;
		Component[3].Float = Value.W;
	}

	inline FValue(bool v) : ComponentType(EValueComponentType::Bool), NumComponents(1)
	{
		Component[0].Bool = v;
	}

	inline FValue(int32 v) : ComponentType(EValueComponentType::Int), NumComponents(1)
	{
		Component[0].Int = v;
	}

	inline EValueType GetType() const { return MakeValueType(ComponentType, NumComponents); }

	inline const FValueComponent& GetComponent(int32 i) const { check(i >= 0 && i < NumComponents); return Component[i]; }

	FFloatValue AsFloat() const;
	FIntValue AsInt() const;
	FBoolValue AsBool() const;

	FLinearColor AsLinearColor() const;
	bool AsBoolScalar() const;

	EValueComponentType ComponentType;
	int8 NumComponents;
	FValueComponent Component[4];
};

ENGINE_API bool operator==(const FValue& Lhs, const FValue& Rhs);
inline bool operator!=(const FValue& Lhs, const FValue& Rhs) { return !operator==(Lhs, Rhs); }

ENGINE_API FValue Abs(const FValue& Value);
ENGINE_API FValue Saturate(const FValue& Value);
ENGINE_API FValue Floor(const FValue& Value);
ENGINE_API FValue Ceil(const FValue& Value);
ENGINE_API FValue Round(const FValue& Value);
ENGINE_API FValue Trunc(const FValue& Value);
ENGINE_API FValue Sign(const FValue& Value);
ENGINE_API FValue Frac(const FValue& Value);
ENGINE_API FValue Fractional(const FValue& Value);
ENGINE_API FValue Sqrt(const FValue& Value);
ENGINE_API FValue Log2(const FValue& Value);
ENGINE_API FValue Log10(const FValue& Value);
ENGINE_API FValue Sin(const FValue& Value);
ENGINE_API FValue Cos(const FValue& Value);
ENGINE_API FValue Tan(const FValue& Value);
ENGINE_API FValue Asin(const FValue& Value);
ENGINE_API FValue Acos(const FValue& Value);
ENGINE_API FValue Atan(const FValue& Value);

ENGINE_API FValue Add(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Sub(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Mul(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Div(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Min(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Max(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Clamp(const FValue& Value, const FValue& Low, const FValue& High);
ENGINE_API FValue Fmod(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Atan2(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Dot(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Cross(const FValue& Lhs, const FValue& Rhs);
ENGINE_API FValue Append(const FValue& Lhs, const FValue& Rhs);
}
}
