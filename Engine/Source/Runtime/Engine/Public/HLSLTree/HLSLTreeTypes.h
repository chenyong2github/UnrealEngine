// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/EnumClassFlags.h"

class UTexture;

namespace UE
{
namespace HLSLTree
{

class FNode;
class FExpression;
class FLocalDeclaration;
class FParameterDeclaration;
class FTextureParameterDeclaration;
class FStatement;
class FScope;
class FTree;
class FCodeWriter;

enum class EExpressionComponentType
{
	Void,
	Float,
	MaterialAttributes,
};

struct FExpressionTypeDescription
{
	FExpressionTypeDescription() : Name(nullptr), ComponentType(EExpressionComponentType::Void), NumComponents(0) {}
	FExpressionTypeDescription(const TCHAR* InName, EExpressionComponentType InComponentType, int32 InNumComponents) : Name(InName), ComponentType(InComponentType), NumComponents(InNumComponents) {}

	const TCHAR* Name;
	EExpressionComponentType ComponentType;
	int32 NumComponents;
};

enum class EExpressionType : uint8
{
	Void,

	Float1,
	Float2,
	Float3,
	Float4,

	MaterialAttributes,
};

FExpressionTypeDescription GetExpressionTypeDescription(EExpressionType Type);
EExpressionType MakeExpressionType(EExpressionComponentType ComponentType, int32 NumComponents);
EExpressionType MakeExpressionType(EExpressionType BaseType, int32 NumComponents);
EExpressionType MakeArithmeticResultType(EExpressionType Lhs, EExpressionType Rhs, FString& OutErrorMessage);

/**
 * Describes how a given expression needs to be evaluated */
enum class EExpressionEvaluationType
{
	/** The expression needs to generate HLSL code (EmitHLSL will be called) */
	Shader,

	/** The expression can generate a preshader (EmitPreshader will be called) */
	Preshader,

	/** The expression is constant (EmitPreshader will be called, generated preshader will be evaluated and cached at compile-time) */
	Constant,
};

EExpressionEvaluationType CombineEvaluationTypes(EExpressionEvaluationType Lhs, EExpressionEvaluationType Rhs);

struct FConstant
{
	inline FConstant() : Type(EExpressionType::Void) { FMemory::Memzero(Raw); }
	inline FConstant(float v) : Type(EExpressionType::Float1) { FMemory::Memzero(Raw); Float[0] = v; }
	inline FConstant(float X, float Y) : Type(EExpressionType::Float2) { FMemory::Memzero(Raw); Float[0] = X; Float[1] = Y; }
	inline FConstant(float X, float Y, float Z) : Type(EExpressionType::Float3) { FMemory::Memzero(Raw); Float[0] = X; Float[1] = Y; Float[2] = Z; }
	inline FConstant(float X, float Y, float Z, float W) : Type(EExpressionType::Float4) { FMemory::Memzero(Raw); Float[0] = X; Float[1] = Y; Float[2] = Z; Float[3] = W; }
	inline FConstant(EExpressionType InType, const FLinearColor& Value) : Type(InType) { FMemory::Memzero(Raw); Float[0] = Value.R; Float[1] = Value.G; Float[2] = Value.B; Float[3] = Value.A; }

	FLinearColor ToLinearColor() const;

	void EmitHLSL(FCodeWriter& Writer) const;

	EExpressionType Type;
	union
	{
		uint32 Raw[4];
		float Float[4];
	};
};

struct FTextureDescription
{
	FTextureDescription()
		: Texture(nullptr), SamplerType(SAMPLERTYPE_Color)
	{}

	FTextureDescription(UTexture* InTexture, EMaterialSamplerType InSamplerType)
		: Texture(InTexture), SamplerType(InSamplerType)
	{}

	UTexture* Texture;
	EMaterialSamplerType SamplerType;
};
inline bool operator==(const FTextureDescription& Lhs, const FTextureDescription& Rhs)
{
	return Lhs.Texture == Rhs.Texture && Lhs.SamplerType == Rhs.SamplerType;
}
inline bool operator!=(const FTextureDescription& Lhs, const FTextureDescription& Rhs)
{
	return !operator==(Lhs, Rhs);
}
inline uint32 GetTypeHash(const FTextureDescription& Ref)
{
	return HashCombine(GetTypeHash(Ref.Texture), GetTypeHash(Ref.SamplerType));
}

enum class ECastFlags : uint32
{
	None = 0u,
	ReplicateScalar = (1u << 0),
};
ENUM_CLASS_FLAGS(ECastFlags);

} // namespace HLSLTree
} // namespace UE
