// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Shader/PreshaderTypes.h"
#include "Serialization/MemoryImage.h"
#include "Materials/MaterialLayersFunctions.h"

class FUniformExpressionSet;
struct FMaterialRenderContext;

namespace UE
{
namespace Shader
{

enum class EPreshaderOpcode : uint8
{
	Nop,
	ConstantZero,
	Constant,
	Parameter,
	Add,
	Sub,
	Mul,
	Div,
	Fmod,
	Min,
	Max,
	Clamp,
	Sin,
	Cos,
	Tan,
	Asin,
	Acos,
	Atan,
	Atan2,
	Dot,
	Cross,
	Sqrt,
	Rcp,
	Length,
	Normalize,
	Saturate,
	Abs,
	Floor,
	Ceil,
	Round,
	Trunc,
	Sign,
	Frac,
	Fractional,
	Log2,
	Log10,
	ComponentSwizzle,
	AppendVector,
	TextureSize,
	TexelSize,
	ExternalTextureCoordinateScaleRotation,
	ExternalTextureCoordinateOffset,
	RuntimeVirtualTextureUniform,
	GetField,
	SetField,
	Neg,
};

struct FPreshaderStructType
{
	DECLARE_TYPE_LAYOUT(FPreshaderStructType, NonVirtual);
	LAYOUT_FIELD(uint64, Hash);
	LAYOUT_FIELD(int32, ComponentTypeIndex);
	LAYOUT_FIELD(int32, NumComponents);
};

class FPreshaderData
{
	DECLARE_TYPE_LAYOUT(FPreshaderData, NonVirtual);
public:
	friend inline bool operator==(const FPreshaderData& Lhs, const FPreshaderData& Rhs)
	{
		return Lhs.Names == Rhs.Names && Lhs.Data == Rhs.Data;
	}

	friend inline bool operator!=(const FPreshaderData& Lhs, const FPreshaderData& Rhs)
	{
		return !operator==(Lhs, Rhs);
	}

	FSHAHash GetHash() const;
	void AppendHash(FSHA1& OutHasher) const;

	FPreshaderValue Evaluate(FUniformExpressionSet* UniformExpressionSet, const struct FMaterialRenderContext& Context, FPreshaderStack& Stack) const;
	FPreshaderValue EvaluateConstant(const FMaterial& Material, FPreshaderStack& Stack) const;

	const int32 Num() const { return Data.Num(); }

	void WriteData(const void* Value, uint32 Size);
	void WriteName(const FScriptName& Name);
	void WriteType(const FType& Type);
	void WriteValue(const FValue& Value);

	template<typename T>
	FPreshaderData& Write(const T& Value) { WriteData(&Value, sizeof(T)); return *this; }

	template<>
	FPreshaderData& Write<FScriptName>(const FScriptName& Value) { WriteName(Value); return *this; }

	template<>
	FPreshaderData& Write<FType>(const FType& Value) { WriteType(Value); return *this; }

	template<>
	FPreshaderData& Write<FValue>(const FValue& Value) { WriteValue(Value); return *this; }

	/** Can't write FName, use FScriptName instead */
	template<>
	FPreshaderData& Write<FName>(const FName& Value) = delete;

	template<>
	FPreshaderData& Write<FHashedMaterialParameterInfo>(const FHashedMaterialParameterInfo& Value) { return Write(Value.Name).Write(Value.Index).Write(Value.Association); }

	inline FPreshaderData& WriteOpcode(EPreshaderOpcode Op) { return Write<uint8>((uint8)Op); }

	LAYOUT_FIELD(TMemoryImageArray<FScriptName>, Names);
	LAYOUT_FIELD(TMemoryImageArray<FPreshaderStructType>, StructTypes);
	LAYOUT_FIELD(TMemoryImageArray<EValueComponentType>, StructComponentTypes);
	LAYOUT_FIELD(TMemoryImageArray<uint8>, Data);
};

} // namespace Shader
} // namespace UE