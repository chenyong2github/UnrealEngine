// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/EnumClassFlags.h"
#include "Containers/BitArray.h"
#include "Shader/ShaderTypes.h"

class UTexture;

namespace UE
{

namespace Shader
{
enum class EPreshaderOpcode : uint8;
} // namespace Shader

namespace HLSLTree
{

class FNode;
class FStructType;
class FExpression;
class FStatement;
class FScope;
class FTree;

/**
 * Describes how a given expression needs to be evaluated */
enum class EExpressionEvaluation : uint8
{
	/** Invalid/uninitialized */
	None,

	/** Valid, but not yet known */
	Unknown,

	/** The expression outputs HLSL code (via FExpressionEmitResult::Writer) */
	Shader,

	PreshaderLoop,

	/** The expression outputs preshader code evaluated at runtime (via FExpressionEmitResult::Preshader) */
	Preshader,

	ConstantLoop,

	/** The expression outputs constant preshader code evaluated at compile time (via FExpressionEmitResult::Preshader) */
	Constant,

	/** The expression evaluates to 0 */
	ConstantZero,
};

EExpressionEvaluation CombineEvaluations(EExpressionEvaluation Lhs, EExpressionEvaluation Rhs);
EExpressionEvaluation MakeLoopEvaluation(EExpressionEvaluation Evaluation);
EExpressionEvaluation MakeNonLoopEvaluation(EExpressionEvaluation Evaluation);

inline bool IsLoopEvaluation(EExpressionEvaluation Evaluation)
{
	return Evaluation == EExpressionEvaluation::PreshaderLoop || Evaluation == EExpressionEvaluation::ConstantLoop;
}

enum class EOperation : uint8
{
	None,

	// Unary Ops
	Abs,
	Neg,
	Rcp,
	Sqrt,
	Log2,
	Frac,
	Floor,
	Ceil,
	Round,
	Trunc,
	Saturate,
	Sign,
	Length,
	Normalize,
	Sin,
	Cos,
	Tan,
	Asin,
	AsinFast,
	Acos,
	AcosFast,
	Atan,
	AtanFast,

	// Binary Ops
	Add,
	Sub,
	Mul,
	Div,
	Fmod,
	PowPositiveClamped,
	Atan2,
	Atan2Fast,
	Dot,
	Min,
	Max,
	Less,
	Greater,
	LessEqual,
	GreaterEqual,

	VecMulMatrix3,
	VecMulMatrix4,
	Matrix3MulVec,
	Matrix4MulVec,
};

struct FOperationDescription
{
	FOperationDescription();
	FOperationDescription(const TCHAR* InName, const TCHAR* InOperator, int8 InNumInputs, Shader::EPreshaderOpcode InOpcode);

	const TCHAR* Name;
	const TCHAR* Operator;
	int8 NumInputs;
	Shader::EPreshaderOpcode PreshaderOpcode;
};

FOperationDescription GetOperationDescription(EOperation Op);

struct FCustomHLSLInput
{
	FCustomHLSLInput() = default;
	FCustomHLSLInput(FStringView InName, FExpression* InExpression) : Name(InName), Expression(InExpression) {}

	FStringView Name;
	FExpression* Expression = nullptr;
};

} // namespace HLSLTree
} // namespace UE
