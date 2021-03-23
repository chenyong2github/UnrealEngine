// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MaterialShared.h"

#if WITH_EDITORONLY_DATA

class FHLSLMaterialTranslator;

/**
* For a node, the known information of the partial derivatives.
* NotAware	- This node is made by a function that has no knowledge of analytic partial derivatives.
* NotValid	- This node is aware of partial derivatives, and knows that one of its source inputs is not partial derivative aware, and therefore its value is not to be used.
* Zero		- This node is aware of partial derivatives, and knows that it's value is zero, as is the case for uniform parameters.
* Valid		- This node is aware of partial derivatives, and knows that it has a calculated value.
**/
enum class EDerivativeStatus
{
	NotAware,
	NotValid,
	Zero,
	Valid,
};

inline bool IsDerivativeValid(EDerivativeStatus Status)
{
	return Status == EDerivativeStatus::Valid || Status == EDerivativeStatus::Zero;
}

inline bool IsDerivTypeIndexValid(int32 DerivTypeIndex)
{
	return (uint32)DerivTypeIndex < 4u;
}

int32 GetDerivTypeIndex(EMaterialValueType ValueType, bool bAllowNonFloat = false);


struct FDerivInfo
{
	EMaterialValueType	Type;
	int32				TypeIndex;
	EDerivativeStatus	DerivativeStatus;
};

class FMaterialDerivativeAutogen
{
public:
	// Unary functions
	enum class EFunc1
	{
		Abs,
		Log2,
		Log10,
		Exp,
		Sin,
		Cos,
		Tan,
		Asin,
		AsinFast,
		Acos,
		AcosFast,
		Atan,
		AtanFast,
		Sqrt,
		Rcp,
		Rsqrt,
		Saturate,
		Frac,
		Length,
		InvLength,
		Normalize,
		Num
	};

	// Binary functions
	enum class EFunc2
	{
		Add,
		Sub,
		Mul,
		Div,
		Fmod,
		Max,
		Min,
		Dot,	// Depends on Add/Mul, so it must come after them
		Pow,
		PowPositiveClamped,
		Cross,
		Atan2,
		Atan2Fast,
		Num
	};

	int32 GenerateExpressionFunc1(FHLSLMaterialTranslator& Translator, EFunc1 Op, int32 SrcCode);
	int32 GenerateExpressionFunc2(FHLSLMaterialTranslator& Translator, EFunc2 Op, int32 LhsCode, int32 RhsCode);

	int32 GenerateLerpFunc(FHLSLMaterialTranslator& Translator, int32 A, int32 B, int32 S);
	int32 GenerateRotateScaleOffsetTexCoordsFunc(FHLSLMaterialTranslator& Translator, int32 TexCoord, int32 RotationScale, int32 Offset);
	int32 GenerateIfFunc(FHLSLMaterialTranslator& Translator, int32 A, int32 B, int32 Greater, int32 Equal, int32 Less, int32 Threshold);
	
	FString GenerateUsedFunctions(FHLSLMaterialTranslator& Translator);

	FString ApplyUnMirror(FString Value, bool bUnMirrorU, bool bUnMirrorV);

	FString ConstructDeriv(const FString& Value, const FString& Ddx, const FString& Ddy, int32 DstType);
	FString ConstructDerivFinite(const FString& Value, int32 DstType);

	FString ConvertDeriv(const FString& Value, int32 DstType, int32 SrcType);

private:
	// Note that the type index is from [0,3] for float1 to float4. I.e. A float3 would be index 2.
	static int32 GetFunc1ReturnNumComponents(int32 SrcTypeIndex, EFunc1 Op);
	static int32 GetFunc2ReturnNumComponents(int32 LhsTypeIndex, int32 RhsTypeIndex, EFunc2 Op);

	FString CoerceValueRaw(FHLSLMaterialTranslator& Translator, const FString& Token, int32 SrcType, EDerivativeStatus SrcStatus, int32 DstType);
	FString CoerceValueDeriv(const FString& Token, int32 SrcType, EDerivativeStatus SrcStatus, int32 DstType);

	void EnableGeneratedDepencencies();

	// State to keep track of which derivative functions have been used and need to be generated.
	bool bFunc1OpIsEnabled[(int32)EFunc1::Num][4] = {};
	bool bFunc2OpIsEnabled[(int32)EFunc2::Num][4] = {};

	bool bConstructDerivEnabled[4] = {};
	bool bConstructConstantDerivEnabled[4] = {};
	bool bConstructFiniteDerivEnabled[4] = {};

	bool bConvertDerivEnabled[4][4] = {};

	bool bExtractIndexEnabled[4] = {};

	bool bIfEnabled[4] = {};
	bool bIf2Enabled[4] = {};
	bool bLerpEnabled[4] = {};
	bool bRotateScaleOffsetTexCoords = false;
	bool bUnMirrorEnabled[2][2] = {};
	
};

#endif // WITH_EDITORONLY_DATA
