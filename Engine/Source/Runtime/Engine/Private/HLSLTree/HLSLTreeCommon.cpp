// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

namespace UE
{
namespace HLSLTree
{

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
		const EComponentRequest ComponentRequest = RequestedType.GetComponentRequest(Index);
		if (ComponentRequest != EComponentRequest::None)
		{
			const int32 SwizzledComponentIndex = ComponentIndex[Index];
			RequestedInputType.SetComponentRequest(SwizzledComponentIndex, ComponentRequest);
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
	OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Constant, Value.Type);
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
}

void FExpressionMaterialParameter::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const EExpressionEvaluation Evaluation = IsStaticMaterialParameter(ParameterType) ? EExpressionEvaluation::Constant : EExpressionEvaluation::Preshader;
	OutResult.SetType(Context, RequestedType, Evaluation, GetShaderValueType(ParameterType));
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

void FExpressionExternalInput::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	// TODO - derivative type
	OutResult.SetTypeWithDerivative(Context, RequestedType, EExpressionEvaluation::Shader, GetInputExpressionType(InputType));
}

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	if (IsTexCoord(InputType))
	{
		const int32 TypeIndex = (int32)InputType;
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInputType::TexCoord0;

		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		OutResult.Code = Context.EmitInlineCode(Shader::EValueType::Float2, TEXT("Parameters.TexCoords[%].xy"), TexCoordIndex);
		if (GetDerivative(RequestedType) == EExpressionDerivative::Valid)
		{
			OutResult.CodeDdx = Context.EmitInlineCode(Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDX[%].xy"), TexCoordIndex);
			OutResult.CodeDdy = Context.EmitInlineCode(Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDY[%].xy"), TexCoordIndex);
		}
	}
	else
	{
		const Shader::EValueType Type = GetInputExpressionType(InputType);

		// TODO - handle PrevFrame position
		switch (InputType)
		{
		case EExternalInputType::WorldPosition:
			OutResult.Code = Context.EmitInlineCode(Type, TEXT("GetWorldPosition(Parameters)"));
			break;
		case EExternalInputType::WorldPosition_NoOffsets:
			OutResult.Code = Context.EmitInlineCode(Type, TEXT("GetWorldPosition_NoMaterialOffsets(Parameters)"));
			break;
		case EExternalInputType::TranslatedWorldPosition:
			OutResult.Code = Context.EmitInlineCode(Type, TEXT("GetTranslatedWorldPosition(Parameters)"));
			break;
		case EExternalInputType::TranslatedWorldPosition_NoOffsets:
			OutResult.Code = Context.EmitInlineCode(Type, TEXT("GetTranslatedWorldPosition_NoMaterialOffsets(Parameters)"));
			break;
		default:
			checkNoEntry();
			break;
		}

		if (GetDerivative(RequestedType) == EExpressionDerivative::Valid)
		{
			OutResult.CodeDdx = Context.EmitInlineCode(Shader::EValueType::Float3, TEXT("Parameters.WorldPosition_DDX"));
			OutResult.CodeDdy = Context.EmitInlineCode(Shader::EValueType::Float3, TEXT("Parameters.WorldPosition_DDY"));
		}
	}
}

void FExpressionTextureSample::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FRequestedType TexCoordRequestedType(Shader::EValueType::Float2, EComponentRequest::RequestedWithDerivative);
	PrepareExpressionValue(Context, TexCoordExpression, TexCoordRequestedType);
	OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
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

	const FRequestedType TexCoordRequestedType(Shader::EValueType::Float2, EComponentRequest::RequestedWithDerivative);
	const FEmitShaderValues TexCoordValue = TexCoordExpression->GetValueShader(Context, TexCoordRequestedType);
	OutResult.Code = Context.EmitCode(Shader::EValueType::Float4, TEXT("%(%Grad(%, %, %, %, %))"),
		SamplerTypeFunction,
		SampleFunctionName,
		*TextureName,
		*SamplerStateCode,
		TexCoordValue.Code,
		TexCoordValue.CodeDdx,
		TexCoordValue.CodeDdy);
}

void FExpressionGetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(StructType, EComponentRequest::None);
	RequestedStructType.SetField(Field, RequestedType);

	const FPreparedType& StructPreparedType = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors.AddErrorf(this, TEXT("Expected type %s"), StructType->Name);
	}

	OutResult.SetType(Context, RequestedType, StructPreparedType.GetFieldType(Field));
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	FRequestedType RequestedStructType(StructType, EComponentRequest::None);
	RequestedStructType.SetField(Field, RequestedType);

	const FEmitShaderValues StructValue = StructExpression->GetValueShader(Context, RequestedStructType);

	OutResult.Code = Context.EmitInlineCode(Field->Type, TEXT("%.%"),
		StructValue.Code,
		Field->Name);

	if (StructValue.HasDerivatives() && GetDerivative(RequestedType) == EExpressionDerivative::Valid)
	{
		const Shader::FType DerivativeType = Field->Type.GetDerivativeType();
		OutResult.CodeDdx = Context.EmitInlineCode(DerivativeType, TEXT("%.%"),
			StructValue.CodeDdx,
			Field->Name);
		OutResult.CodeDdy = Context.EmitInlineCode(DerivativeType, TEXT("%.%"),
			StructValue.CodeDdy,
			Field->Name);
	}
}

void FExpressionGetStructField::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(StructType, EComponentRequest::None);
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

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(RequestedFieldType);

	const FEmitShaderValues FieldValue = FieldExpression->GetValueShader(Context, RequestedFieldType, Field->Type);

	const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
	const EExpressionDerivative Derivative = GetDerivative(RequestedType);

	if (StructEvaluation == EExpressionEvaluation::None)
	{
		check(FieldEvaluation != EExpressionEvaluation::None);

		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutResult.Code = Context.EmitCode(StructType, TEXT("%_Set%((%)0, %)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldValue.Code);
		if (Derivative == EExpressionDerivative::Valid && DerivativeStructType && FieldValue.HasDerivatives())
		{
			OutResult.CodeDdx = Context.EmitCode(DerivativeStructType, TEXT("%_Set%((%)0, %)"),
				DerivativeStructType->Name,
				Field->Name,
				DerivativeStructType->Name,
				FieldValue.CodeDdx);
			OutResult.CodeDdy = Context.EmitCode(DerivativeStructType, TEXT("%_Set%((%)0, %)"),
				DerivativeStructType->Name,
				Field->Name,
				DerivativeStructType->Name,
				FieldValue.CodeDdy);
		}
	}
	else
	{
		const FEmitShaderValues StructValue = StructExpression->GetValueShader(Context, RequestedStructType);
		OutResult.Code = Context.EmitCode(StructType, TEXT("%_Set%(%, %)"),
			StructType->Name,
			Field->Name,
			StructValue.Code,
			FieldValue.Code);
		if (Derivative == EExpressionDerivative::Valid && DerivativeStructType && StructValue.HasDerivatives() && FieldValue.HasDerivatives())
		{
			OutResult.CodeDdx = Context.EmitCode(DerivativeStructType, TEXT("%_Set%(%, %)"),
				DerivativeStructType->Name,
				Field->Name,
				StructValue.CodeDdx,
				FieldValue.CodeDdx);
			OutResult.CodeDdy = Context.EmitCode(DerivativeStructType, TEXT("%_Set%(%, %)"),
				DerivativeStructType->Name,
				Field->Name,
				StructValue.CodeDdy,
				FieldValue.CodeDdy);
		}
	}
}

void FExpressionSetStructField::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(RequestedFieldType);

	if (StructEvaluation != EExpressionEvaluation::None)
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
	if (ConditionType.GetEvaluation(Shader::EValueType::Bool1) == EExpressionEvaluation::Constant)
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

void FExpressionSelect::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	const FEmitShaderValues TrueValue = TrueExpression->GetValueShader(Context, RequestedType);
	const FEmitShaderValues FalseValue = FalseExpression->GetValueShader(Context, RequestedType);

	OutResult.Code = Context.EmitCode(RequestedType.GetType(), TEXT("(% ? % : %)"),
		ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1).Code,
		TrueValue.Code,
		FalseValue.Code);
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	check(false); // TODO
}

void FExpressionUnaryOp::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& InputType = PrepareExpressionValue(Context, Input, RequestedType);
	if (InputType.IsVoid())
	{
		return;
	}

	if (!InputType.IsNumeric())
	{
		return Context.Errors.AddError(this, TEXT("Invalid operation on non-numeric type"));
	}

	const FUnaryOpDescription OpDesc = GetUnaryOpDesription(Op);
	FPreparedType ResultType = InputType;
	if (OpDesc.PreshaderOpcode == Shader::EPreshaderOpcode::Nop)
	{
		// No preshader support
		ResultType.SetEvaluation(EExpressionEvaluation::Shader);
	}

	OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionUnaryOp::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	OutResult = Context.EmitUnaryOp(Op,
		Input->GetValueShader(Context, RequestedType),
		GetDerivative(RequestedType));
}

void FExpressionUnaryOp::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FUnaryOpDescription OpDesc = GetUnaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Input->GetValuePreshader(Context, RequestedType, OutPreshader);
	OutPreshader.WriteOpcode(OpDesc.PreshaderOpcode);
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
		ResultType.SetDerivative(EExpressionDerivative::Zero); // Comparisons result in 'zero' derivative...is this correct?
	}

	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	if (OpDesc.PreshaderOpcode == Shader::EPreshaderOpcode::Nop)
	{
		// No preshader support
		ResultType.SetEvaluation(EExpressionEvaluation::Shader);
	}

	OutResult.SetType(Context, RequestedType, ResultType);
}

namespace Private
{
FRequestedType GetBinaryOpInputType(const FRequestedType& RequestedType, EBinaryOp Op, const FRequestedType& LhsType, const FRequestedType& RhsType)
{
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const Shader::EValueComponentType InputComponentType = Shader::CombineComponentTypes(LhsType.ValueComponentType, RhsType.ValueComponentType);
	return MakeRequestedType(InputComponentType, RequestedType);
}
} // namespace Private

void FExpressionBinaryOp::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	const FRequestedType InputType = Private::GetBinaryOpInputType(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	OutResult = Context.EmitBinaryOp(Op,
		Lhs->GetValueShader(Context, InputType),
		Rhs->GetValueShader(Context, InputType),
		GetDerivative(RequestedType));
}

void FExpressionBinaryOp::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FRequestedType InputType = Private::GetBinaryOpInputType(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Lhs->GetValuePreshader(Context, InputType, OutPreshader);
	Rhs->GetValuePreshader(Context, InputType, OutPreshader);
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
		ResultType.SetComponentData(ComponentIndex, InputType.GetComponentData(SwizzledComponentIndex));
	}

	OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
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

	const FEmitShaderValues InputValue = Input->GetValueShader(Context, RequestedInputType);
	if (bHasSwizzleReorder)
	{
		OutResult.Code = Context.EmitInlineCode(RequestedType.GetType(), TEXT("%.%"),
			InputValue.Code,
			Swizzle);
	}
	else
	{
		OutResult.Code = InputValue.Code;
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
	const FPreparedType& LhsType = PrepareExpressionValue(Context, Lhs, RequestedType);
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min(LhsType.GetNumComponents(), NumRequestedComponents);

	FRequestedType RhsRequestedType;
	for (int32 Index = NumLhsComponents; Index < NumRequestedComponents; ++Index)
	{
		RhsRequestedType.SetComponentRequest(Index - NumLhsComponents, RequestedType.GetComponentRequest(Index));
	}

	const FPreparedType& RhsType = PrepareExpressionValue(Context, Rhs, RhsRequestedType);
	const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);

	if (LhsType.ValueComponentType != RhsType.ValueComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponentData(Index, LhsType.GetComponentData(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		ResultType.SetComponentData(NumLhsComponents + Index, LhsType.GetComponentData(Index));
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
		Types.LhsType.SetComponentRequest(Index, RequestedType.GetComponentRequest(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		Types.RhsType.SetComponentRequest(Index, RequestedType.GetComponentRequest(NumLhsComponents + Index));
	}
	return Types;
}
} // namespace Private

void FExpressionAppend::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const FEmitShaderValues LhsValue = Lhs->GetValueShader(Context, Types.LhsType);

	if (Types.RhsType.IsVoid())
	{
		OutResult.Code = LhsValue.Code;
	}
	else
	{
		const FEmitShaderValues RhsValue = Rhs->GetValueShader(Context, Types.RhsType);
		OutResult.Code = Context.EmitInlineCode(Types.ResultType, TEXT("%(%, %)"),
			RequestedType.GetName(),
			LhsValue.Code,
			RhsValue.Code);
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
	OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float3);
}

void FExpressionReflectionVector::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const
{
	OutResult.Code = Context.EmitInlineCode(Shader::EValueType::Float3, TEXT("Parameters.ReflectionVector"));
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
	ParentScope->EmitStatementf(Context, TEXT("return %s;"), Expression->GetValueShader(Context).Code->Reference);
}

void FStatementIf::Prepare(FEmitContext& Context) const
{
	const FPreparedType& ConditionType = PrepareExpressionValue(Context, ConditionExpression, 1);
	if (ConditionType.GetEvaluation(Shader::EValueType::Bool1) == EExpressionEvaluation::Constant)
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
	if (ConditionExpression->GetEvaluation(Shader::EValueType::Bool1) == EExpressionEvaluation::Constant)
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
			const FEmitShaderValues ConditionValue = ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1);
			ParentScope->EmitNestedScopef(Context, ThenScope, TEXT("if (%s)"), ConditionValue.Code->Reference);
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
