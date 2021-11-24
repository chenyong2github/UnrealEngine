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

void FExpressionConstant::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	if (Value.Type.IsStruct())
	{
		if (Value.Type.StructType != RequestedType.GetStructType())
		{
			return Context.Errors.AddError(this, TEXT("Type mismatch"));
		}

		// TODO - Support for preshader structs?
		return SetType(Context, EExpressionEvaluationType::Shader, Value.Type);
	}
	else
	{
		return SetType(Context, EExpressionEvaluationType::Constant, Shader::MakeValueTypeWithRequestedNumComponents(Value.Type, RequestedType.GetRequestedNumComponents()));
	}
}

void FExpressionConstant::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const TArrayView<const Shader::EValueComponentType> ComponentTypes = Value.Type.StructType->ComponentTypes;
	OutShader.Code.Append(TEXT("{ "));
	for (int32 ComponentIndex = 0; ComponentIndex < Value.Component.Num(); ++ComponentIndex)
	{
		if (ComponentIndex > 0)
		{
			OutShader.Code.Append(TEXT(", "));
		}
		Value.Component[ComponentIndex].ToString(ComponentTypes[ComponentIndex], OutShader.Code);
	}
	OutShader.Code.Append(TEXT(" }"));
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
{
	Shader::FValue BaseValue(Value.Type.ValueType);
	for (int32 ComponentIndex = 0; ComponentIndex < Value.Component.Num(); ++ComponentIndex)
	{
		BaseValue.Component[ComponentIndex] = Value.Component[ComponentIndex];
	}
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(BaseValue);
}

void FExpressionMaterialParameter::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	const Shader::EValueType CurrentType = Shader::MakeValueTypeWithRequestedNumComponents(GetShaderValueType(ParameterType), RequestedType.GetRequestedNumComponents());
	const EExpressionEvaluationType EvaluationType = IsStaticMaterialParameter(ParameterType) ? EExpressionEvaluationType::Constant : EExpressionEvaluationType::Preshader;
	return SetType(Context, EvaluationType, CurrentType);
}

void FExpressionMaterialParameter::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
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

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const int32 TypeIndex = (int32)InputType;
	const int32 TexCoordIndex = TypeIndex - (int32)EExternalInputType::TexCoord0;

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

void FExpressionTextureSample::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	PrepareExpressionValue(Context, TexCoordExpression, 2);
	return SetType(Context, EExpressionEvaluationType::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
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

	OutShader.Code.Appendf(TEXT("%s(%s(%s, %s, %s))"),
		SamplerTypeFunction,
		SampleFunctionName,
		*TextureName,
		*SamplerStateCode,
		TexCoordExpression->GetValueShader(Context, Shader::EValueType::Float2));
}

void FExpressionGetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
	if (!FieldRef)
	{
		return Context.Errors.AddErrorf(this, TEXT("Invalid field %s"), FieldName);
	}

	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(FieldRef, RequestedType);

	const FPrepareValueResult StructResult = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (StructResult && StructResult.Type != FType(StructType))
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	// TODO - preshader structs
	return SetType(Context, EExpressionEvaluationType::Shader, FieldRef.Type);
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	OutShader.bInline = true;
	OutShader.Code.Appendf(TEXT("%s.%s"),
		StructExpression->GetValueShader(Context, StructType),
		FieldName);
}

void FExpressionSetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
	if (!FieldRef)
	{
		return Context.Errors.AddErrorf(this, TEXT("Invalid field %s"), FieldName);
	}

	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(FieldRef);

	const FPrepareValueResult StructResult = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (StructResult && StructResult.Type != FType(StructType))
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	const FPrepareValueResult FieldResult = PrepareExpressionValue(Context, FieldExpression, RequestedType.GetField(FieldRef));
	if (!FieldResult)
	{
		return SetForwardValue(Context, StructExpression, RequestedType);
	}

	// TODO - preshader structs
	return SetType(Context, EExpressionEvaluationType::Shader, StructType);
}

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
	check(FieldRef);
	if (StructExpression->GetEvaluationType() != EExpressionEvaluationType::None)
	{
		OutShader.Code.Appendf(TEXT("%s_Set%s(%s, %s)"),
			StructType->Name,
			FieldName,
			StructExpression->GetValueShader(Context, StructType),
			FieldExpression->GetValueShader(Context, FieldRef.Type));
	}
	else
	{
		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutShader.Code.Appendf(TEXT("%s_Set%s((%s)0, %s)"),
			StructType->Name,
			FieldName,
			StructType->Name,
			FieldExpression->GetValueShader(Context, FieldRef.Type));
	}
}

void FExpressionSelect::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	const FPrepareValueResult ConditionResult = PrepareExpressionValue(Context, ConditionExpression, 1);
	if (ConditionResult.EvaluationType == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionResult.ConstantValue.AsBoolScalar();
		return SetForwardValue(Context, bCondition ? TrueExpression : FalseExpression, RequestedType);
	}

	const FPrepareValueResult LhsResult = PrepareExpressionValue(Context, FalseExpression, RequestedType);
	const FPrepareValueResult RhsResult = PrepareExpressionValue(Context, TrueExpression, RequestedType);
	if (!ConditionResult || !LhsResult || !RhsResult)
	{
		return;
	}

	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsResult.Type);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsResult.Type);
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	if (LhsResult.Type.IsStruct())
	{
		return SetType(Context, EExpressionEvaluationType::Shader, LhsResult.Type);
	}
	else
	{
		const int8 NumComponents = FMath::Max(LhsTypeDesc.NumComponents, RhsTypeDesc.NumComponents);
		return SetType(Context, EExpressionEvaluationType::Shader, Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents));
	}
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	OutShader.Code.Appendf(TEXT("(%s ? %s : %s)"),
		ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1),
		TrueExpression->GetValueShader(Context, GetType()),
		FalseExpression->GetValueShader(Context, GetType()));
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
{
	check(false); // TODO
}

void FExpressionBinaryOp::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	const FPrepareValueResult LhsResult = PrepareExpressionValue(Context, Lhs, RequestedType);
	const FPrepareValueResult RhsResult = PrepareExpressionValue(Context, Rhs, RequestedType);
	if (!LhsResult || !RhsResult)
	{
		return;
	}

	FString ErrorMessage;
	Shader::EValueType ResultType;
	switch (Op)
	{
	case EBinaryOp::Less:
		ResultType = Shader::MakeComparisonResultType(LhsResult.Type, RhsResult.Type, ErrorMessage);
		break;
	default:
		ResultType = Shader::MakeArithmeticResultType(LhsResult.Type, RhsResult.Type, ErrorMessage);
		break;
	}
	if (!ErrorMessage.IsEmpty())
	{
		return Context.Errors.AddError(this, ErrorMessage);
	}

	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	const EExpressionEvaluationType EvaluationType = OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop
		? CombineEvaluationTypes(LhsResult.EvaluationType, RhsResult.EvaluationType)
		: EExpressionEvaluationType::Shader;
	return SetType(Context, EvaluationType, ResultType);
}

void FExpressionBinaryOp::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const TCHAR* LhsCode = Lhs->GetValueShader(Context);
	const TCHAR* RhsCode = Rhs->GetValueShader(Context);
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

void FExpressionBinaryOp::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
{
	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Lhs->GetValuePreshader(Context, OutPreshader);
	Rhs->GetValuePreshader(Context, OutPreshader);
	OutPreshader.WriteOpcode(OpDesc.PreshaderOpcode);
}

void FExpressionSwizzle::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	FRequestedType RequestedInputType(4, false);
	int32 MaxComponentIndex = 0;
	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		if (RequestedType.IsComponentRequested(ComponentIndex))
		{
			const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
			RequestedInputType.RequestedComponents[SwizzledComponentIndex] = true;
			MaxComponentIndex = ComponentIndex;
		}
	}

	const FPrepareValueResult InputResult = PrepareExpressionValue(Context, Input, RequestedInputType);
	return SetType(Context, InputResult.EvaluationType, Shader::MakeValueType(InputResult.Type, MaxComponentIndex + 1));
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(GetType());
	const int32 NumSwizzleComponents = FMath::Min<int32>(TypeDesc.NumComponents, Parameters.NumComponents);

	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	bool bHasSwizzleReorder = false;

	for (int32 i = 0; i < NumSwizzleComponents; ++i)
	{
		const int32 ComponentIndex = Parameters.ComponentIndex[i];
		check(ComponentIndex >= 0 && ComponentIndex < 4);
		Swizzle[i] = ComponentName[ComponentIndex];
		if (ComponentIndex != i)
		{
			bHasSwizzleReorder = true;
		}
	}

	OutShader.bInline = true;
	if (bHasSwizzleReorder)
	{
		OutShader.Code.Appendf(TEXT("%s.%s"),
			Input->GetValueShader(Context),
			Swizzle);
	}
	else
	{
		const Shader::EValueType InputType = Shader::MakeValueType(GetType(), NumSwizzleComponents);
		OutShader.Code.Appendf(TEXT("%s"), Input->GetValueShader(Context, InputType));
	}
}

void FExpressionSwizzle::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
{
	Input->GetValuePreshader(Context, OutPreshader);
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ComponentSwizzle)
		.Write((uint8)Parameters.NumComponents)
		.Write((uint8)Parameters.ComponentIndex[0])
		.Write((uint8)Parameters.ComponentIndex[1])
		.Write((uint8)Parameters.ComponentIndex[2])
		.Write((uint8)Parameters.ComponentIndex[3]);
}

void FExpressionAppend::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	const FPrepareValueResult LhsResult = PrepareExpressionValue(Context, Lhs, 4);
	const FPrepareValueResult RhsResult = PrepareExpressionValue(Context, Rhs, 4);
	if (!LhsResult || !RhsResult)
	{
		return;
	}

	if (LhsResult.Type.IsStruct() || RhsResult.Type.IsStruct())
	{
		return Context.Errors.AddError(this, TEXT("Not supported for structs"));
	}

	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsResult.Type);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsResult.Type);
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	const EExpressionEvaluationType EvaluationType = CombineEvaluationTypes(LhsResult.EvaluationType, RhsResult.EvaluationType);
	const int32 NumComponents = FMath::Min<int32>(LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents, RequestedType.GetRequestedNumComponents());
	return SetType(Context, EvaluationType, Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents));
}

void FExpressionAppend::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const Shader::FValueTypeDescription ResultTypeDesc = Shader::GetValueTypeDescription(GetType());
	OutShader.bInline = true;
	OutShader.Code.Appendf(TEXT("%s(%s, %s)"),
		ResultTypeDesc.Name,
		Lhs->GetValueShader(Context),
		Rhs->GetValueShader(Context));
}

void FExpressionAppend::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
{
	Lhs->GetValuePreshader(Context, OutPreshader);
	Rhs->GetValuePreshader(Context, OutPreshader);
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
}

void FExpressionReflectionVector::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
	Context.bReadMaterialNormal = true;
	return SetType(Context, EExpressionEvaluationType::Shader, Shader::EValueType::Float3);
}

void FExpressionReflectionVector::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	OutShader.bInline = true;
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
	const FPrepareValueResult ConditionResult = PrepareExpressionValue(Context, ConditionExpression, 1);
	if (ConditionResult.EvaluationType == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionResult.ConstantValue.AsBoolScalar();
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
	else if(ConditionResult)
	{
		MarkScopeLive(ThenScope);
		MarkScopeLive(ElseScope);
	}
}

void FStatementIf::EmitShader(FEmitContext& Context) const
{
	if (ConditionExpression->GetEvaluationType() == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context).AsBoolScalar();
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
