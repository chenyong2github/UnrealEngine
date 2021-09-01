// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/EnumClassFlags.h"
#include "Shader/ShaderTypes.h"

class UTexture;

namespace UE
{
namespace HLSLTree
{

class FNode;
class FExpression;
class FTextureParameterDeclaration;
class FStatement;
class FScope;
class FFunctionCall;
class FTree;
class FCodeWriter;

/**
 * Describes how a given expression needs to be evaluated */
enum class EExpressionEvaluationType
{
	/** Invalid/uninitialized */
	None,

	/** The expression outputs HLSL code (via FExpressionEmitResult::Writer) */
	Shader,

	/** The expression outputs preshader code evaluated at runtime (via FExpressionEmitResult::Preshader) */
	Preshader,

	/** The expression outputs constant preshader code evaluated at compile time (via FExpressionEmitResult::Preshader) */
	Constant,
};

EExpressionEvaluationType CombineEvaluationTypes(EExpressionEvaluationType Lhs, EExpressionEvaluationType Rhs);

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
