// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

namespace UE
{
namespace HLSLTree
{

FBinaryOpDescription::FBinaryOpDescription()
	: Name(nullptr), Operator(nullptr), PreshaderOpcode(Shader::EPreshaderOpcode::Nop)
{}

FBinaryOpDescription::FBinaryOpDescription(const TCHAR* InName, const TCHAR* InOperator, Shader::EPreshaderOpcode InOpcode)
	: Name(InName), Operator(InOperator), PreshaderOpcode(InOpcode)
{}

FBinaryOpDescription GetBinaryOpDesription(EBinaryOp Op)
{
	switch (Op)
	{
	case EBinaryOp::None: return FBinaryOpDescription(TEXT("None"), TEXT(""), Shader::EPreshaderOpcode::Nop); break;
	case EBinaryOp::Add: return FBinaryOpDescription(TEXT("Add"), TEXT("+"), Shader::EPreshaderOpcode::Add); break;
	case EBinaryOp::Sub: return FBinaryOpDescription(TEXT("Subtract"), TEXT("-"), Shader::EPreshaderOpcode::Sub); break;
	case EBinaryOp::Mul: return FBinaryOpDescription(TEXT("Multiply"), TEXT("*"), Shader::EPreshaderOpcode::Mul); break;
	case EBinaryOp::Div: return FBinaryOpDescription(TEXT("Divide"), TEXT("/"), Shader::EPreshaderOpcode::Div); break;
	case EBinaryOp::Less: return FBinaryOpDescription(TEXT("Less"), TEXT("<"), Shader::EPreshaderOpcode::Nop); break;
	default: checkNoEntry(); return FBinaryOpDescription();
	}
}

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
	RequestedInputType.ValueComponentType = RequestedType.ValueComponentType;

	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			const int32 SwizzledComponentIndex = ComponentIndex[Index];
			RequestedInputType.SetComponentRequested(SwizzledComponentIndex);
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

void FExpressionConstant::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	OutResult.SetType(Context, RequestedType, EExpressionEvaluationType::Constant, Value.Type);
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
}

void FExpressionMaterialParameter::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const EExpressionEvaluationType EvaluationType = IsStaticMaterialParameter(ParameterType) ? EExpressionEvaluationType::Constant : EExpressionEvaluationType::Preshader;
	OutResult.SetType(Context, RequestedType, EvaluationType, GetShaderValueType(ParameterType));
}

void FExpressionMaterialParameter::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	if (ParameterType == EMaterialParameterType::StaticSwitch)
	{
		const FMaterialParameterInfo ParameterInfo(ParameterName);
		Shader::FValue Value = DefaultValue;
		for (const FStaticSwitchParameter& Parameter : Context.StaticParameters->StaticSwitchParameters)
		{
			if (Parameter.ParameterInfo == ParameterInfo)
			{
				Value = Parameter.Value;
				break;
			}
		}
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
	}
	else
	{
		const uint32* PrevDefaultOffset = Context.DefaultUniformValues.Find(DefaultValue);
		uint32 DefaultOffset;
		if (PrevDefaultOffset)
		{
			DefaultOffset = *PrevDefaultOffset;
		}
		else
		{
			DefaultOffset = Context.MaterialCompilationOutput->UniformExpressionSet.AddDefaultParameterValue(DefaultValue);
			Context.DefaultUniformValues.Add(DefaultValue, DefaultOffset);
		}
		const int32 ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddNumericParameter(ParameterType, ParameterName, DefaultOffset);
		check(ParameterIndex >= 0 && ParameterIndex <= 0xffff);
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Parameter).Write((uint16)ParameterIndex);
	}
}

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	const int32 TypeIndex = (int32)InputType;
	const int32 TexCoordIndex = TypeIndex - (int32)EExternalInputType::TexCoord0;

	OutShader.Type = GetInputExpressionType(InputType);
	if (TexCoordIndex >= 0 && TexCoordIndex < 8)
	{
		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		OutShader.bInline = true;
		OutShader.Code.Appendf(TEXT("Parameters.TexCoords[%u].xy"), TexCoordIndex);
	}
	else
	{
		return Context.Errors.AddError(this, TEXT("Invalid texcoord"));
	}
}

void FExpressionTextureSample::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	PrepareExpressionValue(Context, TexCoordExpression, 2);
	OutResult.SetType(Context, RequestedType, EExpressionEvaluationType::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	const FTextureDescription& Desc = Declaration->Description;
	const EMaterialValueType MaterialValueType = Desc.Texture->GetMaterialType();
	EMaterialTextureParameterType TextureType = EMaterialTextureParameterType::Count;
	bool bVirtualTexture = false;
	const TCHAR* TextureTypeName = nullptr;
	const TCHAR* SampleFunctionName = nullptr;
	switch (MaterialValueType)
	{
	case MCT_Texture2D:
		TextureType = EMaterialTextureParameterType::Standard2D;
		TextureTypeName = TEXT("Texture2D");
		SampleFunctionName = TEXT("Texture2DSample");
		break;
	case MCT_TextureCube:
		TextureType = EMaterialTextureParameterType::Cube;
		TextureTypeName = TEXT("TextureCube");
		SampleFunctionName = TEXT("TextureCubeSample");
		break;
	case MCT_Texture2DArray:
		TextureType = EMaterialTextureParameterType::Array2D;
		TextureTypeName = TEXT("Texture2DArray");
		SampleFunctionName = TEXT("Texture2DArraySample");
		break;
	case MCT_VolumeTexture:
		TextureType = EMaterialTextureParameterType::Volume;
		TextureTypeName = TEXT("VolumeTexture");
		SampleFunctionName = TEXT("Texture3DSample");
		break;
	case MCT_TextureExternal:
		// TODO
		TextureTypeName = TEXT("ExternalTexture");
		SampleFunctionName = TEXT("TextureExternalSample");
		break;
	case MCT_TextureVirtual:
		// TODO
		TextureType = EMaterialTextureParameterType::Virtual;
		SampleFunctionName = TEXT("TextureVirtualSample");
		bVirtualTexture = true;
		break;
	default:
		checkNoEntry();
		break;
	}

	const TCHAR* SamplerTypeFunction = TEXT("");
	switch (Desc.SamplerType)
	{
	case SAMPLERTYPE_External:
		SamplerTypeFunction = TEXT("ProcessMaterialExternalTextureLookup");
		break;
	case SAMPLERTYPE_Color:
		SamplerTypeFunction = TEXT("ProcessMaterialColorTextureLookup");
		break;
	case SAMPLERTYPE_VirtualColor:
		// has a mobile specific workaround
		SamplerTypeFunction = TEXT("ProcessMaterialVirtualColorTextureLookup");
		break;
	case SAMPLERTYPE_LinearColor:
	case SAMPLERTYPE_VirtualLinearColor:
		SamplerTypeFunction = TEXT("ProcessMaterialLinearColorTextureLookup");
		break;
	case SAMPLERTYPE_Alpha:
	case SAMPLERTYPE_VirtualAlpha:
	case SAMPLERTYPE_DistanceFieldFont:
		SamplerTypeFunction = TEXT("ProcessMaterialAlphaTextureLookup");
		break;
	case SAMPLERTYPE_Grayscale:
	case SAMPLERTYPE_VirtualGrayscale:
		SamplerTypeFunction = TEXT("ProcessMaterialGreyscaleTextureLookup");
		break;
	case SAMPLERTYPE_LinearGrayscale:
	case SAMPLERTYPE_VirtualLinearGrayscale:
		SamplerTypeFunction = TEXT("ProcessMaterialLinearGreyscaleTextureLookup");
		break;
	case SAMPLERTYPE_Normal:
	case SAMPLERTYPE_VirtualNormal:
		// Normal maps need to be unpacked in the pixel shader.
		SamplerTypeFunction = TEXT("UnpackNormalMap");
		break;
	case SAMPLERTYPE_Masks:
	case SAMPLERTYPE_VirtualMasks:
	case SAMPLERTYPE_Data:
		SamplerTypeFunction = TEXT("");
		break;
	default:
		check(0);
		break;
	}

	FMaterialTextureParameterInfo TextureParameterInfo;
	TextureParameterInfo.ParameterInfo = Declaration->Name;
	TextureParameterInfo.TextureIndex = Context.Material->GetReferencedTextures().Find(Desc.Texture);
	TextureParameterInfo.SamplerSource = SamplerSource;
	check(TextureParameterInfo.TextureIndex != INDEX_NONE);

	const int32 ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddTextureParameter(TextureType, TextureParameterInfo);
	const FString TextureName = FString::Printf(TEXT("Material.%s_%u"), TextureTypeName, ParameterIndex);

	FString SamplerStateCode;
	bool AutomaticViewMipBias = false; // TODO
	bool RequiresManualViewMipBias = AutomaticViewMipBias;

	if (!bVirtualTexture) //VT does not have explict samplers (and always requires manual view mip bias)
	{
		if (SamplerSource == SSM_FromTextureAsset)
		{
			SamplerStateCode = FString::Printf(TEXT("%sSampler"), *TextureName);
		}
		else if (SamplerSource == SSM_Wrap_WorldGroupSettings)
		{
			// Use the shared sampler to save sampler slots
			const TCHAR* SharedSamplerName = AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearWrapedSampler") : TEXT("Material.Wrap_WorldGroupSettings");
			SamplerStateCode = FString::Printf(TEXT("GetMaterialSharedSampler(%sSampler,%s)"), *TextureName, SharedSamplerName);
			RequiresManualViewMipBias = false;
		}
		else if (SamplerSource == SSM_Clamp_WorldGroupSettings)
		{
			// Use the shared sampler to save sampler slots
			const TCHAR* SharedSamplerName = AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearClampedSampler") : TEXT("Material.Clamp_WorldGroupSettings");
			SamplerStateCode = FString::Printf(TEXT("GetMaterialSharedSampler(%sSampler,%s)"), *TextureName, SharedSamplerName);
			RequiresManualViewMipBias = false;
		}
	}

	OutShader.Type = Shader::EValueType::Float4;
	OutShader.Code.Appendf(TEXT("%s(%s(%s, %s, %s))"),
		SamplerTypeFunction,
		SampleFunctionName,
		*TextureName,
		*SamplerStateCode,
		TexCoordExpression->GetValueShader(Context, Shader::EValueType::Float2));
}

void FExpressionGetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	const FPreparedType& StructPreparedType = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	OutResult.SetType(Context, RequestedType, StructPreparedType.GetFieldType(Field));
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	OutShader.bInline = true;
	OutShader.Type = Field->Type;
	OutShader.Code.Appendf(TEXT("%s.%s"),
		StructExpression->GetValueShader(Context, RequestedStructType),
		Field->Name);
}

void FExpressionGetStructField::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	StructExpression->GetValuePreshader(Context, RequestedStructType, OutPreshader);
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::GetField).Write(Field->Type).Write(Field->ComponentIndex);
}

void FExpressionSetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);

	const FPreparedType& StructPreparedType = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	const FPreparedType& FieldPreparedType = PrepareExpressionValue(Context, FieldExpression, RequestedType.GetField(Field));
	if (FieldPreparedType.IsVoid())
	{
		OutResult.SetForwardValue(Context, RequestedType, StructExpression);
	}
	else
	{
		FPreparedType ResultType(StructPreparedType);
		if (ResultType.IsVoid())
		{
			ResultType = StructType;
		}
		ResultType.SetField(Field, FieldPreparedType);
		OutResult.SetType(Context, RequestedType, ResultType);
	}
}

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluationType StructEvaluationType = StructExpression->GetEvaluationType(RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluationType FieldEvaluationType = FieldExpression->GetEvaluationType(RequestedFieldType);

	OutShader.Type = StructType;
	if (StructEvaluationType == EExpressionEvaluationType::None)
	{
		check(FieldEvaluationType != EExpressionEvaluationType::None);

		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutShader.Code.Appendf(TEXT("%s_Set%s((%s)0, %s)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldExpression->GetValueShader(Context, RequestedFieldType));
	}
	else
	{
		OutShader.Code.Appendf(TEXT("%s_Set%s(%s, %s)"),
			StructType->Name,
			Field->Name,
			StructExpression->GetValueShader(Context, RequestedStructType),
			FieldExpression->GetValueShader(Context, RequestedFieldType));
	}
}

void FExpressionSetStructField::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluationType StructEvaluationType = StructExpression->GetEvaluationType(RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluationType FieldEvaluationType = FieldExpression->GetEvaluationType(RequestedFieldType);

	if (StructEvaluationType != EExpressionEvaluationType::None)
	{
		StructExpression->GetValuePreshader(Context, RequestedStructType, OutPreshader);
	}
	else
	{
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(Shader::FType(StructType));
	}

	FieldExpression->GetValuePreshader(Context, RequestedFieldType, OutPreshader);
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::SetField).Write(Field->ComponentIndex).Write(Field->GetNumComponents());
}

void FExpressionSelect::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& ConditionType = PrepareExpressionValue(Context, ConditionExpression, 1);
	if (ConditionType.GetEvaluationType(Shader::EValueType::Bool1) == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Shader::EValueType::Bool1).AsBoolScalar();
		return OutResult.SetForwardValue(Context, RequestedType, bCondition ? TrueExpression : FalseExpression);
	}

	const FPreparedType& LhsType = PrepareExpressionValue(Context, FalseExpression, RequestedType);
	const FPreparedType& RhsType = PrepareExpressionValue(Context, TrueExpression, RequestedType);
	
	if (LhsType.ValueComponentType != RhsType.ValueComponentType ||
		LhsType.StructType != RhsType.StructType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	OutResult.SetType(Context, RequestedType, MergePreparedTypes(LhsType, RhsType));
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	OutShader.Type = RequestedType.GetType();
	OutShader.Code.Appendf(TEXT("(%s ? %s : %s)"),
		ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1),
		TrueExpression->GetValueShader(Context, RequestedType),
		FalseExpression->GetValueShader(Context, RequestedType));
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	check(false); // TODO
}

void FExpressionBinaryOp::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = PrepareExpressionValue(Context, Lhs, RequestedType);
	const FPreparedType& RhsType = PrepareExpressionValue(Context, Rhs, RequestedType);
	if (LhsType.IsVoid() || RhsType.IsVoid())
	{
		return;
	}

	if (!LhsType.IsNumeric() || !RhsType.IsNumeric())
	{
		return Context.Errors.AddError(this, TEXT("Invalid arithmetic between non-numeric types"));
	}

	FPreparedType ResultType = MergePreparedTypes(LhsType, RhsType);
	if (Op == EBinaryOp::Less)
	{
		ResultType.ValueComponentType = Shader::EValueComponentType::Bool;
	}

	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	if (OpDesc.PreshaderOpcode == Shader::EPreshaderOpcode::Nop)
	{
		// No preshader support
		ResultType.SetEvaluationType(EExpressionEvaluationType::Shader);
	}

	OutResult.SetType(Context, RequestedType, ResultType);
}

namespace Private
{
struct FBinaryOpTypes
{
	Shader::FType ResultType;
	FRequestedType LhsType;
	FRequestedType RhsType;
};
FBinaryOpTypes GetBinaryOpTypes(const FRequestedType& RequestedType, EBinaryOp Op, const FRequestedType& LhsType, const FRequestedType& RhsType)
{
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const Shader::EValueComponentType InputComponentType = Shader::CombineComponentTypes(LhsType.ValueComponentType, RhsType.ValueComponentType);
	FBinaryOpTypes Result;
	switch (Op)
	{
	case EBinaryOp::Less:
		Result.LhsType = Shader::MakeValueType(InputComponentType, NumRequestedComponents);
		Result.RhsType = Shader::MakeValueType(InputComponentType, NumRequestedComponents);
		Result.ResultType = Shader::MakeValueType(Shader::EValueComponentType::Bool, NumRequestedComponents);
		break;
	default:
		Result.LhsType = RequestedType;
		Result.RhsType = RequestedType;
		Result.ResultType = RequestedType.GetType();
		break;
	}
	return Result;
}
} // namespace Private

void FExpressionBinaryOp::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	const Private::FBinaryOpTypes Types = Private::GetBinaryOpTypes(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const TCHAR* LhsCode = Lhs->GetValueShader(Context, Types.LhsType);
	const TCHAR* RhsCode = Rhs->GetValueShader(Context, Types.RhsType);

	OutShader.Type = Types.ResultType;
	switch (Op)
	{
	case EBinaryOp::Add: OutShader.Code.Appendf(TEXT("(%s + %s)"), LhsCode, RhsCode); break;
	case EBinaryOp::Sub: OutShader.Code.Appendf(TEXT("(%s - %s)"), LhsCode, RhsCode); break;
	case EBinaryOp::Mul: OutShader.Code.Appendf(TEXT("(%s * %s)"), LhsCode, RhsCode); break;
	case EBinaryOp::Div: OutShader.Code.Appendf(TEXT("(%s / %s)"), LhsCode, RhsCode); break;
	case EBinaryOp::Less: OutShader.Code.Appendf(TEXT("(%s < %s)"), LhsCode, RhsCode); break;
	default: checkNoEntry(); break;
	}
}

void FExpressionBinaryOp::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const Private::FBinaryOpTypes Types = Private::GetBinaryOpTypes(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Lhs->GetValuePreshader(Context, Types.LhsType, OutPreshader);
	Rhs->GetValuePreshader(Context, Types.RhsType, OutPreshader);
	OutPreshader.WriteOpcode(OpDesc.PreshaderOpcode);
}

void FExpressionSwizzle::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	const FPreparedType& InputType = PrepareExpressionValue(Context, Input, RequestedInputType);

	FPreparedType ResultType(InputType.ValueComponentType);
	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
		ResultType.SetComponentEvaluationType(ComponentIndex, InputType.GetComponentEvaluationType(SwizzledComponentIndex));
	}

	OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);

	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	bool bHasSwizzleReorder = false;

	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
		Swizzle[ComponentIndex] = ComponentName[SwizzledComponentIndex];
		if (SwizzledComponentIndex != ComponentIndex)
		{
			bHasSwizzleReorder = true;
		}
	}

	OutShader.Type = RequestedType.GetType();
	OutShader.bInline = true;
	if (bHasSwizzleReorder)
	{
		OutShader.Code.Appendf(TEXT("%s.%s"),
			Input->GetValueShader(Context, RequestedInputType),
			Swizzle);
	}
	else
	{
		OutShader.Code.Appendf(TEXT("%s"), Input->GetValueShader(Context, RequestedInputType));
	}
}

void FExpressionSwizzle::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);

	Input->GetValuePreshader(Context, RequestedInputType, OutPreshader);
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ComponentSwizzle)
		.Write((uint8)Parameters.NumComponents)
		.Write((uint8)Parameters.ComponentIndex[0])
		.Write((uint8)Parameters.ComponentIndex[1])
		.Write((uint8)Parameters.ComponentIndex[2])
		.Write((uint8)Parameters.ComponentIndex[3]);
}

void FExpressionAppend::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	// TODO - better handling of requested types for inputs
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const FPreparedType& LhsType = PrepareExpressionValue(Context, Lhs, NumRequestedComponents);
	const int32 NumLhsComponents = FMath::Min(LhsType.GetNumComponents(), NumRequestedComponents);

	const FPreparedType& RhsType = PrepareExpressionValue(Context, Rhs, NumRequestedComponents - NumLhsComponents);
	const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);

	if (LhsType.ValueComponentType != RhsType.ValueComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponentEvaluationType(Index, LhsType.GetComponentEvaluationType(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		ResultType.SetComponentEvaluationType(NumLhsComponents + Index, LhsType.GetComponentEvaluationType(Index));
	}

	OutResult.SetType(Context, RequestedType, ResultType);
}

namespace Private
{
struct FAppendTypes
{
	Shader::FType ResultType;
	FRequestedType LhsType;
	FRequestedType RhsType;
};
FAppendTypes GetAppendTypes(const FRequestedType& RequestedType, const FRequestedType& LhsType, const FRequestedType& RhsType)
{
	const int32 NumResultComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min<int32>(NumResultComponents, LhsType.GetNumComponents());
	const int32 NumRhsComponents = FMath::Min<int32>(NumResultComponents - NumLhsComponents, RhsType.GetNumComponents());

	FAppendTypes Types;
	Types.ResultType = Shader::MakeValueType(RequestedType.ValueComponentType, NumLhsComponents + NumRhsComponents);
	Types.LhsType.ValueComponentType = RequestedType.ValueComponentType;
	Types.RhsType.ValueComponentType = RequestedType.ValueComponentType;
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		Types.LhsType.SetComponentRequested(Index, RequestedType.IsComponentRequested(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		Types.RhsType.SetComponentRequested(Index, RequestedType.IsComponentRequested(NumLhsComponents + Index));
	}
	return Types;
}
} // namespace Private

void FExpressionAppend::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetRequestedType(), Rhs->GetRequestedType());

	OutShader.bInline = true;
	OutShader.Type = Types.ResultType;
	if (Types.RhsType.IsVoid())
	{
		OutShader.Code.Appendf(TEXT("%s"), Lhs->GetValueShader(Context, Types.LhsType));
	}
	else
	{
		OutShader.Code.Appendf(TEXT("%s(%s, %s)"),
			RequestedType.GetName(),
			Lhs->GetValueShader(Context, Types.LhsType),
			Rhs->GetValueShader(Context, Types.RhsType));
	}
}

void FExpressionAppend::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	Lhs->GetValuePreshader(Context, Types.LhsType, OutPreshader);
	if (Types.RhsType.IsVoid())
	{
		Rhs->GetValuePreshader(Context, Types.RhsType, OutPreshader);
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
	}
}

void FExpressionReflectionVector::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.bReadMaterialNormal = true;
	OutResult.SetType(Context, RequestedType, EExpressionEvaluationType::Shader, Shader::EValueType::Float3);
}

void FExpressionReflectionVector::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	OutShader.bInline = true;
	OutShader.Type = Shader::EValueType::Float3;
	OutShader.Code.Append(TEXT("Parameters.ReflectionVector"));
}

void FStatementBreak::Prepare(FEmitContext& Context) const
{
}

void FStatementBreak::EmitShader(FEmitContext& Context) const
{
	ParentScope->EmitStatementf(Context, TEXT("break;"));
}

void FStatementReturn::Prepare(FEmitContext& Context) const
{
}

void FStatementReturn::EmitShader(FEmitContext& Context) const
{
	ParentScope->EmitStatementf(Context, TEXT("return %s;"), Expression->GetValueShader(Context));
}

void FStatementIf::Prepare(FEmitContext& Context) const
{
	const FPreparedType& ConditionType = PrepareExpressionValue(Context, ConditionExpression, 1);
	if (ConditionType.GetEvaluationType(Shader::EValueType::Bool1) == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Shader::EValueType::Bool1).AsBoolScalar();
		if (bCondition)
		{
			MarkScopeLive(ThenScope);
			MarkScopeDead(ElseScope);
		}
		else
		{
			MarkScopeDead(ThenScope);
			MarkScopeLive(ElseScope);
		}
	}
	else if(!ConditionType.IsVoid())
	{
		MarkScopeLive(ThenScope);
		MarkScopeLive(ElseScope);
	}
}

void FStatementIf::EmitShader(FEmitContext& Context) const
{
	if (ConditionExpression->GetEvaluationType(Shader::EValueType::Bool1) == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Shader::EValueType::Bool1).AsBoolScalar();
		if (bCondition && IsScopeLive(ThenScope))
		{
			ParentScope->EmitScope(Context, ThenScope);
		}
		else if(!bCondition && IsScopeLive(ElseScope))
		{
			ParentScope->EmitScope(Context, ElseScope);
		}
	}
	else
	{
		if (IsScopeLive(ThenScope) || IsScopeLive(ElseScope))
		{
			ParentScope->EmitNestedScopef(Context, ThenScope, TEXT("if (%s)"),
				ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1));
			if (IsScopeLive(ElseScope))
			{
				ParentScope->EmitNestedScopef(Context, ElseScope, TEXT("else"));
			}
		}
	}

	ParentScope->EmitScope(Context, NextScope);
}

void FStatementLoop::Prepare(FEmitContext& Context) const
{
	MarkScopeLive(LoopScope);
}

void FStatementLoop::EmitShader(FEmitContext& Context) const
{
	if (IsScopeLive(LoopScope))
	{
		ParentScope->EmitNestedScopef(Context, LoopScope, TEXT("while (true)"));
	}
	ParentScope->EmitScope(Context, NextScope);
}

} // namespace HLSLTree
} // namespace UE
