// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLMaterialDerivativeAutogen.h"
#include "HLSLMaterialTranslator.h"

#if WITH_EDITORONLY_DATA

static int32 GDebugGenerateAllFunctionsEnabled = 0;
static FAutoConsoleVariableRef CVarAnalyticDerivDebugGenerateAllFunctions(
	TEXT("r.MaterialEditor.AnalyticDeriv.DebugGenerateAllFunctions"),
	GDebugGenerateAllFunctionsEnabled,
	TEXT("Debug: Generate all derivative functions.")
);

static inline bool IsDebugGenerateAllFunctionsEnabled()
{
	return GDebugGenerateAllFunctionsEnabled != 0;
}

static inline const TCHAR * GetBoolVectorName(int32 TypeIndex)
{
	switch(TypeIndex)
	{
	case 0:
		return TEXT("bool");
	case 1:
		return TEXT("bool2");
	case 2:
		return TEXT("bool3");
	case 3:
		return TEXT("bool4");
	default:
		check(0);
		return TEXT("");
	}
}

static inline const TCHAR * GetFloatVectorName(int32 TypeIndex)
{
	switch(TypeIndex)
	{
	case 0:
		return TEXT("float");
	case 1:
		return TEXT("float2");
	case 2:
		return TEXT("float3");
	case 3:
		return TEXT("float4");
	default:
		check(0);
		return TEXT("");
	}
}

static inline const TCHAR * GetDerivVectorName(int32 TypeIndex)
{
	switch(TypeIndex)
	{
	case 0:
		return TEXT("FloatDeriv");
	case 1:
		return TEXT("FloatDeriv2");
	case 2:
		return TEXT("FloatDeriv3");
	case 3:
		return TEXT("FloatDeriv4");
	default:
		check(0);
		return TEXT("");
	}
}


// 0: Float
// 1: Float2
// 2: Float3
// 3: Float4
// -1: Everything else
int32 GetDerivTypeIndex(EMaterialValueType ValueType, bool bAllowNonFloat)
{
	int32 Ret = INDEX_NONE;
	if (ValueType == MCT_Float1 || ValueType == MCT_Float)
	{
		Ret = 0;
	}
	else if (ValueType == MCT_Float2)
	{
		Ret = 1;
	}
	else if (ValueType == MCT_Float3)
	{
		Ret = 2;
	}
	else if (ValueType == MCT_Float4)
	{
		Ret = 3;
	}
	else
	{
		Ret = INDEX_NONE;
		check(bAllowNonFloat);
	}
	return Ret;
}

static EMaterialValueType GetMaterialTypeFromDerivTypeIndex(int32 Index)
{
	switch (Index)
	{
	case 0:
		return MCT_Float;
	case 1:
		return MCT_Float2;
	case 2:
		return MCT_Float3;
	case 3:
		return MCT_Float4;
	default:
		check(0);
		break;
	}

	return (EMaterialValueType)0; // invalid, should be a Float 1/2/3/4, break at the check(0);
}


static FString CoerceFloat(const TCHAR* Value, int32 DstType, int32 SrcType)
{
	if (DstType == SrcType)
	{
		return Value;
	}

	if (SrcType == 0)
	{
		const TCHAR* Mask[4] = { TEXT("x"), TEXT("xx"), TEXT("xxx"), TEXT("xxxx") };
		return FString::Printf(TEXT("%s.%s"), Value, Mask[DstType]);
	}
	
	if (DstType < SrcType)
	{
		const TCHAR* Mask[4] = { TEXT("x"), TEXT("xy"), TEXT("xyz"), TEXT("xyzw") };
		return FString::Printf(TEXT("%s.%s"), Value, Mask[DstType]);
	}
	else
	{
		check(DstType > SrcType);
		const TCHAR* Zeros[4] = { TEXT("0.0f"), TEXT("0.0f, 0.0f"), TEXT("0.0f, 0.0f, 0.0f"), TEXT("0.0f, 0.0f, 0.0f, 0.0f") };
		return FString::Printf(TEXT("%s(%s, %s)"), GetFloatVectorName(DstType), Value, Zeros[DstType - SrcType - 1]);
	}
}

void FMaterialDerivativeAutogen::EnableGeneratedDepencencies()
{

	for (int32 Index = 0; Index < 4; Index++)
	{
		// PowPositiveClamped requires Pow
		if (bFunc2OpIsEnabled[(int32)EFunc2::PowPositiveClamped][Index])
		{
			bFunc2OpIsEnabled[(int32)EFunc2::Pow][Index] = true;
		}
	}

	for (int32 Index = 0; Index < 4; Index++)
	{
		// normalize requires rsqrt1, dot, expand, and mul
		if (bFunc1OpIsEnabled[(int32)EFunc1::Normalize][Index])
		{
			bConvertDerivEnabled[Index][0] = true;
			bFunc2OpIsEnabled[(int32)EFunc2::Dot][Index] = true;
			bFunc1OpIsEnabled[(int32)EFunc1::Rsqrt][0] = true;
			bFunc2OpIsEnabled[(int32)EFunc2::Mul][Index] = true;
		}

		// length requires sqrt1 and dot, dot requires a few other things, but those are handled below
		if (bFunc1OpIsEnabled[(int32)EFunc1::Length][Index])
		{
			bFunc2OpIsEnabled[(int32)EFunc2::Dot][Index] = true;
			bFunc1OpIsEnabled[(int32)EFunc1::Sqrt][0] = true;
		}

		// inv length requires rsqrt1 (instead of sqrt1) and dot
		if (bFunc1OpIsEnabled[(int32)EFunc1::InvLength][Index])
		{
			bFunc2OpIsEnabled[(int32)EFunc2::Dot][Index] = true;
			bFunc1OpIsEnabled[(int32)EFunc1::Rsqrt][0] = true;
		}
	}

	// Dot requires extract, mul1, add1 and FloatDeriv constructor
	for (int32 Index = 0; Index < 4; Index++)
	{
		if (bFunc2OpIsEnabled[(int32)EFunc2::Dot][Index])
		{
			bExtractIndexEnabled[Index] = true;
			bConstructConstantDerivEnabled[0] = true;
			bFunc2OpIsEnabled[(int32)EFunc2::Add][0] = true;
			bFunc2OpIsEnabled[(int32)EFunc2::Mul][0] = true;
		}
	}

	if (bRotateScaleOffsetTexCoords)
	{
		bFunc2OpIsEnabled[(int32)EFunc2::Add][1] = true;
		bFunc2OpIsEnabled[(int32)EFunc2::Mul][1] = true;
		bConstructDerivEnabled[1] = true;
	}
}

// given a string, convert it from type to type
FString FMaterialDerivativeAutogen::CoerceValueRaw(const FString& Token, int32 SrcType, EDerivativeStatus SrcStatus, int32 DstType)
{
	check(IsDerivTypeIndexValid(SrcType));
	check(IsDerivTypeIndexValid(DstType));

	FString Ret = Token;

	// If the original value is a derivative, grab the raw value
	if (SrcStatus == EDerivativeStatus::Valid)
	{
		Ret = Ret + TEXT(".Value");
	}

	if (SrcType == DstType)
	{
		// no op
	}
	else
	{
		check(SrcType == 0); // can only coerce a float1
		if (DstType == 0)
		{
			Ret = FString::Printf( TEXT("MaterialFloat(%s)"), *Ret);
		}
		else if (DstType == 1)
		{
			Ret = FString::Printf( TEXT("MaterialFloat2(%s,%s)"), *Ret, *Ret);
		}
		else if (DstType == 2)
		{
			Ret = FString::Printf( TEXT("MaterialFloat3(%s,%s,%s)"), *Ret, *Ret, *Ret);
		}
		else if (DstType == 3)
		{
			Ret = FString::Printf( TEXT("MaterialFloat4(%s,%s,%s,%s)"), *Ret, *Ret, *Ret, *Ret);
		}
		else
		{
			check(0);
		}
	}
	return Ret;
}

// given a string, convert it from type to type
FString FMaterialDerivativeAutogen::CoerceValueDeriv(const FString& Token, int32 SrcType, EDerivativeStatus SrcStatus, int32 DstType)
{
	check(IsDerivTypeIndexValid(SrcType));
	check(IsDerivTypeIndexValid(DstType));

	FString Ret = Token;

	check(IsDerivativeValid(SrcStatus));

	// If it's valid, then it's already a type. Otherwise, we need to convert it from raw to deriv
	if (SrcStatus == EDerivativeStatus::Zero)
	{
		FString SrcDerivType = GetDerivVectorName(SrcType);
		bConstructConstantDerivEnabled[SrcType] = true;
		Ret = TEXT("ConstructConstant") + SrcDerivType + TEXT("(") + Ret + TEXT(")");
	}

	return ConvertDeriv(Ret, DstType, SrcType);
}

FString FMaterialDerivativeAutogen::ConstructDeriv(const FString& Value, const FString& Ddx, const FString& Ddy, int32 DstType)
{
	check(IsDerivTypeIndexValid(DstType));

	bConstructDerivEnabled[DstType] = true;
	FString TypeName = GetDerivVectorName(DstType);
	FString Ret = TEXT("Construct") + TypeName + TEXT("(") + Value + TEXT(",") + Ddx + TEXT(",") + Ddy + TEXT(")");
	return Ret;
}

FString FMaterialDerivativeAutogen::ConstructDerivFinite(const FString& Value, int32 DstType)
{
	check(IsDerivTypeIndexValid(DstType));

	bConstructFiniteDerivEnabled[DstType] = true;

	FString TypeName = GetDerivVectorName(DstType);
	FString Ret = TEXT("ConstructFinite") + TypeName + TEXT("(") + Value + TEXT(")");
	return Ret;
}

FString FMaterialDerivativeAutogen::ConvertDeriv(const FString& Value, int32 DstType, int32 SrcType)
{
	check(IsDerivTypeIndexValid(DstType));
	check(IsDerivTypeIndexValid(SrcType));

	if (DstType == SrcType)
		return Value;
	
	bConvertDerivEnabled[DstType][SrcType] = true;

	FString DstTypeName = GetDerivVectorName(DstType);
	return FString::Printf(TEXT("Convert%s(%s)"), *DstTypeName, *Value);
}

int32 FMaterialDerivativeAutogen::GetFunc1ReturnNumComponents(int32 SrcTypeIndex, EFunc1 Op)
{
	check(IsDerivTypeIndexValid(SrcTypeIndex));

	int32 DstTypeIndex = INDEX_NONE;

	switch(Op)
	{
	case EFunc1::Abs:
	case EFunc1::Log2:
	case EFunc1::Log10:
	case EFunc1::Exp:
	case EFunc1::Sin:
	case EFunc1::Cos:
	case EFunc1::Tan:
	case EFunc1::Asin:
	case EFunc1::AsinFast:
	case EFunc1::Acos:
	case EFunc1::AcosFast:
	case EFunc1::Atan:
	case EFunc1::AtanFast:
	case EFunc1::Sqrt:
	case EFunc1::Rcp:
	case EFunc1::Rsqrt:
	case EFunc1::Saturate:
	case EFunc1::Frac:
	case EFunc1::Normalize:
		DstTypeIndex = SrcTypeIndex;
		break;

	case EFunc1::Length:
	case EFunc1::InvLength:
		DstTypeIndex = 0;
		break;
	default:
		check(0);
		break;
	}

	return DstTypeIndex;
}

int32 FMaterialDerivativeAutogen::GetFunc2ReturnNumComponents(int32 LhsTypeIndex, int32 RhsTypeIndex, EFunc2 Op)
{
	check(IsDerivTypeIndexValid(LhsTypeIndex));
	check(IsDerivTypeIndexValid(RhsTypeIndex));

	int32 DstTypeIndex = INDEX_NONE;
	
	switch(Op)
	{
	case EFunc2::Add:
	case EFunc2::Sub:
	case EFunc2::Mul:
	case EFunc2::Div:
	case EFunc2::Fmod:
	case EFunc2::Max:
	case EFunc2::Min:
	case EFunc2::Pow:
	case EFunc2::PowPositiveClamped:
	case EFunc2::Atan2:
	case EFunc2::Atan2Fast:
		// if the initial type is different from the output type, then it's only valid if type is 0 (float). We can convert
		// a float to a type with more components, but for example, we can't implicitly convert a float2 to a float3/float4.
		if (LhsTypeIndex == RhsTypeIndex || RhsTypeIndex == 0 || LhsTypeIndex == 0)
		{
			DstTypeIndex = FMath::Max<int32>(LhsTypeIndex, RhsTypeIndex);
		}
		break;
	case EFunc2::Dot:
		DstTypeIndex = 0;
		break;
	case EFunc2::Cross:
		check(LhsTypeIndex == 2);
		check(RhsTypeIndex == 2);
		DstTypeIndex = 2;
		break;
	default:
		check(0);
		break;
	}

	return DstTypeIndex;
}


int32 FMaterialDerivativeAutogen::GenerateExpressionFunc1(FHLSLMaterialTranslator& Translator, EFunc1 Op, int32 SrcCode)
{
	if (SrcCode == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const FDerivInfo SrcDerivInfo = Translator.GetDerivInfo(SrcCode);
	int32 OutputTypeIndex	= GetFunc1ReturnNumComponents(SrcDerivInfo.TypeIndex, Op);

	if (OutputTypeIndex < 0)
	{
		return INDEX_NONE;
	}

	EDerivativeStatus DstStatus = IsDerivativeValid(SrcDerivInfo.DerivativeStatus) ? EDerivativeStatus::Valid : EDerivativeStatus::NotValid;
	bool bUseScalarVersion = (DstStatus != EDerivativeStatus::Valid);

	// make initial tokens
	FString DstTokens[CompiledPDV_MAX];

	for (int32 Index = 0; Index < CompiledPDV_MAX; Index++)
	{
		ECompiledPartialDerivativeVariation Variation = (ECompiledPartialDerivativeVariation)Index;

		FString SrcToken = Translator.GetParameterCodeDeriv(SrcCode,Variation);

		// The token is the symbol name. If we are in finite mode, that's all we have to do. But if
		// we are in analytic mode, we may need to get the value.
		if (Index == CompiledPDV_Analytic)
		{
			SrcToken = CoerceValueRaw(SrcToken, SrcDerivInfo.TypeIndex, SrcDerivInfo.DerivativeStatus, SrcDerivInfo.TypeIndex);
		}

		FString DstToken;
		// just generate a type
		switch(Op)
		{
		case EFunc1::Abs:
			DstToken = TEXT("abs(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Log2:
			DstToken = TEXT("log2(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Log10:
			DstToken = TEXT("log10(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Exp:
			DstToken = TEXT("exp(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Sin:
			DstToken = TEXT("sin(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Cos:
			DstToken = TEXT("cos(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Tan:
			DstToken = TEXT("tan(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Asin:
			DstToken = TEXT("asin(") + SrcToken + TEXT(")");
			break;
		case EFunc1::AsinFast:
			DstToken = TEXT("asinFast(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Acos:
			DstToken = TEXT("acos(") + SrcToken + TEXT(")");
			break;
		case EFunc1::AcosFast:
			DstToken = TEXT("acosFast(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Atan:
			DstToken = TEXT("atan(") + SrcToken + TEXT(")");
			break;
		case EFunc1::AtanFast:
			DstToken = TEXT("atanFast(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Sqrt:
			DstToken = TEXT("sqrt(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Rcp:
			DstToken = TEXT("rcp(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Rsqrt:
			DstToken = TEXT("rsqrt(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Saturate:
			DstToken = TEXT("saturate(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Frac:
			DstToken = TEXT("frac(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Length:
			DstToken = TEXT("length(") + SrcToken + TEXT(")");
			break;
		case EFunc1::InvLength:
			DstToken = TEXT("rcp(length(") + SrcToken + TEXT("))");
			break;
		case EFunc1::Normalize:
			DstToken = TEXT("normalize(") + SrcToken + TEXT(")");
			break;
		default:
			check(0);
			break;
		}

		DstTokens[Index] = DstToken;
	}

	if (!bUseScalarVersion)
	{
		FString SrcToken = Translator.GetParameterCodeDeriv(SrcCode, CompiledPDV_Analytic);

		SrcToken = CoerceValueDeriv(SrcToken, SrcDerivInfo.TypeIndex, SrcDerivInfo.DerivativeStatus, SrcDerivInfo.TypeIndex);

		check(Op < EFunc1::Num);
		bFunc1OpIsEnabled[(int32)Op][SrcDerivInfo.TypeIndex] = true;
		
		FString DstToken;
		switch(Op)
		{
		case EFunc1::Abs:
			DstToken = TEXT("AbsDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Log2:
			DstToken = TEXT("Log2Deriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Log10:
			DstToken = TEXT("Log10Deriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Exp:
			DstToken = TEXT("ExpDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Sin:
			DstToken = TEXT("SinDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Cos:
			DstToken = TEXT("CosDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Tan:
			DstToken = TEXT("TanDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Asin:
			DstToken = TEXT("ASinDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::AsinFast:
			DstToken = TEXT("ASinFastDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Acos:
			DstToken = TEXT("ACosDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::AcosFast:
			DstToken = TEXT("ACosFastDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Atan:
			DstToken = TEXT("ATanDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::AtanFast:
			DstToken = TEXT("ATanFastDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Sqrt:
			DstToken = TEXT("SqrtDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Rcp:
			DstToken = TEXT("RcpDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Rsqrt:
			DstToken = TEXT("RsqrtDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Saturate:
			DstToken = TEXT("SaturateDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Frac:
			DstToken = TEXT("FracDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Length:
			DstToken = TEXT("LengthDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::InvLength:
			DstToken = TEXT("InvLengthDeriv(") + SrcToken + TEXT(")");
			break;
		case EFunc1::Normalize:
			DstToken = TEXT("NormalizeDeriv(") + SrcToken + TEXT(")");
			break;
		default:
			check(0);
			break;
		}

		DstTokens[CompiledPDV_Analytic] = DstToken;
	}

	EMaterialValueType DstMatType = GetMaterialTypeFromDerivTypeIndex(OutputTypeIndex);
	int32 Ret = Translator.AddCodeChunkInnerDeriv(*DstTokens[CompiledPDV_FiniteDifferences],*DstTokens[CompiledPDV_Analytic],DstMatType,false /*?*/, DstStatus);
	return Ret;
}

int32 FMaterialDerivativeAutogen::GenerateExpressionFunc2(FHLSLMaterialTranslator& Translator, EFunc2 Op, int32 LhsCode, int32 RhsCode)
{
	if (LhsCode == INDEX_NONE || RhsCode == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const FDerivInfo LhsDerivInfo = Translator.GetDerivInfo(LhsCode);
	const FDerivInfo RhsDerivInfo = Translator.GetDerivInfo(RhsCode);

	int32 IntermediaryTypeIndex = FMath::Max<int32>(LhsDerivInfo.TypeIndex, RhsDerivInfo.TypeIndex);

	if (Op == EFunc2::Cross)
	{
		IntermediaryTypeIndex = 2;
	}

	if (Op == EFunc2::Fmod)
	{
		check(RhsDerivInfo.DerivativeStatus == EDerivativeStatus::Zero);
	}

	int32 OutputTypeIndex = GetFunc2ReturnNumComponents(LhsDerivInfo.TypeIndex, RhsDerivInfo.TypeIndex, Op);

	if (OutputTypeIndex < 0)
	{
		return INDEX_NONE;
	}

	EDerivativeStatus DstStatus = EDerivativeStatus::NotValid;

	// Rules for derivatives:
	// 1. If either the LHS or RHS is Not Valid or Not Aware, then the derivative is not valid. Run scalar route.
	// 2. If both LHS and RHS are known to be Zero, then run raw code, and specify a known zero status.
	// 3. If both LHS and RHS are Valid derivatives, then run deriv path.
	// 4. If one is Valid and the other is known Zero, then promote the Zero to Valid, and run deriv path.

	bool bUseScalarVersion = false;
	bool bIsDerivValidZero = true;
	bool bMakeDerivLhs = false;
	bool bMakeDerivRhs = false;
	if (!IsDerivativeValid(LhsDerivInfo.DerivativeStatus) || !IsDerivativeValid(RhsDerivInfo.DerivativeStatus))
	{
		// use scalar version as a fallback
		bUseScalarVersion = true;
		// derivative is not valid
		bIsDerivValidZero = false;

		// We output status as invalid, since one of the parameters is either not aware or not valid
		DstStatus = EDerivativeStatus::NotValid;

	}
	else if (LhsDerivInfo.DerivativeStatus == EDerivativeStatus::Zero && RhsDerivInfo.DerivativeStatus == EDerivativeStatus::Zero)
	{
		// use scalar version
		bUseScalarVersion = true;
		// since we know both incoming values have derivatives of zero, we know the output is zero
		bIsDerivValidZero = true;
		// we know that the value is zero
		DstStatus = EDerivativeStatus::Zero;
	}
	else
	{
		check(IsDerivativeValid(LhsDerivInfo.DerivativeStatus));
		check(IsDerivativeValid(RhsDerivInfo.DerivativeStatus));

		// use deriv version
		bUseScalarVersion = false;
		// we will be calculating a valid derivative, so this value doesn't matter, but make it false for consistency
		bIsDerivValidZero = false;

		// if the lhs has a derivitive of zero, and rhs is non-zero, convert lhs from a scalar type to deriv type
		if (LhsDerivInfo.DerivativeStatus == EDerivativeStatus::Zero)
		{
			bMakeDerivLhs = true;
		}

		// if the rhs has a derivative of zero, and lhs is non-zero, convert rhs from a scalar type to deriv type
		if (RhsDerivInfo.DerivativeStatus == EDerivativeStatus::Zero)
		{
			bMakeDerivRhs = true;
		}

		// derivative results will be valid
		DstStatus = EDerivativeStatus::Valid;
	}

	FString DstTokens[CompiledPDV_MAX];

	for (int32 Index = 0; Index < CompiledPDV_MAX; Index++)
	{
		ECompiledPartialDerivativeVariation Variation = (ECompiledPartialDerivativeVariation)Index;

		FString LhsToken = Translator.GetParameterCodeDeriv(LhsCode,Variation);
		FString RhsToken = Translator.GetParameterCodeDeriv(RhsCode,Variation);

		// The token is the symbol name. If we are in finite mode, that's all we have to do. But if
		// we are in analytic mode, we may need to get the value.
		if (Index == CompiledPDV_Analytic)
		{
			LhsToken = CoerceValueRaw(LhsToken, LhsDerivInfo.TypeIndex, LhsDerivInfo.DerivativeStatus, IntermediaryTypeIndex);
			RhsToken = CoerceValueRaw(RhsToken, RhsDerivInfo.TypeIndex, RhsDerivInfo.DerivativeStatus, IntermediaryTypeIndex);
		}

		FString DstToken;
		// just generate a type
		switch(Op)
		{
		case EFunc2::Add:
			DstToken = TEXT("(") + LhsToken + TEXT(" + ") + RhsToken + TEXT(")");
			break;
		case EFunc2::Sub:
			DstToken = TEXT("(") + LhsToken + TEXT(" - ") + RhsToken + TEXT(")");
			break;
		case EFunc2::Mul:
			DstToken = TEXT("(") + LhsToken + TEXT(" * ") + RhsToken + TEXT(")");
			break;
		case EFunc2::Div:
			DstToken = TEXT("(") + LhsToken + TEXT(" / ") + RhsToken + TEXT(")");
			break;
		case EFunc2::Fmod:
			DstToken = TEXT("fmod(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Min:
			DstToken = TEXT("min(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Max:
			DstToken = TEXT("max(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Dot:
			DstToken = TEXT("dot(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Pow:
			DstToken = TEXT("pow(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::PowPositiveClamped:
			DstToken = TEXT("PositiveClampedPow(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Atan2:
			DstToken = TEXT("atan2(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Atan2Fast:
			DstToken = TEXT("atan2Fast(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Cross:
			DstToken = TEXT("cross(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		default:
			check(0);
			break;
		}

		DstTokens[Index] = DstToken;
	}

	if (!bUseScalarVersion)
	{
		FString LhsToken = Translator.GetParameterCodeDeriv(LhsCode,CompiledPDV_Analytic);
		FString RhsToken = Translator.GetParameterCodeDeriv(RhsCode,CompiledPDV_Analytic);

		LhsToken = CoerceValueDeriv(LhsToken, LhsDerivInfo.TypeIndex, LhsDerivInfo.DerivativeStatus, IntermediaryTypeIndex);
		RhsToken = CoerceValueDeriv(RhsToken, RhsDerivInfo.TypeIndex, RhsDerivInfo.DerivativeStatus, IntermediaryTypeIndex);

		check(Op < EFunc2::Num);
		bFunc2OpIsEnabled[(int32)Op][IntermediaryTypeIndex] = true;

		FString DstToken;
		switch(Op)
		{
		case EFunc2::Add:
			DstToken = TEXT("AddDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Sub:
			DstToken = TEXT("SubDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Mul:
			DstToken = TEXT("MulDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Div:
			DstToken = TEXT("DivDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Fmod:
			DstToken = TEXT("FmodDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Dot:
			DstToken = TEXT("DotDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Min:
			DstToken = TEXT("MinDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Max:
			DstToken = TEXT("MaxDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Pow:
			DstToken = TEXT("PowDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::PowPositiveClamped:
			DstToken = TEXT("PowPositiveClampedDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Cross:
			DstToken = TEXT("CrossDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Atan2:
			DstToken = TEXT("Atan2Deriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		case EFunc2::Atan2Fast:
			DstToken = TEXT("Atan2FastDeriv(") + LhsToken + TEXT(",") + RhsToken + TEXT(")");
			break;
		default:
			check(0);
			break;
		}

		DstTokens[CompiledPDV_Analytic] = DstToken;
		DstStatus = EDerivativeStatus::Valid;
	}

	EMaterialValueType DstMatType = GetMaterialTypeFromDerivTypeIndex(OutputTypeIndex);

	int32 Ret = Translator.AddCodeChunkInnerDeriv(*DstTokens[CompiledPDV_FiniteDifferences],*DstTokens[CompiledPDV_Analytic],DstMatType,false /*?*/, DstStatus);
	return Ret;
}

int32 FMaterialDerivativeAutogen::GenerateLerpFunc(FHLSLMaterialTranslator& Translator, int32 A, int32 B, int32 S)
{
	// TODO: generalize to Func3?
	if (A == INDEX_NONE || B == INDEX_NONE || S == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const FDerivInfo ADerivInfo = Translator.GetDerivInfo(A);
	const FDerivInfo BDerivInfo = Translator.GetDerivInfo(B);
	const FDerivInfo SDerivInfo = Translator.GetDerivInfo(S);
	
	const EMaterialValueType ResultType = Translator.GetArithmeticResultType(A, B);
	const EMaterialValueType AlphaType = (ResultType == SDerivInfo.Type) ? ResultType : MCT_Float1;
	const uint32 NumResultComponents = GetNumComponents(ResultType);

	const bool bAllZeroDeriv = (ADerivInfo.DerivativeStatus == EDerivativeStatus::Zero && BDerivInfo.DerivativeStatus == EDerivativeStatus::Zero && SDerivInfo.DerivativeStatus == EDerivativeStatus::Zero);
	FString FiniteString = FString::Printf(TEXT("lerp(%s,%s,%s)"), *Translator.CoerceParameter(A, ResultType), *Translator.CoerceParameter(B, ResultType), *Translator.CoerceParameter(S, AlphaType));
	
	if (!bAllZeroDeriv && IsDerivativeValid(ADerivInfo.DerivativeStatus) && IsDerivativeValid(BDerivInfo.DerivativeStatus) && IsDerivativeValid(SDerivInfo.DerivativeStatus))
	{
		const uint32 ResultTypeIndex = GetDerivTypeIndex(ResultType);
		//const uint32 AlphaTypeIndex = GetDerivTypeIndex(AlphaType);
		FString ADeriv = Translator.GetParameterCodeDeriv(A, CompiledPDV_Analytic);
		FString BDeriv = Translator.GetParameterCodeDeriv(B, CompiledPDV_Analytic);
		FString SDeriv = Translator.GetParameterCodeDeriv(S, CompiledPDV_Analytic);

		ADeriv = CoerceValueDeriv(ADeriv, ADerivInfo.TypeIndex, ADerivInfo.DerivativeStatus, ResultTypeIndex);
		BDeriv = CoerceValueDeriv(BDeriv, BDerivInfo.TypeIndex, BDerivInfo.DerivativeStatus, ResultTypeIndex);
		SDeriv = CoerceValueDeriv(SDeriv, SDerivInfo.TypeIndex, SDerivInfo.DerivativeStatus, ResultTypeIndex);

		FString AnalyticString = FString::Printf(TEXT("LerpDeriv(%s, %s, %s)"), *ADeriv, *BDeriv, *SDeriv);

		check(NumResultComponents <= 4);
		bLerpEnabled[ResultTypeIndex] = true;

		return Translator.AddCodeChunkInnerDeriv(*FiniteString, *AnalyticString, ResultType, false, EDerivativeStatus::Valid);
	}
	else
	{
		return Translator.AddCodeChunkInnerDeriv(*FiniteString, *FiniteString, ResultType, false, bAllZeroDeriv ? EDerivativeStatus::Zero : EDerivativeStatus::NotValid);
	}
}

int32 FMaterialDerivativeAutogen::GenerateRotateScaleOffsetTexCoordsFunc(FHLSLMaterialTranslator& Translator, int32 TexCoord, int32 RotationScale, int32 Offset)
{
	// TODO: generalize to Func3?
	if (TexCoord == INDEX_NONE || RotationScale == INDEX_NONE || Offset == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const FDerivInfo TexCoordDerivInfo		= Translator.GetDerivInfo(TexCoord);
	const FDerivInfo RotationScaleDerivInfo	= Translator.GetDerivInfo(RotationScale);
	const FDerivInfo OffsetDerivInfo		= Translator.GetDerivInfo(Offset);

	const EMaterialValueType ResultType = MCT_Float2;
	const uint32 NumResultComponents = 2;

	const bool bAllZeroDeriv = (TexCoordDerivInfo.DerivativeStatus == EDerivativeStatus::Zero && RotationScaleDerivInfo.DerivativeStatus == EDerivativeStatus::Zero && OffsetDerivInfo.DerivativeStatus == EDerivativeStatus::Zero);
	FString FiniteString = FString::Printf(TEXT("RotateScaleOffsetTexCoords(%s, %s, %s.xy)"),	*Translator.CoerceParameter(TexCoord,		ResultType), 
																								*Translator.CoerceParameter(RotationScale,	ResultType),
																								*Translator.CoerceParameter(Offset,			ResultType));

	if (!bAllZeroDeriv && IsDerivativeValid(TexCoordDerivInfo.DerivativeStatus) && IsDerivativeValid(RotationScaleDerivInfo.DerivativeStatus) && IsDerivativeValid(OffsetDerivInfo.DerivativeStatus))
	{
		const uint32 ResultTypeIndex = GetDerivTypeIndex(ResultType);
		FString TexCoordDeriv		= Translator.GetParameterCodeDeriv(TexCoord, CompiledPDV_Analytic);
		FString RotationScaleDeriv	= Translator.GetParameterCodeDeriv(RotationScale, CompiledPDV_Analytic);
		FString OffsetDeriv			= Translator.GetParameterCodeDeriv(Offset, CompiledPDV_Analytic);

		TexCoordDeriv		= CoerceValueDeriv(TexCoordDeriv,		TexCoordDerivInfo.TypeIndex,		TexCoordDerivInfo.DerivativeStatus,			ResultTypeIndex);
		RotationScaleDeriv	= CoerceValueDeriv(RotationScaleDeriv,	RotationScaleDerivInfo.TypeIndex,	RotationScaleDerivInfo.DerivativeStatus,	ResultTypeIndex);
		OffsetDeriv			= CoerceValueDeriv(OffsetDeriv,			OffsetDerivInfo.TypeIndex,			OffsetDerivInfo.DerivativeStatus,			ResultTypeIndex);

		FString AnalyticString = FString::Printf(TEXT("RotateScaleOffsetTexCoordsDeriv(%s, %s, %s)"), *TexCoordDeriv, *RotationScaleDeriv, *OffsetDeriv);

		check(NumResultComponents <= 4);
		bRotateScaleOffsetTexCoords = true;

		return Translator.AddCodeChunkInnerDeriv(*FiniteString, *AnalyticString, ResultType, false, EDerivativeStatus::Valid);
	}
	else
	{
		return Translator.AddCodeChunkInnerDeriv(*FiniteString, *FiniteString, ResultType, false, bAllZeroDeriv ? EDerivativeStatus::Zero : EDerivativeStatus::NotValid);
	}
}

int32 FMaterialDerivativeAutogen::GenerateIfFunc(FHLSLMaterialTranslator& Translator, int32 A, int32 B, int32 Greater, int32 Equal, int32 Less, int32 Threshold)
{
	if (A == INDEX_NONE || B == INDEX_NONE || Greater == INDEX_NONE || Less == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const FString AFinite = Translator.GetParameterCode(A);
	const FString BFinite = Translator.GetParameterCode(B);

	const bool bEqual = (Equal != INDEX_NONE);

	if (bEqual && Threshold == INDEX_NONE)
		return INDEX_NONE;

	EMaterialValueType ResultType = Translator.GetArithmeticResultType(Greater, Less);

	if (bEqual)
		ResultType = Translator.GetArithmeticResultType(ResultType, Translator.GetParameterType(Greater));
	
	Greater = Translator.ForceCast(Greater,		ResultType);
	Less	= Translator.ForceCast(Less,		ResultType);
	if (Greater == INDEX_NONE || Less == INDEX_NONE)
		return INDEX_NONE;

	const FString GreaterFinite = Translator.GetParameterCode(Greater);
	const FString LessFinite	= Translator.GetParameterCode(Less);

	if (bEqual)
	{
		Equal = Translator.ForceCast(Equal, ResultType);
		if (Equal == INDEX_NONE)
			return INDEX_NONE;
	}
	
	const FDerivInfo GreaterDerivInfo	= Translator.GetDerivInfo(Greater, true);
	const FDerivInfo LessDerivInfo		= Translator.GetDerivInfo(Less, true);

	FString CodeFinite;
	FString ThresholdFinite;
	if (bEqual)
	{
		ThresholdFinite = *Translator.GetParameterCode(Threshold);

		CodeFinite = FString::Printf(
			TEXT("((abs(%s - %s) > %s) ? (%s >= %s ? %s : %s) : %s)"),
			*AFinite, *BFinite, *ThresholdFinite,
			*AFinite, *BFinite,
			*GreaterFinite, *LessFinite, *Translator.GetParameterCode(Equal));
	}
	else
	{
		CodeFinite = FString::Printf(
			TEXT("((%s >= %s) ? %s : %s)"),
			*AFinite, *BFinite,
			*GreaterFinite, *LessFinite
		);
	}

	const bool bAllDerivValid = IsDerivativeValid(GreaterDerivInfo.DerivativeStatus) && IsDerivativeValid(LessDerivInfo.DerivativeStatus) && (!bEqual || IsDerivativeValid(Translator.GetDerivativeStatus(Equal)));
	const bool bAllDerivZero = GreaterDerivInfo.DerivativeStatus == EDerivativeStatus::Zero && LessDerivInfo.DerivativeStatus == EDerivativeStatus::Zero && (!bEqual || Translator.GetDerivativeStatus(Equal) == EDerivativeStatus::Zero);
	if (bAllDerivValid && !bAllDerivZero)
	{
		const uint32 ResultTypeIndex = GetDerivTypeIndex(ResultType);

		FString GreaterDeriv	= Translator.GetParameterCodeDeriv(Greater, CompiledPDV_Analytic);
		FString LessDeriv		= Translator.GetParameterCodeDeriv(Less,	CompiledPDV_Analytic);

		GreaterDeriv	= CoerceValueDeriv(GreaterDeriv,	ResultTypeIndex, GreaterDerivInfo.DerivativeStatus, ResultTypeIndex);
		LessDeriv		= CoerceValueDeriv(LessDeriv,		ResultTypeIndex, LessDerivInfo.DerivativeStatus,	ResultTypeIndex);

		FString CodeAnalytic;
		if (bEqual)
		{
			const FDerivInfo EqualDerivInfo = Translator.GetDerivInfo(Equal, true);
			FString EqualDeriv = Translator.GetParameterCodeDeriv(Equal, CompiledPDV_Analytic);
			EqualDeriv = CoerceValueDeriv(EqualDeriv, ResultTypeIndex, EqualDerivInfo.DerivativeStatus, ResultTypeIndex);

			CodeAnalytic = FString::Printf(TEXT("IfDeriv(%s, %s, %s, %s, %s, %s)"), *AFinite, *BFinite, *GreaterDeriv, *LessDeriv, *EqualDeriv, *ThresholdFinite);
			bIf2Enabled[ResultTypeIndex] = true;
		}
		else
		{
			CodeAnalytic = FString::Printf(TEXT("IfDeriv(%s, %s, %s, %s)"), *AFinite, *BFinite, *GreaterDeriv, *LessDeriv);
			bIfEnabled[ResultTypeIndex] = true;
		}

		return Translator.AddCodeChunkInnerDeriv(*CodeFinite, *CodeAnalytic, ResultType, false, EDerivativeStatus::Valid);

	}
	else
	{
		return Translator.AddCodeChunkInnerDeriv(*CodeFinite, *CodeFinite, ResultType, false, bAllDerivZero ? EDerivativeStatus::Zero : EDerivativeStatus::NotValid);
	}
}

FString FMaterialDerivativeAutogen::GenerateUsedFunctions(FHLSLMaterialTranslator& Translator)
{
	// Certain derivative functions rely on other derivative functions. For example, Dot() requires Mul() and Add(). So if (for example) dot is enabled, then enable mul1 and add1.
	EnableGeneratedDepencencies();

	FString Ret;

	// The basic structs (FloatDeriv, FloatDeriv2, FloatDeriv3, FloatDeriv4)
	// It's not worth keeping track of all the times these are used, just make them.
	for (int32 Index = 0; Index < 4; Index++)
	{
		FString BaseName = GetDerivVectorName(Index);
		FString FieldName = GetFloatVectorName(Index);

		Ret += TEXT("struct ") + BaseName + LINE_TERMINATOR;
		Ret += TEXT("{") LINE_TERMINATOR;
		Ret += TEXT("\t") + FieldName + TEXT(" Value;") LINE_TERMINATOR;
		Ret += TEXT("\t") + FieldName + TEXT(" Ddx;") LINE_TERMINATOR;
		Ret += TEXT("\t") + FieldName + TEXT(" Ddy;") LINE_TERMINATOR;
		Ret += TEXT("};") LINE_TERMINATOR;
		Ret += TEXT("") LINE_TERMINATOR;
	}

	// Full FloatDerivX constructors with explicit derivatives.
	for (int32 Index = 0; Index < 4; Index++)
	{
		if (bConstructDerivEnabled[Index] || IsDebugGenerateAllFunctionsEnabled())
		{
			FString BaseName = GetDerivVectorName(Index);
			FString FieldName = GetFloatVectorName(Index);

			Ret += BaseName + TEXT(" Construct") + BaseName + TEXT("(") + FieldName + TEXT(" InValue,") + FieldName + TEXT(" InDdx,") + FieldName + TEXT(" InDdy)") LINE_TERMINATOR;
			Ret += TEXT("{") LINE_TERMINATOR;
			Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Value = InValue;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddx = InDdx;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddy = InDdy;") LINE_TERMINATOR;
			Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
			Ret += TEXT("}") LINE_TERMINATOR;
			Ret += TEXT("") LINE_TERMINATOR;
		}
	}

	// FloatDerivX constructors from constant floatX.
	for (int32 Index = 0; Index < 4; Index++)
	{
		if (bConstructConstantDerivEnabled[Index] || IsDebugGenerateAllFunctionsEnabled())
		{
			FString BaseName = GetDerivVectorName(Index);
			FString FieldName = GetFloatVectorName(Index);

			Ret += BaseName + TEXT(" ConstructConstant") + BaseName + TEXT("(") + FieldName + TEXT(" Value)") LINE_TERMINATOR;
			Ret += TEXT("{") LINE_TERMINATOR;
			Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Value = Value;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddx = 0;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddy = 0;") LINE_TERMINATOR;
			Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
			Ret += TEXT("}") LINE_TERMINATOR;
			Ret += TEXT("") LINE_TERMINATOR;
		}
	}

	// FloatDerivX constructor from floatX with implicit derivatives.
	for (int32 Index = 0; Index < 4; Index++)
	{
		if (bConstructFiniteDerivEnabled[Index] || IsDebugGenerateAllFunctionsEnabled())
		{
			FString BaseName = GetDerivVectorName(Index);
			FString FieldName = GetFloatVectorName(Index);

			Ret += BaseName + TEXT(" ConstructFinite") + BaseName + TEXT("(") + FieldName + TEXT(" InValue)") LINE_TERMINATOR;
			Ret += TEXT("{") LINE_TERMINATOR;
			Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Value = InValue;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddx = ddx(InValue);") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddy = ddy(InValue);") LINE_TERMINATOR;
			Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
			Ret += TEXT("}") LINE_TERMINATOR;
			Ret += TEXT("") LINE_TERMINATOR;
		}
	}

	// Convert between FloatDeriv types
	for (int32 DstIndex = 0; DstIndex < 4; DstIndex++)
	{
		for (int32 SrcIndex = 0; SrcIndex < 4; SrcIndex++)
		{
			if (SrcIndex == DstIndex)
				continue;

			if (bConvertDerivEnabled[DstIndex][SrcIndex] || IsDebugGenerateAllFunctionsEnabled())
			{
				FString DstBaseName = GetDerivVectorName(DstIndex);
				FString SrcBaseName = GetDerivVectorName(SrcIndex);
				FString ScalarName = GetDerivVectorName(0);

				Ret += DstBaseName + TEXT(" Convert") + DstBaseName + TEXT("(") + SrcBaseName + TEXT(" Src)") LINE_TERMINATOR;
				Ret += TEXT("{") LINE_TERMINATOR;
				Ret += TEXT("\t") + DstBaseName + TEXT(" Ret;") LINE_TERMINATOR;
				Ret += TEXT("\tRet.Value = ") + CoerceFloat(TEXT("Src.Value"), DstIndex, SrcIndex) + TEXT(";") LINE_TERMINATOR;
				Ret += TEXT("\tRet.Ddx = ") + CoerceFloat(TEXT("Src.Ddx"), DstIndex, SrcIndex) + TEXT(";") LINE_TERMINATOR;
				Ret += TEXT("\tRet.Ddy = ") + CoerceFloat(TEXT("Src.Ddy"), DstIndex, SrcIndex) + TEXT(";") LINE_TERMINATOR;
				Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
				Ret += TEXT("}") LINE_TERMINATOR;
				Ret += TEXT("") LINE_TERMINATOR;
			}
		}
	}

	const TCHAR* SwizzleList[4] =
	{
		TEXT("x"),
		TEXT("y"),
		TEXT("z"),
		TEXT("w")
	};

	// Extract single FloatDeriv element from FloatDerivX
	for (int32 StructIndex = 0; StructIndex < 4; StructIndex++)
	{
		for (int32 ElemIndex = 0; ElemIndex <= StructIndex; ElemIndex++)
		{
			if (bExtractIndexEnabled[StructIndex] || IsDebugGenerateAllFunctionsEnabled())
			{
				// i.e. can't grab the 4th component of a float3
				check(ElemIndex <= StructIndex);

				FString BaseName = GetDerivVectorName(StructIndex);

				FString ElemStr = FString::Printf(TEXT("%d"), ElemIndex + 1);

				Ret += TEXT("FloatDeriv Extract") + BaseName + TEXT("_") + ElemStr + TEXT("(") + BaseName + TEXT(" InValue)") LINE_TERMINATOR;
				Ret += TEXT("{") LINE_TERMINATOR;
				Ret += TEXT("\tFloatDeriv Ret;") LINE_TERMINATOR;
				Ret += TEXT("\tRet.Value = InValue.Value.") + FString(SwizzleList[ElemIndex]) + TEXT(";") LINE_TERMINATOR;
				Ret += TEXT("\tRet.Ddx = InValue.Ddx.") + FString(SwizzleList[ElemIndex]) + TEXT(";") LINE_TERMINATOR;
				Ret += TEXT("\tRet.Ddy = InValue.Ddy.") + FString(SwizzleList[ElemIndex]) + TEXT(";") LINE_TERMINATOR;
				Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
				Ret += TEXT("}") LINE_TERMINATOR;
				Ret += TEXT("") LINE_TERMINATOR;
			}
		}
	}

	// Func2s
	for (int32 Op = 0; Op < (int32)EFunc2::Num; Op++)
	{
		for (int32 Index = 0; Index < 4; Index++)
		{
			if (bFunc2OpIsEnabled[Op][Index] || IsDebugGenerateAllFunctionsEnabled())
			{
				FString BaseName = GetDerivVectorName(Index);
				FString FieldName = GetFloatVectorName(Index);
				FString BoolName = GetBoolVectorName(Index);

				switch((EFunc2)Op)
				{
				case EFunc2::Add:
					Ret += BaseName + TEXT(" AddDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = A.Value + B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = A.Ddx + B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = A.Ddy + B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Sub:
					Ret += BaseName + TEXT(" SubDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = A.Value - B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = A.Ddx - B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = A.Ddy - B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Mul:
					Ret += BaseName + TEXT(" MulDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = A.Value * B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = A.Ddx * B.Value + A.Value * B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = A.Ddy * B.Value + A.Value * B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Div:
					Ret += BaseName + TEXT(" DivDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = A.Value / B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" Denom = rcp(B.Value * B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA =  B.Value * Denom;") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdB = -A.Value * Denom;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx + dFdB * B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy + dFdB * B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Fmod:
					// Only valid when B derivatives are zero.
					// We can't really do anything meaningful in the non-zero case.
					Ret += BaseName + TEXT(" FmodDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = fmod(A.Value, B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Min:
					Ret += BaseName + TEXT(" MinDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\t") + BoolName + TEXT(" Cmp = A.Value < B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = Cmp ? A.Value : B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = Cmp ? A.Ddx : B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = Cmp ? A.Ddy : B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Max:
					Ret += BaseName + TEXT(" MaxDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\t") + BoolName + TEXT(" Cmp = A.Value > B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = Cmp ? A.Value : B.Value;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = Cmp ? A.Ddx : B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = Cmp ? A.Ddy : B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Dot:
					Ret += TEXT("FloatDeriv DotDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\tFloatDeriv Ret = ConstructConstantFloatDeriv(0);") LINE_TERMINATOR;
					for (int32 Component = 0; Component <= Index; Component++)
					{
						Ret += FString::Printf(TEXT("\tRet = AddDeriv(Ret,MulDeriv(Extract%s_%d(A),Extract%s_%d(B)));"), *BaseName, Component + 1, *BaseName, Component + 1) + LINE_TERMINATOR;
					}
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Pow:
					// pow(A,B) = exp(B*log(A))
					//     pow'(A,B) = exp(B*log(A)) * (B'*log(A) + (B/A)*A')
					//     pow'(A,B) = pow(A,B) * (B'*log(A) + (B/A)*A')
					// sanity check when B is constant and A is a linear function (B'=0,A'=1)
					//     pow'(A,B) = pow(A,B) * (0*log(A) + (B/A)*1)
					//     pow'(A,B) = B * pow(A,B-1)
					Ret += BaseName + TEXT(" PowDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = pow(A.Value, B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = Ret.Value * (B.Ddx * log(A.Value) + (B.Value/A.Value)*A.Ddx);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = Ret.Value * (B.Ddy * log(A.Value) + (B.Value/A.Value)*A.Ddy);") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::PowPositiveClamped:
					Ret += BaseName + TEXT(" PowPositiveClampedDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\t") + BoolName + TEXT(" InRange = (0.0 < B.Value);") LINE_TERMINATOR; // should we check for A as well?
					Ret += TEXT("\t") + FieldName + TEXT(" Zero = 0.0;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = PositiveClampedPow(A.Value, B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = Ret.Value * (B.Ddx * log(A.Value) + (B.Value/A.Value)*A.Ddx);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = Ret.Value * (B.Ddy * log(A.Value) + (B.Value/A.Value)*A.Ddy);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = InRange ? Ret.Ddx : Zero;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = InRange ? Ret.Ddy : Zero;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Atan2:
					Ret += BaseName + TEXT(" Atan2Deriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = atan2(A.Value, B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" Denom = rcp(A.Value * A.Value + B.Value * B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA =  B.Value * Denom;") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdB = -A.Value * Denom;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx + dFdB * B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy + dFdB * B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Atan2Fast:
					Ret += BaseName + TEXT(" Atan2FastDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = atan2Fast(A.Value, B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" Denom = rcp(A.Value * A.Value + B.Value * B.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA =  B.Value * Denom;") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdB = -A.Value * Denom;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx + dFdB * B.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy + dFdB * B.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc2::Cross:
					if (Index == 2)
					{
						// (A*B)' = A' * B + A * B'
						// Cross(A, B) = A.yzx * B.zxy - A.zxy * B.yzx;
						// Cross(A, B)' = A.yzx' * B.zxy + A.yzx * B.zxy' - A.zxy' * B.yzx - A.zxy * B.yzx';
						Ret += BaseName + TEXT(" CrossDeriv(") + BaseName + TEXT(" A, ") + BaseName + TEXT(" B)") LINE_TERMINATOR;
						Ret += TEXT("{") LINE_TERMINATOR;
						Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
						Ret += TEXT("\tRet.Value = cross(A.Value, B.Value);") LINE_TERMINATOR;
						Ret += TEXT("\tRet.Ddx = A.Ddx.yzx * B.Value.zxy + A.Value.yzx * B.Ddx.zxy - A.Ddx.zxy * B.Value.yzx - A.Value.zxy * B.Ddx.yzx;") LINE_TERMINATOR;
						Ret += TEXT("\tRet.Ddy = A.Ddy.yzx * B.Value.zxy + A.Value.yzx * B.Ddy.zxy - A.Ddy.zxy * B.Value.yzx - A.Value.zxy * B.Ddy.yzx;") LINE_TERMINATOR;
						Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
						Ret += TEXT("}") LINE_TERMINATOR;
						Ret += TEXT("") LINE_TERMINATOR;
					}
					break;
				default:
					check(0);
					break;
				}
			}
		}
	}

	for (int32 Op = 0; Op < (int32)EFunc1::Num; Op++)
	{
		for (int32 Index = 0; Index < 4; Index++)
		{
			if (bFunc1OpIsEnabled[Op][Index] || IsDebugGenerateAllFunctionsEnabled())
			{
				FString BaseName	= GetDerivVectorName(Index);
				FString FieldName	= GetFloatVectorName(Index);
				FString BoolName	= GetBoolVectorName(Index);

				switch((EFunc1)Op)
				{
				case EFunc1::Abs:
					Ret += BaseName + TEXT(" AbsDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = abs(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = (A.Value >= 0.0f ? 1.0f : -1.0f);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Sin:
					Ret += BaseName + TEXT(" SinDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = sin(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = cos(A.Value);");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Cos:
					Ret += BaseName + TEXT(" CosDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = cos(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = -sin(A.Value);");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Tan:
					Ret += BaseName + TEXT(" TanDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = tan(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rcp(cos(A.Value) * cos(A.Value));");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Asin:
					Ret += BaseName + TEXT(" AsinDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = asin(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rsqrt(max(1.0f - A.Value * A.Value, 0.00001f));");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::AsinFast:
					Ret += BaseName + TEXT(" AsinFastDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = asinFast(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rsqrt(max(1.0f - A.Value * A.Value, 0.00001f));");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Acos:
					Ret += BaseName + TEXT(" AcosDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = acos(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = -rsqrt(max(1.0f - A.Value * A.Value, 0.00001f));");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::AcosFast:
					Ret += BaseName + TEXT(" AcosFastDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = acosFast(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = -rsqrt(max(1.0f - A.Value * A.Value, 0.00001f));");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Atan:
					Ret += BaseName + TEXT(" AtanDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = atan(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rcp(A.Value * A.Value + 1.0f);");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::AtanFast:
					Ret += BaseName + TEXT(" AtanFastDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = atanFast(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rcp(A.Value * A.Value + 1.0f);");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Sqrt:
					Ret += BaseName + TEXT(" SqrtDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = sqrt(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = 0.5f * rsqrt(max(A.Value, 0.00001f));");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Rcp:
					Ret += BaseName + TEXT(" RcpDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = rcp(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = -Ret.Value * Ret.Value;");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Rsqrt:
					Ret += BaseName + TEXT(" RsqrtDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = rsqrt(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = -0.5f * rsqrt(A.Value) * rcp(A.Value);");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Saturate:
					Ret += BaseName + TEXT(" SaturateDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BoolName + TEXT(" InRange = (0.0 < A.Value && A.Value < 1.0);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" Zero = 0.0f;") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = saturate(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = InRange ? A.Ddx : Zero;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = InRange ? A.Ddy : Zero;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Frac:
					Ret += BaseName + TEXT(" FracDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = frac(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Log2:
					Ret += BaseName + TEXT(" Log2Deriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = log2(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rcp(A.Value) * 1.442695f;");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx ;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Log10:
					Ret += BaseName + TEXT(" Log10Deriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = log10(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\t") + FieldName + TEXT(" dFdA = rcp(A.Value) * 0.4342945f;");
					Ret += TEXT("\tRet.Ddx = dFdA * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = dFdA * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Exp:
					Ret += BaseName + TEXT(" ExpDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Value = exp(A.Value);") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddx = exp(A.Value) * A.Ddx;") LINE_TERMINATOR;
					Ret += TEXT("\tRet.Ddy = exp(A.Value) * A.Ddy;") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Length:
					Ret += TEXT("FloatDeriv LengthDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\tFloatDeriv Ret = SqrtDeriv(DotDeriv(A,A));") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::InvLength:
					Ret += TEXT("FloatDeriv InvLengthDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\tFloatDeriv Ret = RsqrtDeriv(DotDeriv(A,A));") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				case EFunc1::Normalize:
					Ret += BaseName + TEXT(" NormalizeDeriv(") + BaseName + TEXT(" A)") LINE_TERMINATOR;
					Ret += TEXT("{") LINE_TERMINATOR;
					Ret += TEXT("\tFloatDeriv InvLen = RsqrtDeriv(DotDeriv(A,A));") LINE_TERMINATOR;
					Ret += TEXT("\t") + BaseName + TEXT(" Ret = MulDeriv(") + ConvertDeriv(TEXT("InvLen"), Index, 0) + TEXT(", A);") LINE_TERMINATOR;
					Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
					Ret += TEXT("}") LINE_TERMINATOR;
					Ret += TEXT("") LINE_TERMINATOR;
					break;
				default:
					check(0);
					break;
				}
			}
		}
	}

	for (int32 Index = 0; Index < 4; Index++)
	{
		const FString BaseName = GetDerivVectorName(Index);
		const FString FieldName = GetFloatVectorName(Index);
		const FString BoolName = GetBoolVectorName(Index);

		if (bLerpEnabled[Index] || IsDebugGenerateAllFunctionsEnabled())
		{
			
			// lerp(a,b,s) = a*(1-s) + b*s
			// lerp(a,b,s)' = a' * (1 - s') + b' * s + s' * (b - a)
			Ret += FString::Printf(TEXT("%s LerpDeriv(%s A, %s B, %s S)"), *BaseName, *BaseName, *BaseName, *BaseName) + LINE_TERMINATOR;
			Ret += TEXT("{") LINE_TERMINATOR;
			Ret += TEXT("\t") + BaseName + TEXT(" Ret;") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Value = lerp(A.Value, B.Value, S.Value);") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddx = lerp(A.Ddx, B.Ddx, S.Value) + S.Ddx * (B.Value - A.Value);") LINE_TERMINATOR;
			Ret += TEXT("\tRet.Ddy = lerp(A.Ddy, B.Ddy, S.Value) + S.Ddy * (B.Value - A.Value);") LINE_TERMINATOR;
			Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
			Ret += TEXT("}") LINE_TERMINATOR;
			Ret += TEXT("") LINE_TERMINATOR;
		}

		if (bIfEnabled[Index] || IsDebugGenerateAllFunctionsEnabled())
		{
			Ret += FString::Printf(TEXT("%s IfDeriv(float A, float B, %s Greater, %s Less)"), *BaseName, *BaseName, *BaseName) + LINE_TERMINATOR;
			Ret += TEXT("{") LINE_TERMINATOR;
			Ret += TEXT("\tif(A >= B)") LINE_TERMINATOR;
			Ret += TEXT("\t\treturn Greater;") LINE_TERMINATOR;
			Ret += TEXT("\telse") LINE_TERMINATOR;
			Ret += TEXT("\t\treturn Less;") LINE_TERMINATOR;
			Ret += TEXT("}") LINE_TERMINATOR;
			Ret += TEXT("") LINE_TERMINATOR;
		}

		if (bIf2Enabled[Index] || IsDebugGenerateAllFunctionsEnabled())
		{
			Ret += FString::Printf(TEXT("%s IfDeriv(float A, float B, %s Greater, %s Less, %s Equal, float Threshold)"), *BaseName, *BaseName, *BaseName, *BaseName) + LINE_TERMINATOR;
			Ret += TEXT("{") LINE_TERMINATOR;
			Ret += TEXT("\tif(!(abs(A - B) > Threshold))") LINE_TERMINATOR;	// Written like this to preserve NaN behavior of original code.
			Ret += TEXT("\t\treturn Equal;") LINE_TERMINATOR;
			Ret += TEXT("\tif(A >= B)") LINE_TERMINATOR;
			Ret += TEXT("\t\treturn Greater;") LINE_TERMINATOR;
			Ret += TEXT("\telse") LINE_TERMINATOR;
			Ret += TEXT("\t\treturn Less;") LINE_TERMINATOR;
			Ret += TEXT("}") LINE_TERMINATOR;
			Ret += TEXT("") LINE_TERMINATOR;
		}
	}

	if (bRotateScaleOffsetTexCoords || IsDebugGenerateAllFunctionsEnabled())
	{
		// float2(dot(InTexCoords, InRotationScale.xy), dot(InTexCoords, InRotationScale.zw)) + InOffset;
		// InTexCoords.xy * InRotationScale.xw + InTexCoords.yx * InRotationScale.yz + InOffset;
		Ret += TEXT("FloatDeriv2 RotateScaleOffsetTexCoordsDeriv(FloatDeriv2 TexCoord, FloatDeriv2 RotationScale, FloatDeriv2 Offset)") LINE_TERMINATOR;
		Ret += TEXT("{") LINE_TERMINATOR;
		Ret += TEXT("\tFloatDeriv2 Ret = Offset;") LINE_TERMINATOR;
		Ret += TEXT("\tRet = AddDeriv(Ret, MulDeriv(TexCoord, SwizzleDeriv2(RotationScale, xw));") LINE_TERMINATOR;
		Ret += TEXT("\tRet = AddDeriv(Ret, MulDeriv(SwizzleDeriv2(TexCoord, yx), SwizzleDeriv2(RotationScale, yz));") LINE_TERMINATOR;
		Ret += TEXT("\treturn Ret;") LINE_TERMINATOR;
		Ret += TEXT("}") LINE_TERMINATOR;
		Ret += TEXT("") LINE_TERMINATOR;
	}
	
	if (bUnMirrorEnabled[1][1] || IsDebugGenerateAllFunctionsEnabled())
	{
		// UnMirrorUV
		Ret += TEXT("FloatDeriv2 UnMirrorU(FloatDeriv2 UV, FMaterialPixelParameters Parameters)") LINE_TERMINATOR;
		Ret += TEXT("{") LINE_TERMINATOR;
		Ret += TEXT("\tconst MaterialFloat Scale = (Parameters.UnMirrored * 0.5f);") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Value = UV.Value * Scale + 0.5f;") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Ddx *= Scale;") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Ddy *= Scale;") LINE_TERMINATOR;
		Ret += TEXT("\treturn UV;") LINE_TERMINATOR;
		Ret += TEXT("}") LINE_TERMINATOR;
		Ret += TEXT("") LINE_TERMINATOR;
	}
	
	if(bUnMirrorEnabled[1][0] || IsDebugGenerateAllFunctionsEnabled())
	{
		// UnMirrorU
		Ret += TEXT("FloatDeriv2 UnMirrorU(FloatDeriv2 UV, FMaterialPixelParameters Parameters)") LINE_TERMINATOR;
		Ret += TEXT("{") LINE_TERMINATOR;
		Ret += TEXT("\tconst MaterialFloat Scale = (Parameters.UnMirrored * 0.5f);") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Value.x = UV.Value.x * Scale + 0.5f;") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Ddx.x *= Scale;") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Ddy.x *= Scale;") LINE_TERMINATOR;
		Ret += TEXT("\treturn UV;") LINE_TERMINATOR;
		Ret += TEXT("}") LINE_TERMINATOR;
		Ret += TEXT("") LINE_TERMINATOR;
	}
	
	if (bUnMirrorEnabled[0][1] || IsDebugGenerateAllFunctionsEnabled())
	{
		// UnMirrorV
		Ret += TEXT("FloatDeriv2 UnMirrorV(FloatDeriv2 UV, FMaterialPixelParameters Parameters)") LINE_TERMINATOR;
		Ret += TEXT("{") LINE_TERMINATOR;
		Ret += TEXT("\tconst MaterialFloat Scale = (Parameters.UnMirrored * 0.5f);") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Value.y = UV.Value.y * Scale + 0.5f;") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Ddx.y *= Scale;") LINE_TERMINATOR;
		Ret += TEXT("\tUV.Ddy.y *= Scale;") LINE_TERMINATOR;
		Ret += TEXT("\treturn UV;") LINE_TERMINATOR;
		Ret += TEXT("}") LINE_TERMINATOR;
		Ret += TEXT("") LINE_TERMINATOR;
	}

	return Ret;
}


FString FMaterialDerivativeAutogen::ApplyUnMirror(FString Value, bool bUnMirrorU, bool bUnMirrorV)
{
	if (bUnMirrorU && bUnMirrorV)
	{
		Value = FString::Printf(TEXT("UnMirrorUV(%s, Parameters)"), *Value);
	}
	else if (bUnMirrorU)
	{
		Value = FString::Printf(TEXT("UnMirrorU(%s, Parameters)"), *Value);
	}
	else if (bUnMirrorV)
	{
		Value = FString::Printf(TEXT("UnMirrorV(%s, Parameters)"), *Value);
	}

	bUnMirrorEnabled[bUnMirrorU][bUnMirrorV] = true;
	
	return Value;	
}

#endif // WITH_EDITORONLY_DATA