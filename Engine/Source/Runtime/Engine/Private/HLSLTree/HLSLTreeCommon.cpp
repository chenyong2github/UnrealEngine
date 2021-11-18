// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

namespace UE
{
namespace HLSLTree
{

FBinaryOpDescription GetBinaryOpDesription(EBinaryOp Op)
{
	switch (Op)
	{
	case EBinaryOp::None: return FBinaryOpDescription(TEXT("None"), TEXT("")); break;
	case EBinaryOp::Add: return FBinaryOpDescription(TEXT("Add"), TEXT("+")); break;
	case EBinaryOp::Sub: return FBinaryOpDescription(TEXT("Subtract"), TEXT("-")); break;
	case EBinaryOp::Mul: return FBinaryOpDescription(TEXT("Multiply"), TEXT("*")); break;
	case EBinaryOp::Div: return FBinaryOpDescription(TEXT("Divide"), TEXT("/")); break;
	case EBinaryOp::Less: return FBinaryOpDescription(TEXT("Less"), TEXT("<")); break;
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

void FExpressionConstant::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	if (Value.Type.IsStruct())
	{
		if (Value.Type.StructType != RequestedType.GetStructType())
		{
			return Context.Errors.AddError(this, TEXT("Type mismatch"));
		}
		return SetType(Context, Value.Type);
	}
	else
	{
		return SetType(Context, Shader::MakeValueTypeWithRequestedNumComponents(Value.Type, RequestedType.GetRequestedNumComponents()));
	}
}


void FExpressionConstant::PrepareValue(FEmitContext& Context)
{
	check(Value.Component.Num() == Value.Type.GetNumComponents());
	if (Value.Type.IsStruct())
	{
		const TArrayView<const Shader::EValueComponentType> ComponentTypes = Value.Type.StructType->ComponentTypes;
		// TODO - Support for preshader structs?
		TStringBuilder<1024> FormattedCode;
		FormattedCode.Append(TEXT("{ "));
		for (int32 ComponentIndex = 0; ComponentIndex < Value.Component.Num(); ++ComponentIndex)
		{
			if (ComponentIndex > 0)
			{
				FormattedCode.Append(TEXT(", "));
			}
			Value.Component[ComponentIndex].ToString(ComponentTypes[ComponentIndex], FormattedCode);
		}
		FormattedCode.Append(TEXT(" }"));
		return SetValueShader(Context, FormattedCode);
	}
	else
	{
		Shader::FValue BaseValue(Value.Type.ValueType);
		for (int32 ComponentIndex = 0; ComponentIndex < Value.Component.Num(); ++ComponentIndex)
		{
			BaseValue.Component[ComponentIndex] = Value.Component[ComponentIndex];
		}
		return SetValueConstant(Context, BaseValue);
	}
}

void FExpressionMaterialParameter::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	const Shader::EValueType CurrentType = Shader::MakeValueTypeWithRequestedNumComponents(GetShaderValueType(ParameterType), RequestedType.GetRequestedNumComponents());
	return SetType(Context, CurrentType);
}

void FExpressionMaterialParameter::PrepareValue(FEmitContext& Context)
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

		return SetValueConstant(Context, Value);
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

		Shader::FPreshaderData LocalPreshader;
		LocalPreshader.WriteOpcode(Shader::EPreshaderOpcode::Parameter).Write((uint16)ParameterIndex);
		return SetValuePreshader(Context, LocalPreshader);
	}
}

void FExpressionExternalInput::PrepareValue(FEmitContext& Context)
{
	const int32 TypeIndex = (int32)InputType;
	const int32 TexCoordIndex = TypeIndex - (int32)EExternalInputType::TexCoord0;

	if (TexCoordIndex >= 0 && TexCoordIndex < 8)
	{
		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		return SetValueInlineShaderf(Context, TEXT("Parameters.TexCoords[%u].xy"), TexCoordIndex);
	}
	else
	{
		return Context.Errors.AddError(this, TEXT("Invalid texcoord"));
	}
}

void FExpressionTextureSample::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	RequestExpressionType(Context, TexCoordExpression, 2);
	return SetType(Context, Shader::EValueType::Float4);
}

void FExpressionTextureSample::PrepareValue(FEmitContext& Context)
{
	if (PrepareExpressionValue(Context, TexCoordExpression) == EExpressionEvaluationType::None)
	{
		return;
	}

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

	return SetValueShaderf(Context, TEXT("%s(%s(%s, %s, %s))"),
		SamplerTypeFunction,
		SampleFunctionName,
		*TextureName,
		*SamplerStateCode,
		TexCoordExpression->GetValueShader(Context, Shader::EValueType::Float2));
}

void FExpressionGetStructField::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
	if (!FieldRef)
	{
		return Context.Errors.AddErrorf(this, TEXT("Invalid field %s"), FieldName);
	}

	if (RequestExpressionType(Context, StructExpression, RequestedType.MakeFieldAccess(StructType, FieldRef)) != FType(StructType))
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	return SetType(Context, FieldRef.Type);
}

void FExpressionGetStructField::PrepareValue(FEmitContext& Context)
{
	const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
	check(FieldRef);

	if (PrepareExpressionValue(Context, StructExpression) == EExpressionEvaluationType::None)
	{
		return;
	}

	return SetValueInlineShaderf(Context, TEXT("%s.%s"),
		StructExpression->GetValueShader(Context, StructType),
		FieldName);
}

void FExpressionSetStructField::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
	if (!FieldRef)
	{
		return Context.Errors.AddErrorf(this, TEXT("Invalid field %s"), FieldName);
	}

	const FType StructExpressionType = RequestExpressionType(Context, StructExpression, RequestedType.CopyWithFieldRemoved(FieldRef));
	if (StructExpressionType && StructExpressionType.StructType != StructType)
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	RequestExpressionType(Context, FieldExpression, RequestedType.GetField(FieldRef));
	return SetType(Context, StructType);
}

void FExpressionSetStructField::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType StructEvaluation = PrepareExpressionValue(Context, StructExpression);
	const EExpressionEvaluationType FieldEvaluation = PrepareExpressionValue(Context, FieldExpression);

	if (FieldEvaluation != EExpressionEvaluationType::None)
	{
		const FStructFieldRef FieldRef = StructType->FindFieldByName(FieldName);
		check(FieldRef);

		if (StructEvaluation != EExpressionEvaluationType::None)
		{
			return SetValueShaderf(Context, TEXT("%s_Set%s(%s, %s)"),
				StructType->Name,
				FieldName,
				StructExpression->GetValueShader(Context, StructType),
				FieldExpression->GetValueShader(Context, FieldRef.Type));
		}
		else
		{
			// StructExpression is not used, so default to a zero-initialized struct
			// This will happen if all the accessed struct fields are explicitly defined
			return SetValueShaderf(Context, TEXT("%s_Set%s((%s)0, %s)"),
				StructType->Name,
				FieldName,
				StructType->Name,
				FieldExpression->GetValueShader(Context, FieldRef.Type));
		}
	}
	else
	{
		// Skip assigning the field, and just forward the struct value
		check(StructEvaluation != EExpressionEvaluationType::None);
		return SetValueForward(Context, StructExpression);
	}
}

void FExpressionSelect::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	const FType ConditionType = RequestExpressionType(Context, ConditionExpression, 1);
	const FType LhsType = RequestExpressionType(Context, FalseExpression, RequestedType);
	const FType RhsType = RequestExpressionType(Context, TrueExpression, RequestedType);
	if (!ConditionType || !LhsType || !RhsType)
	{
		return;
	}

	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	if (LhsType.IsStruct())
	{
		return SetType(Context, LhsType);
	}
	else
	{
		const int8 NumComponents = FMath::Max(LhsTypeDesc.NumComponents, RhsTypeDesc.NumComponents);
		return SetType(Context, Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents));
	}
}

void FExpressionSelect::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType ConditionEvaluation = PrepareExpressionValue(Context, ConditionExpression);
	if (ConditionEvaluation == EExpressionEvaluationType::None)
	{
		return;
	}

	if (ConditionEvaluation == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context).AsBoolScalar();
		return SetValueForward(Context, bCondition ? TrueExpression : FalseExpression);
	}
	else
	{
		const EExpressionEvaluationType TrueEvaluation = PrepareExpressionValue(Context, TrueExpression);
		const EExpressionEvaluationType FalseEvaluation = PrepareExpressionValue(Context, FalseExpression);
		if (TrueEvaluation == EExpressionEvaluationType::None || FalseEvaluation == EExpressionEvaluationType::None)
		{
			return;
		}

		// TODO - preshader
		return SetValueShaderf(Context, TEXT("(%s ? %s : %s)"),
			ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1),
			TrueExpression->GetValueShader(Context, GetType()),
			FalseExpression->GetValueShader(Context, GetType()));
	}
}

void FExpressionBinaryOp::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	const FType LhsType = RequestExpressionType(Context, Lhs, RequestedType);
	const FType RhsType = RequestExpressionType(Context, Rhs, RequestedType);
	if (!LhsType || !RhsType)
	{
		return;
	}

	FString ErrorMessage;
	Shader::EValueType ResultType;
	switch (Op)
	{
	case EBinaryOp::Less:
		ResultType = Shader::MakeComparisonResultType(LhsType, RhsType, ErrorMessage);
		break;
	default:
		ResultType = Shader::MakeArithmeticResultType(LhsType, RhsType, ErrorMessage);
		break;
	}
	if (!ErrorMessage.IsEmpty())
	{
		return Context.Errors.AddError(this, ErrorMessage);
	}

	return SetType(Context, ResultType);
}

void FExpressionBinaryOp::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType LhsEvaluation = PrepareExpressionValue(Context, Lhs);
	const EExpressionEvaluationType RhsEvaluation = PrepareExpressionValue(Context, Rhs);
	if (LhsEvaluation == EExpressionEvaluationType::None || RhsEvaluation == EExpressionEvaluationType::None)
	{
		return;
	}

	const EExpressionEvaluationType Evaluation = CombineEvaluationTypes(LhsEvaluation, RhsEvaluation);
	Shader::EPreshaderOpcode Opcode = Shader::EPreshaderOpcode::Nop;
	if (Evaluation != EExpressionEvaluationType::Shader)
	{
		switch (Op)
		{
		case EBinaryOp::Add: Opcode = Shader::EPreshaderOpcode::Add; break;
		case EBinaryOp::Sub: Opcode = Shader::EPreshaderOpcode::Add; break;
		case EBinaryOp::Mul: Opcode = Shader::EPreshaderOpcode::Mul; break;
		case EBinaryOp::Div: Opcode = Shader::EPreshaderOpcode::Div; break;
		default: break;
		}
	}

	if (Opcode == Shader::EPreshaderOpcode::Nop)
	{
		// Either preshader not supported for our inputs, or not for this operation type
		const TCHAR* LhsCode = Lhs->GetValueShader(Context);
		const TCHAR* RhsCode = Rhs->GetValueShader(Context);
		switch (Op)
		{
		case EBinaryOp::Add: return SetValueShaderf(Context, TEXT("(%s + %s)"), LhsCode, RhsCode);
		case EBinaryOp::Sub: return SetValueShaderf(Context, TEXT("(%s - %s)"), LhsCode, RhsCode);
		case EBinaryOp::Mul: return SetValueShaderf(Context, TEXT("(%s * %s)"), LhsCode, RhsCode);
		case EBinaryOp::Div: return SetValueShaderf(Context, TEXT("(%s / %s)"), LhsCode, RhsCode);
		case EBinaryOp::Less: return SetValueShaderf(Context, TEXT("(%s < %s)"), LhsCode, RhsCode);
		default: checkNoEntry(); return;
		}
	}
	else
	{
		Shader::FPreshaderData LocalPreshader;
		Lhs->GetValuePreshader(Context, LocalPreshader);
		Rhs->GetValuePreshader(Context, LocalPreshader);
		LocalPreshader.WriteOpcode(Opcode);
		return SetValuePreshader(Context, Evaluation, LocalPreshader);
	}
}

void FExpressionSwizzle::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
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

	const Shader::EValueType InputType = RequestExpressionType(Context, Input, RequestedInputType);
	return SetType(Context, Shader::MakeValueType(InputType, MaxComponentIndex + 1));
}

void FExpressionSwizzle::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType InputEvaluation = PrepareExpressionValue(Context, Input);
	if (InputEvaluation == EExpressionEvaluationType::None)
	{
		return;
	}

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

	if (InputEvaluation == EExpressionEvaluationType::Shader)
	{
		if (bHasSwizzleReorder)
		{
			return SetValueInlineShaderf(Context, TEXT("%s.%s"),
				Input->GetValueShader(Context),
				Swizzle);
		}
		else
		{
			const Shader::EValueType InputType = Shader::MakeValueType(GetType(), NumSwizzleComponents);
			return SetValueInlineShaderf(Context, TEXT("%s"), Input->GetValueShader(Context, InputType));
		}
	}
	else
	{
		Shader::FPreshaderData LocalPreshader;
		Input->GetValuePreshader(Context, LocalPreshader);
		LocalPreshader.WriteOpcode(Shader::EPreshaderOpcode::ComponentSwizzle)
			.Write((uint8)Parameters.NumComponents)
			.Write((uint8)Parameters.ComponentIndex[0])
			.Write((uint8)Parameters.ComponentIndex[1])
			.Write((uint8)Parameters.ComponentIndex[2])
			.Write((uint8)Parameters.ComponentIndex[3]);
		return SetValuePreshader(Context, InputEvaluation, LocalPreshader);
	}
}

void FExpressionAppend::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	const FType LhsType = RequestExpressionType(Context, Lhs, 4);
	const FType RhsType = RequestExpressionType(Context, Rhs, 4);
	if (!LhsType || !RhsType)
	{
		return;
	}

	if (LhsType.IsStruct() || RhsType.IsStruct())
	{
		return Context.Errors.AddError(this, TEXT("Not supported for structs"));
	}

	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	const int32 NumComponents = FMath::Min<int32>(LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents, RequestedType.GetRequestedNumComponents());
	return SetType(Context, Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents));
}

void FExpressionAppend::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType LhsEvaluation = PrepareExpressionValue(Context, Lhs);
	const EExpressionEvaluationType RhsEvaluation = PrepareExpressionValue(Context, Rhs);
	if (LhsEvaluation == EExpressionEvaluationType::None || RhsEvaluation == EExpressionEvaluationType::None)
	{
		return;
	}

	const EExpressionEvaluationType Evaluation = CombineEvaluationTypes(LhsEvaluation, RhsEvaluation);
	if (Evaluation == EExpressionEvaluationType::Shader)
	{
		const Shader::FValueTypeDescription ResultTypeDesc = Shader::GetValueTypeDescription(GetType());
		return SetValueShaderf(Context, TEXT("%s(%s, %s)"),
			ResultTypeDesc.Name,
			Lhs->GetValueShader(Context),
			Rhs->GetValueShader(Context));
	}
	else
	{
		Shader::FPreshaderData LocalPreshader;
		Lhs->GetValuePreshader(Context, LocalPreshader);
		Rhs->GetValuePreshader(Context, LocalPreshader);
		LocalPreshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
		return SetValuePreshader(Context, Evaluation, LocalPreshader);
	}
}

void FExpressionReflectionVector::PrepareValue(FEmitContext& Context)
{
	return SetValueInlineShaderf(Context, TEXT("Parameters.ReflectionVector"));
}

void FStatementBreak::EmitHLSL(FEmitContext& Context) const
{
	ParentScope->MarkLive();
	ParentScope->EmitStatementf(Context, TEXT("break;"));
}

void FStatementReturn::RequestTypes(FUpdateTypeContext& Context) const
{
	RequestExpressionType(Context, Expression, Type);
}

void FStatementReturn::EmitHLSL(FEmitContext& Context) const
{
	if (PrepareExpressionValue(Context, Expression) == EExpressionEvaluationType::None)
	{
		return;
	}

	ParentScope->MarkLiveRecursive();
	ParentScope->EmitStatementf(Context, TEXT("return %s;"), Expression->GetValueShader(Context));
}

void FStatementIf::RequestTypes(FUpdateTypeContext& Context) const
{
	RequestExpressionType(Context, ConditionExpression, 1);
	RequestScopeTypes(Context, ThenScope);
	RequestScopeTypes(Context, ElseScope);
	RequestScopeTypes(Context, NextScope);
}

void FStatementIf::EmitHLSL(FEmitContext& Context) const
{
	bool bIfLive = ThenScope->IsLive();
	if (ElseScope && ElseScope->IsLive())
	{
		bIfLive = true;
	}

	if (bIfLive)
	{
		if (PrepareExpressionValue(Context, ConditionExpression) == EExpressionEvaluationType::None)
		{
			return;
		}

		ParentScope->EmitNestedScopef(Context, ThenScope, TEXT("if (%s)"),
			ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1));
		if (ElseScope && ElseScope->IsLive())
		{
			ParentScope->EmitNestedScopef(Context, ElseScope, TEXT("else"));
		}
	}
	ParentScope->EmitNestedScope(Context, NextScope);
}

void FStatementLoop::RequestTypes(FUpdateTypeContext& Context) const
{
	RequestScopeTypes(Context, LoopScope);
	RequestScopeTypes(Context, NextScope);
}

void FStatementLoop::EmitHLSL(FEmitContext& Context) const
{
	if (LoopScope->IsLive())
	{
		ParentScope->EmitNestedScopef(Context, LoopScope, TEXT("while (true)"));
	}
	ParentScope->EmitNestedScope(Context, NextScope);
}

} // namespace HLSLTree
} // namespace UE
