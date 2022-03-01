// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "RenderUtils.h"
#include "Engine/Texture.h"

namespace UE::HLSLTree
{

struct FPreshaderLoopScope
{
	const FStatement* BreakStatement = nullptr;
	Shader::FPreshaderLabel BreakLabel;
};

FSwizzleParameters::FSwizzleParameters(int8 InR, int8 InG, int8 InB, int8 InA) : NumComponents(0)
{
	ComponentIndex[0] = InR;
	ComponentIndex[1] = InG;
	ComponentIndex[2] = InB;
	ComponentIndex[3] = InA;

	if (InA >= 0)
	{
		check(InA <= 3);
		++NumComponents;
		check(InB >= 0);
	}
	if (InB >= 0)
	{
		check(InB <= 3);
		++NumComponents;
		check(InG >= 0);
	}

	if (InG >= 0)
	{
		check(InG <= 3);
		++NumComponents;
	}

	// At least one proper index
	check(InR >= 0 && InR <= 3);
	++NumComponents;
}

FRequestedType FSwizzleParameters::GetRequestedInputType(const FRequestedType& RequestedType) const
{
	FRequestedType RequestedInputType;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			const int32 SwizzledComponentIndex = ComponentIndex[Index];
			RequestedInputType.SetComponentRequest(SwizzledComponentIndex);
		}
	}
	return RequestedInputType;
}

FSwizzleParameters MakeSwizzleMask(bool bInR, bool bInG, bool bInB, bool bInA)
{
	int8 ComponentIndex[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	int8 CurrentComponent = 0;
	if (bInR)
	{
		ComponentIndex[CurrentComponent++] = 0;
	}
	if (bInG)
	{
		ComponentIndex[CurrentComponent++] = 1;
	}
	if (bInB)
	{
		ComponentIndex[CurrentComponent++] = 2;
	}
	if (bInA)
	{
		ComponentIndex[CurrentComponent++] = 3;
	}
	return FSwizzleParameters(ComponentIndex[0], ComponentIndex[1], ComponentIndex[2], ComponentIndex[3]);
}

bool FExpressionError::PrepareValue(FEmitContext& Context, FEmitScope&, const FRequestedType&, FPrepareValueResult&) const
{
	return Context.Errors->AddError(ErrorMessage);
}

FExpression* FTree::NewConstant(const Shader::FValue& Value)
{
	return NewExpression<FExpressionConstant>(Value);
}

void FExpressionConstant::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const Shader::FType DerivativeType = Value.Type.GetDerivativeType();
	if (!DerivativeType.IsVoid())
	{
		const Shader::FValue ZeroValue(DerivativeType);
		OutResult.ExpressionDdx = Tree.NewConstant(ZeroValue);
		OutResult.ExpressionDdy = OutResult.ExpressionDdx;
	}
}

bool FExpressionConstant::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Constant, Value.Type);
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	Context.PreshaderStackPosition++;
	OutResult.Type = Value.Type;
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
}

namespace Private
{
Shader::EValueType GetTexCoordType(Shader::EValueType TextureType)
{
	switch (TextureType)
	{
	case Shader::EValueType::Texture2D:
	case Shader::EValueType::TextureExternal: return Shader::EValueType::Float2;
	case Shader::EValueType::Texture2DArray:
	case Shader::EValueType::TextureCube:
	case Shader::EValueType::Texture3D: return Shader::EValueType::Float3;
	case Shader::EValueType::TextureCubeArray: return Shader::EValueType::Float4;
	default: checkNoEntry(); return Shader::EValueType::Void;
	}
}
}

bool FExpressionTextureSample::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& TextureType = Context.PrepareExpression(TextureExpression, Scope, ERequestedType::Texture);
	if (!Shader::IsTextureType(TextureType.ValueComponentType))
	{
		return Context.Errors->AddError(TEXT("Expected texture"));
	}

	const FRequestedType RequestedTexCoordType = Private::GetTexCoordType(TextureType.GetType());
	const FPreparedType& TexCoordType = Context.PrepareExpression(TexCoordExpression, Scope, RequestedTexCoordType);
	if (TexCoordType.IsVoid())
	{
		return false;
	}

	const bool bUseAnalyticDerivatives = Context.bUseAnalyticDerivatives && (MipValueMode != TMVM_MipLevel) && TexCoordDerivatives.IsValid();
	if (MipValueMode == TMVM_Derivative || bUseAnalyticDerivatives)
	{
		Context.PrepareExpression(TexCoordDerivatives.ExpressionDdx, Scope, RequestedTexCoordType);
		Context.PrepareExpression(TexCoordDerivatives.ExpressionDdy, Scope, RequestedTexCoordType);
	}
	else if (MipValueMode == TMVM_MipLevel || MipValueMode == TMVM_MipBias)
	{
		Context.PrepareExpression(MipValueExpression, Scope, ERequestedType::Scalar);
	}

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* EmitTexture = TextureExpression->GetValueShader(Context, Scope);
	const Shader::EValueType TextureType = EmitTexture->Type;
	check(Shader::IsTextureType(TextureType));

	bool bVirtualTexture = false;
	const TCHAR* SampleFunctionName = nullptr;
	switch (TextureType)
	{
	case Shader::EValueType::Texture2D:
		SampleFunctionName = TEXT("Texture2DSample");
		break;
	case Shader::EValueType::TextureCube:
		SampleFunctionName = TEXT("TextureCubeSample");
		break;
	case Shader::EValueType::Texture2DArray:
		SampleFunctionName = TEXT("Texture2DArraySample");
		break;
	case Shader::EValueType::Texture3D:
		SampleFunctionName = TEXT("Texture3DSample");
		break;
	case Shader::EValueType::TextureExternal:
		SampleFunctionName = TEXT("TextureExternalSample");
		break;
	/*case MCT_TextureVirtual:
		// TODO
		SampleFunctionName = TEXT("TextureVirtualSample");
		bVirtualTexture = true;
		break;*/
	default:
		checkNoEntry();
		break;
	}

	const bool AutomaticViewMipBias = false; // TODO
	TStringBuilder<256> FormattedSampler;
	switch (SamplerSource)
	{
	case SSM_FromTextureAsset:
		FormattedSampler.Appendf(TEXT("%s.Sampler"), EmitTexture->Reference);
		break;
	case SSM_Wrap_WorldGroupSettings:
		FormattedSampler.Appendf(TEXT("GetMaterialSharedSampler(%s.Sampler,%s)"),
			EmitTexture->Reference,
			AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearWrapedSampler") : TEXT("Material.Wrap_WorldGroupSettings"));
		break;
	case SSM_Clamp_WorldGroupSettings:
		FormattedSampler.Appendf(TEXT("GetMaterialSharedSampler(%s.Sampler,%s)"),
			EmitTexture->Reference,
			AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearClampedSampler") : TEXT("Material.Clamp_WorldGroupSettings"));
		break;
	default:
		checkNoEntry();
		break;
	}

	const Shader::EValueType TexCoordType = Private::GetTexCoordType(TextureType);
	FEmitShaderExpression* TexCoordValue = TexCoordExpression->GetValueShader(Context, Scope, TexCoordType);

	FEmitShaderExpression* TextureResult = nullptr;
	if (MipValueMode == TMVM_MipLevel)
	{
		TextureResult = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%Level(%.Texture, %, %, %)"),
			SampleFunctionName,
			EmitTexture,
			FormattedSampler.ToString(),
			TexCoordValue,
			MipValueExpression->GetValueShader(Context, Scope, Shader::EValueType::Float1));
	}
	else if (MipValueMode == TMVM_Derivative || (Context.bUseAnalyticDerivatives && TexCoordDerivatives.IsValid()))
	{
		TextureResult = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%Grad(%.Texture, %, %, %, %)"),
			SampleFunctionName,
			EmitTexture,
			FormattedSampler.ToString(),
			TexCoordValue,
			TexCoordDerivatives.ExpressionDdx->GetValueShader(Context, Scope, TexCoordType),
			TexCoordDerivatives.ExpressionDdy->GetValueShader(Context, Scope, TexCoordType));
	}
	else if (MipValueMode == TMVM_MipBias)
	{
		TextureResult = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%Bias(%.Texture, %, %, %)"),
			SampleFunctionName,
			EmitTexture,
			FormattedSampler.ToString(),
			TexCoordValue,
			MipValueExpression->GetValueShader(Context, Scope, Shader::EValueType::Float1));
	}
	else
	{
		check(MipValueMode == TMVM_None);
		TextureResult = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%(%.Texture, %, %)"),
			SampleFunctionName,
			EmitTexture,
			FormattedSampler.ToString(),
			TexCoordValue);
	}

	OutResult.Code = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("ApplyMaterialSamplerType(%, %.SamplerType)"), TextureResult, EmitTexture);
}

void FExpressionGetStructField::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives StructDerivatives = Tree.GetAnalyticDerivatives(StructExpression);
	if (StructDerivatives.IsValid())
	{
		const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
		check(DerivativeStructType);
		const Shader::FStructField* DerivativeField = DerivativeStructType->FindFieldByName(Field->Name);
		check(DerivativeField);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionGetStructField::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);
	return Tree.NewExpression<FExpressionGetStructField>(StructType, Field, Tree.GetPreviousFrame(StructExpression, RequestedStructType));
}

bool FExpressionGetStructField::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);

	FPreparedType StructPreparedType = Context.PrepareExpression(StructExpression, Scope, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	return OutResult.SetType(Context, RequestedType, StructPreparedType.GetFieldType(Field));
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);

	FEmitShaderExpression* StructValue = StructExpression->GetValueShader(Context, Scope, RequestedStructType);

	OutResult.Code = Context.EmitInlineExpression(Scope, Field->Type, TEXT("%.%"),
		StructValue,
		Field->Name);
}

void FExpressionGetStructField::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);

	StructExpression->GetValuePreshader(Context, Scope, RequestedStructType, OutResult.Preshader);
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::GetField).Write(Field->Type).Write(Field->ComponentIndex);
	OutResult.Type = Field->Type;
}

void FExpressionSetStructField::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives StructDerivatives = Tree.GetAnalyticDerivatives(StructExpression);
	const FExpressionDerivatives FieldDerivatives = Tree.GetAnalyticDerivatives(FieldExpression);

	if (StructDerivatives.IsValid() && FieldDerivatives.IsValid())
	{
		const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
		check(DerivativeStructType);
		const Shader::FStructField* DerivativeField = DerivativeStructType->FindFieldByName(Field->Name);
		check(DerivativeField);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdx, FieldDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdy, FieldDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionSetStructField::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	FExpression* PrevStructExpression = Tree.GetPreviousFrame(StructExpression, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	FExpression* PrevFieldExpression = Tree.GetPreviousFrame(FieldExpression, RequestedFieldType);

	return Tree.NewExpression<FExpressionSetStructField>(StructType, Field, PrevStructExpression, PrevFieldExpression);
}

bool FExpressionSetStructField::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);

	FPreparedType StructPreparedType = Context.PrepareExpression(StructExpression, Scope, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	FPreparedType FieldPreparedType = Context.PrepareExpression(FieldExpression, Scope, RequestedFieldType);

	FPreparedType ResultType(StructPreparedType);
	if (ResultType.IsVoid())
	{
		ResultType = StructType;
	}
	ResultType.SetField(Field, FieldPreparedType);
	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = Context.GetEvaluation(StructExpression, Scope, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = Context.GetEvaluation(FieldExpression, Scope, RequestedStructType);

	FEmitShaderExpression* StructValue = (StructEvaluation != EExpressionEvaluation::None) ? StructExpression->GetValueShader(Context, Scope, RequestedStructType) : nullptr;
	FEmitShaderExpression* FieldValue = (FieldEvaluation != EExpressionEvaluation::None) ? FieldExpression->GetValueShader(Context, Scope, RequestedFieldType, Field->Type) : nullptr;

	if (StructEvaluation == EExpressionEvaluation::None && FieldEvaluation == EExpressionEvaluation::None)
	{
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("((%)0)"), StructType->Name);
	}
	else if (StructEvaluation == EExpressionEvaluation::None)
	{
		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%((%)0, %)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldValue);
	}
	else if (FieldEvaluation == EExpressionEvaluation::None)
	{
		// Don't need field, can just forward the struct value
		OutResult.Code = StructValue;
	}
	else
	{
		check(StructValue->Type.StructType == StructType);
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%(%, %)"),
			StructType->Name,
			Field->Name,
			StructValue,
			FieldValue);
	}
}

void FExpressionSetStructField::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = Context.GetEvaluation(StructExpression, Scope, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = Context.GetEvaluation(FieldExpression, Scope, RequestedStructType);

	if (StructEvaluation != EExpressionEvaluation::None)
	{
		StructExpression->GetValuePreshader(Context, Scope, RequestedStructType, OutResult.Preshader);
	}
	else
	{
		Context.PreshaderStackPosition++;
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(Shader::FType(StructType));
	}

	if (FieldEvaluation != EExpressionEvaluation::None)
	{
		FieldExpression->GetValuePreshader(Context, Scope, RequestedFieldType, OutResult.Preshader);

		check(Context.PreshaderStackPosition > 0);
		Context.PreshaderStackPosition--;

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::SetField).Write(Field->ComponentIndex).Write(Field->GetNumComponents());
	}
	OutResult.Type = StructType;
}

void FExpressionSelect::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives TrueDerivatives = Tree.GetAnalyticDerivatives(TrueExpression);
	const FExpressionDerivatives FalseDerivatives = Tree.GetAnalyticDerivatives(FalseExpression);
	OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSelect>(ConditionExpression, TrueDerivatives.ExpressionDdx, FalseDerivatives.ExpressionDdx);
	OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSelect>(ConditionExpression, TrueDerivatives.ExpressionDdy, FalseDerivatives.ExpressionDdy);
}

FExpression* FExpressionSelect::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	return Tree.NewExpression<FExpressionSelect>(
		Tree.GetPreviousFrame(ConditionExpression, ERequestedType::Scalar),
		Tree.GetPreviousFrame(TrueExpression, RequestedType),
		Tree.GetPreviousFrame(FalseExpression, RequestedType));
}

bool FExpressionSelect::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& ConditionType = Context.PrepareExpression(ConditionExpression, Scope, ERequestedType::Scalar);
	const EExpressionEvaluation ConditionEvaluation = ConditionType.GetEvaluation(Scope, ERequestedType::Scalar);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		FPreparedType ResultType = Context.PrepareExpression(bCondition ? TrueExpression : FalseExpression, Scope, RequestedType);
		ResultType.MergeEvaluation(EExpressionEvaluation::Shader); // TODO - support preshader
		return OutResult.SetType(Context, RequestedType, ResultType);
	}

	const FPreparedType& LhsType = Context.PrepareExpression(FalseExpression, Scope, RequestedType);
	const FPreparedType& RhsType = Context.PrepareExpression(TrueExpression, Scope, RequestedType);
	
	if (LhsType.ValueComponentType != RhsType.ValueComponentType ||
		LhsType.StructType != RhsType.StructType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	FPreparedType ResultType = MergePreparedTypes(LhsType, RhsType);
	ResultType.MergeEvaluation(ConditionEvaluation);
	ResultType.MergeEvaluation(EExpressionEvaluation::Shader); // TODO - support preshader
	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const EExpressionEvaluation ConditionEvaluation = Context.GetEvaluation(ConditionExpression, Scope, ERequestedType::Scalar);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		FExpression* InputExpression = bCondition ? TrueExpression : FalseExpression;
		OutResult.Code = InputExpression->GetValueShader(Context, Scope, RequestedType);
	}
	else
	{
		const Shader::FType LocalType = Context.GetType(this);
		FEmitShaderExpression* TrueValue = TrueExpression->GetValueShader(Context, Scope, RequestedType, LocalType);
		FEmitShaderExpression* FalseValue = FalseExpression->GetValueShader(Context, Scope, RequestedType, LocalType);

		OutResult.Code = Context.EmitExpression(Scope, LocalType, TEXT("(% ? % : %)"),
			ConditionExpression->GetValueShader(Context, Scope, Shader::EValueType::Bool1),
			TrueValue,
			FalseValue);
	}
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	// TODO
	Context.PreshaderStackPosition++;
	OutResult.Type = Context.GetType(this);
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);
}

void FExpressionDerivative::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// TODO
}

FExpression* FExpressionDerivative::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	return Tree.NewExpression<FExpressionDerivative>(Coord, Tree.GetPreviousFrame(Input, RequestedType));
}

bool FExpressionDerivative::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FPreparedType ResultType = Context.PrepareExpression(Input, Scope, RequestedType);
	if (ResultType.IsVoid())
	{
		return false;
	}

	ResultType.ValueComponentType = Shader::MakeNonLWCType(ResultType.ValueComponentType);

	const EExpressionEvaluation InputEvaluation = ResultType.GetEvaluation(Scope, RequestedType);
	if (InputEvaluation != EExpressionEvaluation::Shader)
	{
		ResultType.SetEvaluation(EExpressionEvaluation::Constant);
	}
	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionDerivative::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* EmitInput = Input->GetValueShader(Context, Scope, RequestedType);
	const bool bIsLWC = Shader::IsLWCType(EmitInput->Type);
	const TCHAR* FunctionName = nullptr;
	switch (Coord)
	{
	case EDerivativeCoordinate::Ddx: FunctionName = bIsLWC ? TEXT("LWCDdx") : TEXT("ddx"); break;
	case EDerivativeCoordinate::Ddy: FunctionName = bIsLWC ? TEXT("LWCDdy") : TEXT("ddy"); break;
	default: checkNoEntry(); break;
	}
	OutResult.Code = Context.EmitExpression(Scope, Shader::MakeNonLWCType(EmitInput->Type), TEXT("%(%)"), FunctionName, EmitInput);
}

void FExpressionDerivative::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	// Derivative of a constant is 0
	Context.PreshaderStackPosition++;
	OutResult.Type = Context.GetType(this);
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);
}

void FExpressionSwizzle::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives InputDerivatives = Tree.GetAnalyticDerivatives(Input);
	if (InputDerivatives.IsValid())
	{
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSwizzle>(Parameters, InputDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSwizzle>(Parameters, InputDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionSwizzle::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	return Tree.NewExpression<FExpressionSwizzle>(Parameters, Tree.GetPreviousFrame(Input, RequestedInputType));
}

bool FExpressionSwizzle::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);

	FPreparedType ResultType;
	if (RequestedInputType.IsVoid())
	{
		// All the requested components are outside the swizzle, so just return 0
		ResultType.ValueComponentType = Shader::EValueComponentType::Float;
		for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
		{
			ResultType.SetComponent(ComponentIndex, EExpressionEvaluation::ConstantZero);
		}
	}
	else
	{
		const FPreparedType& InputType = Context.PrepareExpression(Input, Scope, RequestedInputType);

		ResultType.ValueComponentType = InputType.ValueComponentType;
		for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
		{
			if (RequestedType.IsComponentRequested(ComponentIndex))
			{
				const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
				ResultType.SetComponent(ComponentIndex, InputType.GetComponent(SwizzledComponentIndex));
			}
			else
			{
				ResultType.SetComponent(ComponentIndex, EExpressionEvaluation::ConstantZero);
			}
		}
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	TCHAR LWCSwizzle[10] = TEXT("");
	bool bHasSwizzleReorder = false;

	const int32 NumComponents = FMath::Min(RequestedType.GetNumComponents(), Parameters.NumComponents);
	FRequestedType RequestedInputType;
	for (int32 ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
	{
		// If component wasn't requested, we just refer to 'x' component of input, since that should always be present
		int32 SwizzledComponentIndex = 0;

		if (RequestedType.IsComponentRequested(ComponentIndex))
		{
			SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
			RequestedInputType.SetComponentRequest(SwizzledComponentIndex);
		}

		Swizzle[ComponentIndex] = ComponentName[SwizzledComponentIndex];
		LWCSwizzle[ComponentIndex * 2 + 0] = TEXT(',');
		LWCSwizzle[ComponentIndex * 2 + 1] = TEXT('0') + SwizzledComponentIndex;

		if (SwizzledComponentIndex != ComponentIndex)
		{
			bHasSwizzleReorder = true;
		}
	}

	FEmitShaderExpression* InputValue = Input->GetValueShader(Context, Scope, RequestedInputType);
	const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputValue->Type);

	if (bHasSwizzleReorder || NumComponents != InputTypeDesc.NumComponents)
	{
		const Shader::EValueType ResultType = Shader::MakeValueType(InputTypeDesc.ComponentType, NumComponents);
		const int32 NumRequestedComponents = RequestedInputType.GetNumComponents();
		check(NumRequestedComponents > 0);
	
		if (NumRequestedComponents > InputTypeDesc.NumComponents)
		{
			// Zero-extend our input if needed, so we can acccess all the given components
			InputValue = Context.EmitCast(Scope, InputValue, Shader::MakeValueType(InputTypeDesc.ComponentType, NumRequestedComponents), EEmitCastFlags::ZeroExtendScalar);
		}

		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			check(LWCSwizzle[0]);
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("LWCSwizzle(%%)"),
				InputValue,
				LWCSwizzle);
		}
		else
		{
			check(Swizzle[0]);
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("%.%"),
				InputValue,
				Swizzle);
		}
	}
	else
	{
		OutResult.Code = InputValue;
	}
}

void FExpressionSwizzle::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	if (RequestedInputType.IsVoid())
	{
		Context.PreshaderStackPosition++;
		OutResult.Type = Shader::MakeValueType(Shader::EValueComponentType::Float, Parameters.NumComponents);
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);
	}
	else
	{
		const Shader::FType InputType = Input->GetValuePreshader(Context, Scope, RequestedInputType, OutResult.Preshader);
		const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputType);

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ComponentSwizzle)
			.Write((uint8)Parameters.NumComponents)
			.Write((uint8)Parameters.ComponentIndex[0])
			.Write((uint8)Parameters.ComponentIndex[1])
			.Write((uint8)Parameters.ComponentIndex[2])
			.Write((uint8)Parameters.ComponentIndex[3]);
		OutResult.Type = Shader::MakeValueType(InputTypeDesc.ComponentType, Parameters.NumComponents);
	}
}

void FExpressionAppend::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives LhsDerivatives = Tree.GetAnalyticDerivatives(Lhs);
	const FExpressionDerivatives RhsDerivatives = Tree.GetAnalyticDerivatives(Rhs);
	if (LhsDerivatives.IsValid() && RhsDerivatives.IsValid())
	{
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionAppend>(LhsDerivatives.ExpressionDdx, RhsDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionAppend>(LhsDerivatives.ExpressionDdy, RhsDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionAppend::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	// TODO - requested type?
	return Tree.NewExpression<FExpressionAppend>(Tree.GetPreviousFrame(Lhs, RequestedType), Tree.GetPreviousFrame(Rhs, RequestedType));
}

bool FExpressionAppend::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = Context.PrepareExpression(Lhs, Scope, RequestedType);
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min(LhsType.GetNumComponents(), NumRequestedComponents);

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponent(Index, LhsType.GetComponent(Index));
	}

	FRequestedType RhsRequestedType;
	for (int32 Index = NumLhsComponents; Index < NumRequestedComponents; ++Index)
	{
		RhsRequestedType.SetComponentRequest(Index - NumLhsComponents, RequestedType.IsComponentRequested(Index));
	}

	if (!RhsRequestedType.IsVoid())
	{
		const FPreparedType& RhsType = Context.PrepareExpression(Rhs, Scope, RhsRequestedType);
		if (LhsType.ValueComponentType != RhsType.ValueComponentType)
		{
			return Context.Errors->AddError(TEXT("Type mismatch"));
		}

		const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);
		for (int32 Index = 0; Index < NumRhsComponents; ++Index)
		{
			ResultType.SetComponent(NumLhsComponents + Index, RhsType.GetComponent(Index));
		}
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

namespace Private
{
struct FAppendTypes
{
	Shader::EValueType ResultType;
	Shader::EValueType LhsType;
	Shader::EValueType RhsType;
	FRequestedType LhsRequestedType;
	FRequestedType RhsRequestedType;
	bool bIsLWC;
};
FAppendTypes GetAppendTypes(const FRequestedType& RequestedType, Shader::EValueType LhsType, Shader::EValueType RhsType)
{
	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	const Shader::EValueComponentType ComponentType = Shader::CombineComponentTypes(LhsTypeDesc.ComponentType, RhsTypeDesc.ComponentType);
	const int32 NumComponents = FMath::Min(LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents, 4);

	FAppendTypes Types;
	for (int32 Index = 0; Index < LhsTypeDesc.NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Types.LhsRequestedType.SetComponentRequest(Index);
		}
	}
	for (int32 Index = LhsTypeDesc.NumComponents; Index < NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Types.RhsRequestedType.SetComponentRequest(Index - LhsTypeDesc.NumComponents);
		}
	}

	Types.ResultType = Shader::MakeValueType(ComponentType, NumComponents);
	Types.LhsType = Shader::MakeValueType(ComponentType, LhsTypeDesc.NumComponents);
	Types.RhsType = Shader::MakeValueType(ComponentType, NumComponents - LhsTypeDesc.NumComponents);
	Types.bIsLWC = ComponentType == Shader::EValueComponentType::Double;
	return Types;
}
} // namespace Private

void FExpressionAppend::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Context.GetType(Lhs), Context.GetType(Rhs));
	FEmitShaderExpression* LhsValue = Lhs->GetValueShader(Context, Scope, Types.LhsRequestedType, Types.LhsType);

	if (Types.RhsType == Shader::EValueType::Void)
	{
		OutResult.Code = LhsValue;
	}
	else
	{
		FEmitShaderExpression* RhsValue = Rhs->GetValueShader(Context, Scope, Types.RhsRequestedType, Types.RhsType);
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("MakeLWCVector(%, %)"),
				LhsValue,
				RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("%(%, %)"),
				Shader::GetValueTypeDescription(Types.ResultType).Name,
				LhsValue,
				RhsValue);
		}
	}
}

void FExpressionAppend::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Context.GetType(Lhs), Context.GetType(Rhs));
	Lhs->GetValuePreshader(Context, Scope, Types.LhsRequestedType, OutResult.Preshader);
	if (Types.RhsType != Shader::EValueType::Void)
	{
		Rhs->GetValuePreshader(Context, Scope, Types.RhsRequestedType, OutResult.Preshader);

		check(Context.PreshaderStackPosition > 0);
		Context.PreshaderStackPosition--;

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
	}
	OutResult.Type = Types.ResultType;
}

void FExpressionSwitchBase::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	FExpression* InputDdx[MaxInputs];
	FExpression* InputDdy[MaxInputs];
	for (int32 Index = 0; Index < NumInputs; ++Index)
	{
		const FExpressionDerivatives InputDerivative = Tree.GetAnalyticDerivatives(Input[Index]);
		InputDdx[Index] = InputDerivative.ExpressionDdx;
		InputDdy[Index] = InputDerivative.ExpressionDdy;
	}
	OutResult.ExpressionDdx = NewSwitch(Tree, MakeArrayView(InputDdx, NumInputs));
	OutResult.ExpressionDdy = NewSwitch(Tree, MakeArrayView(InputDdy, NumInputs));
}

FExpression* FExpressionSwitchBase::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	FExpression* InputPrevFrame[MaxInputs];
	for (int32 Index = 0; Index < NumInputs; ++Index)
	{
		InputPrevFrame[Index] = Tree.GetPreviousFrame(Input[Index], RequestedType);
	}
	return NewSwitch(Tree, MakeArrayView(InputPrevFrame, NumInputs));
}

bool FExpressionSwitchBase::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const int32 InputIndex = GetInputIndex(Context);
	check(InputIndex >= 0 && InputIndex < NumInputs);
	const FPreparedType& InputType = Context.PrepareExpression(Input[InputIndex], Scope, RequestedType);
	if (InputType.IsVoid())
	{
		return false;
	}
	return OutResult.SetType(Context, RequestedType, InputType);
}

void FExpressionSwitchBase::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const int32 InputIndex = GetInputIndex(Context);
	check(InputIndex >= 0 && InputIndex < NumInputs);
	OutResult.Code = Input[InputIndex]->GetValueShader(Context, Scope, RequestedType);
}

void FExpressionSwitchBase::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const int32 InputIndex = GetInputIndex(Context);
	check(InputIndex >= 0 && InputIndex < NumInputs);
	OutResult.Type = Input[InputIndex]->GetValuePreshader(Context, Scope, RequestedType, OutResult.Preshader);
}

int32 FExpressionFeatureLevelSwitch::GetInputIndex(const FEmitContext& Context) const
{
	return (int32)Context.TargetParameters.FeatureLevel;
}

int32 FExpressionShadingPathSwitch::GetInputIndex(const FEmitContext& Context) const
{
	const EShaderPlatform ShaderPlatform = Context.TargetParameters.ShaderPlatform;
	ERHIShadingPath::Type ShadingPathToCompile = ERHIShadingPath::Deferred;
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		ShadingPathToCompile = ERHIShadingPath::Forward;
	}
	else if (Context.TargetParameters.FeatureLevel < ERHIFeatureLevel::SM5)
	{
		ShadingPathToCompile = ERHIShadingPath::Mobile;
	}
	return (int32)ShadingPathToCompile;
}

bool FExpressionInlineCustomHLSL::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::FType(ResultType));
}

void FExpressionInlineCustomHLSL::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	OutResult.Code = Context.EmitExpression(Scope, ResultType, Code);
}

bool FExpressionCustomHLSL::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	for (const FCustomHLSLInput& Input : Inputs)
	{
		const FPreparedType& InputType = Context.PrepareExpression(Input.Expression, Scope, ERequestedType::Vector4);
		if (InputType.IsVoid())
		{
			return false;
		}
	}

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::FType(OutputStructType));
}

void FExpressionCustomHLSL::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	OutResult.Code = Context.EmitCustomHLSL(Scope, DeclarationCode, FunctionCode, Inputs, OutputStructType);
}

bool FStatementBreak::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	return true;
}

void FStatementBreak::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	Context.EmitStatement(Scope, TEXT("break;"));
}

void FStatementBreak::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	FPreshaderLoopScope* LoopScope = Context.PreshaderLoopScopes.Last();
	check(!LoopScope->BreakStatement);
	LoopScope->BreakStatement = this;
	LoopScope->BreakLabel = OutPreshader.WriteJump(Shader::EPreshaderOpcode::Jump);
}

bool FStatementReturn::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	return true;
}

void FStatementReturn::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	Context.EmitStatement(Scope, TEXT("return %;"), Expression->GetValueShader(Context, Scope));
}

bool FStatementIf::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	const FPreparedType& ConditionType = Context.PrepareExpression(ConditionExpression, Scope, ERequestedType::Scalar);
	if (ConditionType.IsVoid())
	{
		return false;
	}

	const EExpressionEvaluation ConditionEvaluation = ConditionType.GetEvaluation(Scope, ERequestedType::Scalar);
	check(ConditionEvaluation != EExpressionEvaluation::None);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		if (bCondition)
		{
			Context.MarkScopeEvaluation(Scope, ThenScope, EExpressionEvaluation::Constant);
			Context.MarkScopeDead(Scope, ElseScope);
		}
		else
		{
			Context.MarkScopeDead(Scope, ThenScope);
			Context.MarkScopeEvaluation(Scope, ElseScope, EExpressionEvaluation::Constant);
		}
	}
	else
	{
		Context.MarkScopeEvaluation(Scope, ThenScope, ConditionEvaluation);
		Context.MarkScopeEvaluation(Scope, ElseScope, ConditionEvaluation);
	}

	return true;
}

void FStatementIf::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	FEmitShaderNode* Dependency = nullptr;
	const EExpressionEvaluation ConditionEvaluation = Context.GetEvaluation(ConditionExpression, Scope, ERequestedType::Scalar);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		if (bCondition)
		{
			Dependency = Context.EmitNextScope(Scope, ThenScope);
		}
		else if(!bCondition)
		{
			Dependency = Context.EmitNextScope(Scope, ElseScope);
		}
	}
	else if(ConditionEvaluation != EExpressionEvaluation::None)
	{
		FEmitShaderExpression* ConditionValue = ConditionExpression->GetValueShader(Context, Scope, Shader::EValueType::Bool1);
		Dependency = Context.EmitNestedScopes(Scope, ThenScope, ElseScope, TEXT("if (%)"), TEXT("else"), ConditionValue);
	}

	Context.EmitNextScopeWithDependency(Scope, Dependency, NextScope);
}

void FStatementIf::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	ConditionExpression->GetValuePreshader(Context, Scope, ERequestedType::Scalar, OutPreshader);

	check(Context.PreshaderStackPosition > 0);
	Context.PreshaderStackPosition--;
	const Shader::FPreshaderLabel Label0 = OutPreshader.WriteJump(Shader::EPreshaderOpcode::JumpIfFalse);

	Context.EmitPreshaderScope(ThenScope, RequestedType, Scopes, OutPreshader);

	const Shader::FPreshaderLabel Label1 = OutPreshader.WriteJump(Shader::EPreshaderOpcode::Jump);
	OutPreshader.SetLabel(Label0);
	
	Context.EmitPreshaderScope(ElseScope, RequestedType, Scopes, OutPreshader);

	OutPreshader.SetLabel(Label1);
}

bool FStatementLoop::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	FEmitScope* BreakScope = Context.PrepareScope(&BreakStatement->GetParentScope());
	if (!BreakScope)
	{
		return false;
	}

	Context.MarkScopeEvaluation(Scope, LoopScope, BreakScope->Evaluation);
	return true;
}

void FStatementLoop::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	FEmitShaderNode* Dependency = Context.EmitNestedScope(Scope, LoopScope, TEXT("while (true)"));
	Context.EmitNextScopeWithDependency(Scope, Dependency, NextScope);
}

void FStatementLoop::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	FPreshaderLoopScope PreshaderLoopScope;
	Context.PreshaderLoopScopes.Add(&PreshaderLoopScope);

	const Shader::FPreshaderLabel Label = OutPreshader.GetLabel();
	Context.EmitPreshaderScope(LoopScope, RequestedType, Scopes, OutPreshader);
	OutPreshader.WriteJump(Shader::EPreshaderOpcode::Jump, Label);

	verify(Context.PreshaderLoopScopes.Pop() == &PreshaderLoopScope);
	check(PreshaderLoopScope.BreakStatement == BreakStatement);
	OutPreshader.SetLabel(PreshaderLoopScope.BreakLabel);
}

} // namespace UE::HLSLTree
