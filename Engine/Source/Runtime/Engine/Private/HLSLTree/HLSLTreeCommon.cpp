// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

namespace UE
{
namespace HLSLTree
{

FExternalInputDescription GetExternalInputDescription(EExternalInput Input)
{
	switch (Input)
	{
	case EExternalInput::None: return FExternalInputDescription(TEXT("None"), Shader::EValueType::Void);

	case EExternalInput::TexCoord0: return FExternalInputDescription(TEXT("TexCoord0"), Shader::EValueType::Float2, EExternalInput::TexCoord0_Ddx, EExternalInput::TexCoord0_Ddy);
	case EExternalInput::TexCoord1: return FExternalInputDescription(TEXT("TexCoord1"), Shader::EValueType::Float2, EExternalInput::TexCoord1_Ddx, EExternalInput::TexCoord1_Ddy);
	case EExternalInput::TexCoord2: return FExternalInputDescription(TEXT("TexCoord2"), Shader::EValueType::Float2, EExternalInput::TexCoord2_Ddx, EExternalInput::TexCoord2_Ddy);
	case EExternalInput::TexCoord3: return FExternalInputDescription(TEXT("TexCoord3"), Shader::EValueType::Float2, EExternalInput::TexCoord3_Ddx, EExternalInput::TexCoord3_Ddy);
	case EExternalInput::TexCoord4: return FExternalInputDescription(TEXT("TexCoord4"), Shader::EValueType::Float2, EExternalInput::TexCoord4_Ddx, EExternalInput::TexCoord4_Ddy);
	case EExternalInput::TexCoord5: return FExternalInputDescription(TEXT("TexCoord5"), Shader::EValueType::Float2, EExternalInput::TexCoord5_Ddx, EExternalInput::TexCoord5_Ddy);
	case EExternalInput::TexCoord6: return FExternalInputDescription(TEXT("TexCoord6"), Shader::EValueType::Float2, EExternalInput::TexCoord6_Ddx, EExternalInput::TexCoord6_Ddy);
	case EExternalInput::TexCoord7: return FExternalInputDescription(TEXT("TexCoord7"), Shader::EValueType::Float2, EExternalInput::TexCoord7_Ddx, EExternalInput::TexCoord7_Ddy);

	case EExternalInput::TexCoord0_Ddx: return FExternalInputDescription(TEXT("TexCoord0_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord1_Ddx: return FExternalInputDescription(TEXT("TexCoord1_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord2_Ddx: return FExternalInputDescription(TEXT("TexCoord2_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord3_Ddx: return FExternalInputDescription(TEXT("TexCoord3_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord4_Ddx: return FExternalInputDescription(TEXT("TexCoord4_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord5_Ddx: return FExternalInputDescription(TEXT("TexCoord5_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord6_Ddx: return FExternalInputDescription(TEXT("TexCoord6_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord7_Ddx: return FExternalInputDescription(TEXT("TexCoord7_Ddx"), Shader::EValueType::Float2);

	case EExternalInput::TexCoord0_Ddy: return FExternalInputDescription(TEXT("TexCoord0_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord1_Ddy: return FExternalInputDescription(TEXT("TexCoord1_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord2_Ddy: return FExternalInputDescription(TEXT("TexCoord2_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord3_Ddy: return FExternalInputDescription(TEXT("TexCoord3_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord4_Ddy: return FExternalInputDescription(TEXT("TexCoord4_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord5_Ddy: return FExternalInputDescription(TEXT("TexCoord5_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord6_Ddy: return FExternalInputDescription(TEXT("TexCoord6_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord7_Ddy: return FExternalInputDescription(TEXT("TexCoord7_Ddy"), Shader::EValueType::Float2);

	case EExternalInput::WorldPosition: return FExternalInputDescription(TEXT("WorldPosition"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::WorldPosition_NoOffsets: return FExternalInputDescription(TEXT("WorldPosition_NoOffsets"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::TranslatedWorldPosition: return FExternalInputDescription(TEXT("TranslatedWorldPosition"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::TranslatedWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("TranslatedWorldPosition_NoOffsets"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);

	case EExternalInput::WorldPosition_Ddx: return FExternalInputDescription(TEXT("WorldPosition_Ddx"), Shader::EValueType::Float3);
	case EExternalInput::WorldPosition_Ddy: return FExternalInputDescription(TEXT("WorldPosition_Ddx"), Shader::EValueType::Float3);
	default: checkNoEntry(); return FExternalInputDescription(TEXT("Invalid"), Shader::EValueType::Void);
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

bool FExpressionConstant::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Constant, Value.Type);
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
}

void FExpressionMaterialParameter::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const Shader::FType Type = GetShaderValueType(ParameterType);
	const Shader::FType DerivativeType = Type.GetDerivativeType();
	if (!DerivativeType.IsVoid())
	{
		const Shader::FValue ZeroValue(DerivativeType);
		OutResult.ExpressionDdx = Tree.NewConstant(ZeroValue);
		OutResult.ExpressionDdy = OutResult.ExpressionDdx;
	}
}

bool FExpressionMaterialParameter::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const EExpressionEvaluation Evaluation = IsStaticMaterialParameter(ParameterType) ? EExpressionEvaluation::Constant : EExpressionEvaluation::Preshader;
	return OutResult.SetType(Context, RequestedType, Evaluation, GetShaderValueType(ParameterType));
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

void FExpressionExternalInput::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);
	if (InputDesc.Ddx != EExternalInput::None)
	{
		check(InputDesc.Ddy != EExternalInput::None);
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionExternalInput>(InputDesc.Ddx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionExternalInput>(InputDesc.Ddy);
	}
}

bool FExpressionExternalInput::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, InputDesc.Type);
}

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const int32 TypeIndex = (int32)InputType;
	if (IsTexCoord(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0;
		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		OutResult.Code = Context.EmitInlineCode(Shader::EValueType::Float2, TEXT("Parameters.TexCoords[%].xy"), TexCoordIndex);
	}
	else if (IsTexCoord_Ddx(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0_Ddx;
		OutResult.Code = Context.EmitInlineCode(Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDX[%].xy"), TexCoordIndex);
	}
	else if (IsTexCoord_Ddy(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0_Ddy;
		OutResult.Code = Context.EmitInlineCode(Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDY[%].xy"), TexCoordIndex);
	}
	else
	{
		const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);

		// TODO - handle PrevFrame position
		switch (InputType)
		{
		case EExternalInput::WorldPosition:
			OutResult.Code = Context.EmitInlineCode(InputDesc.Type, TEXT("GetWorldPosition(Parameters)"));
			break;
		case EExternalInput::WorldPosition_NoOffsets:
			OutResult.Code = Context.EmitInlineCode(InputDesc.Type, TEXT("GetWorldPosition_NoMaterialOffsets(Parameters)"));
			break;
		case EExternalInput::TranslatedWorldPosition:
			OutResult.Code = Context.EmitInlineCode(InputDesc.Type, TEXT("GetTranslatedWorldPosition(Parameters)"));
			break;
		case EExternalInput::TranslatedWorldPosition_NoOffsets:
			OutResult.Code = Context.EmitInlineCode(InputDesc.Type, TEXT("GetTranslatedWorldPosition_NoMaterialOffsets(Parameters)"));
			break;
		case EExternalInput::WorldPosition_Ddx:
			OutResult.Code = Context.EmitInlineCode(InputDesc.Type, TEXT("Parameters.WorldPosition_DDX"));
			break;
		case EExternalInput::WorldPosition_Ddy:
			OutResult.Code = Context.EmitInlineCode(InputDesc.Type, TEXT("Parameters.WorldPosition_DDY"));
			break;
		default:
			checkNoEntry();
			break;
		}
	}
}

bool FExpressionTextureSample::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	PrepareExpressionValue(Context, TexCoordExpression, Shader::EValueType::Float2);
	PrepareExpressionValue(Context, TexCoordDerivatives.ExpressionDdx, Shader::EValueType::Float2);
	PrepareExpressionValue(Context, TexCoordDerivatives.ExpressionDdy, Shader::EValueType::Float2);

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
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

	FEmitShaderCode* TexCoordValue = TexCoordExpression->GetValueShader(Context, Shader::EValueType::Float2);
	FEmitShaderCode* TexCoordValueDdx = TexCoordDerivatives.ExpressionDdx->GetValueShader(Context, Shader::EValueType::Float2);
	FEmitShaderCode* TexCoordValueDdy = TexCoordDerivatives.ExpressionDdy->GetValueShader(Context, Shader::EValueType::Float2);
	OutResult.Code = Context.EmitCode(Shader::EValueType::Float4, TEXT("%(%Grad(%, %, %, %, %))"),
		SamplerTypeFunction,
		SampleFunctionName,
		*TextureName,
		*SamplerStateCode,
		TexCoordValue,
		TexCoordValueDdx,
		TexCoordValueDdy);
}

void FExpressionGetStructField::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives StructDerivatives = Tree.GetAnalyticDerivatives(StructExpression);
	if (StructDerivatives.IsValid())
	{
		const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
		check(DerivativeStructType);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, Field->Name, StructDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, Field->Name, StructDerivatives.ExpressionDdy);
	}
}

bool FExpressionGetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	const FPreparedType& StructPreparedType = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	return OutResult.SetType(Context, RequestedType, StructPreparedType.GetFieldType(Field));
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	FEmitShaderCode* StructValue = StructExpression->GetValueShader(Context, RequestedStructType);

	OutResult.Code = Context.EmitInlineCode(Field->Type, TEXT("%.%"),
		StructValue,
		Field->Name);
}

void FExpressionGetStructField::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	StructExpression->GetValuePreshader(Context, RequestedStructType, OutPreshader);
	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::GetField).Write(Field->Type).Write(Field->ComponentIndex);
}

void FExpressionSetStructField::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives StructDerivatives = Tree.GetAnalyticDerivatives(StructExpression);
	const FExpressionDerivatives FieldDerivatives = Tree.GetAnalyticDerivatives(FieldExpression);

	if (StructDerivatives.IsValid() && FieldDerivatives.IsValid())
	{
		const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
		check(DerivativeStructType);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, Field->Name, StructDerivatives.ExpressionDdx, FieldDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, Field->Name, StructDerivatives.ExpressionDdy, FieldDerivatives.ExpressionDdy);
	}
}

bool FExpressionSetStructField::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);

	const FPreparedType& StructPreparedType = PrepareExpressionValue(Context, StructExpression, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	const FPreparedType& FieldPreparedType = PrepareExpressionValue(Context, FieldExpression, RequestedType.GetField(Field));
	if (FieldPreparedType.IsVoid())
	{
		return OutResult.SetForwardValue(Context, RequestedType, StructExpression);
	}
	else
	{
		FPreparedType ResultType(StructPreparedType);
		if (ResultType.IsVoid())
		{
			ResultType = StructType;
		}
		ResultType.SetField(Field, FieldPreparedType);
		return OutResult.SetType(Context, RequestedType, ResultType);
	}
}

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(RequestedFieldType);
	FEmitShaderCode* FieldValue = FieldExpression->GetValueShader(Context, RequestedFieldType, Field->Type);

	if (StructEvaluation == EExpressionEvaluation::None)
	{
		check(FieldEvaluation != EExpressionEvaluation::None);

		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutResult.Code = Context.EmitCode(StructType, TEXT("%_Set%((%)0, %)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldValue);
	}
	else
	{
		FEmitShaderCode* StructValue = StructExpression->GetValueShader(Context, RequestedStructType);
		OutResult.Code = Context.EmitCode(StructType, TEXT("%_Set%(%, %)"),
			StructType->Name,
			Field->Name,
			StructValue,
			FieldValue);
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

bool FExpressionSelect::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
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
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	return OutResult.SetType(Context, RequestedType, MergePreparedTypes(LhsType, RhsType));
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderCode* TrueValue = TrueExpression->GetValueShader(Context, RequestedType);
	FEmitShaderCode* FalseValue = FalseExpression->GetValueShader(Context, RequestedType);

	OutResult.Code = Context.EmitCode(RequestedType.GetType(), TEXT("(% ? % : %)"),
		ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1),
		TrueValue,
		FalseValue);
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	check(false); // TODO
}

FExpression* FTree::NewUnaryOp(EUnaryOp Op, FExpression* Input)
{
	return NewExpression<FExpressionUnaryOp>(Op, Input);
}

void FExpressionUnaryOp::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives InputDerivatives = Tree.GetAnalyticDerivatives(Input);
	if (InputDerivatives.IsValid())
	{
		switch (Op)
		{
		case EUnaryOp::Neg:
			OutResult.ExpressionDdx = Tree.NewNeg(InputDerivatives.ExpressionDdx);
			OutResult.ExpressionDdy = Tree.NewNeg(InputDerivatives.ExpressionDdy);
			break;
		case EUnaryOp::Rcp:
		{
			FExpression* Result = Tree.NewRcp(Input);
			FExpression* dFdA = Tree.NewNeg(Tree.NewMul(Result, Result));
			OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives.ExpressionDdx);
			OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives.ExpressionDdy);
			break;
		}
		default:
			checkNoEntry();
			break;
		}
	}
}

bool FExpressionUnaryOp::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& InputType = PrepareExpressionValue(Context, Input, RequestedType);
	if (InputType.IsVoid())
	{
		return false;
	}

	if (!InputType.IsNumeric())
	{
		return Context.Errors->AddError(TEXT("Invalid operation on non-numeric type"));
	}

	const FUnaryOpDescription OpDesc = GetUnaryOpDesription(Op);
	FPreparedType ResultType = InputType;
	if (OpDesc.PreshaderOpcode == Shader::EPreshaderOpcode::Nop)
	{
		// No preshader support
		ResultType.SetEvaluation(EExpressionEvaluation::Shader);
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionUnaryOp::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderCode* InputValue = Input->GetValueShader(Context, RequestedType);
	Shader::FType ResultType = InputValue->Type;
	const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(ResultType);

	switch (Op)
	{
	case EUnaryOp::Neg:
		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCNegate(%)"), InputValue);
		}
		else
		{
			OutResult.Code = Context.EmitInlineCode(ResultType, TEXT("(-%)"), InputValue);
		}
		break;
	case EUnaryOp::Rcp:
		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			ResultType = Shader::MakeValueType(Shader::EValueComponentType::Float, ResultType.GetNumComponents());
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCRcp(%)"), InputValue);
		}
		else
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("rcp(%)"), InputValue);
		}
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FExpressionUnaryOp::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FUnaryOpDescription OpDesc = GetUnaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Input->GetValuePreshader(Context, RequestedType, OutPreshader);
	OutPreshader.WriteOpcode(OpDesc.PreshaderOpcode);
}

FExpression* FTree::NewBinaryOp(EBinaryOp Op, FExpression* Lhs, FExpression* Rhs)
{
	return NewExpression<FExpressionBinaryOp>(Op, Lhs, Rhs);
}

void FExpressionBinaryOp::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// Operations with constant derivatives
	switch (Op)
	{
	case EBinaryOp::Less:
		OutResult.ExpressionDdx = Tree.NewConstant(0.0f);
		OutResult.ExpressionDdx = OutResult.ExpressionDdy;
		break;
	default:
		break;
	}

	if (OutResult.IsValid())
	{
		return;
	}

	const FExpressionDerivatives LhsDerivatives = Tree.GetAnalyticDerivatives(Lhs);
	const FExpressionDerivatives RhsDerivatives = Tree.GetAnalyticDerivatives(Rhs);
	if (LhsDerivatives.IsValid() && RhsDerivatives.IsValid())
	{
		switch (Op)
		{
		case EBinaryOp::Add:
			OutResult.ExpressionDdx = Tree.NewAdd(LhsDerivatives.ExpressionDdx, RhsDerivatives.ExpressionDdx);
			OutResult.ExpressionDdy = Tree.NewAdd(LhsDerivatives.ExpressionDdy, RhsDerivatives.ExpressionDdy);
			break;
		case EBinaryOp::Sub:
			OutResult.ExpressionDdx = Tree.NewSub(LhsDerivatives.ExpressionDdx, RhsDerivatives.ExpressionDdx);
			OutResult.ExpressionDdy = Tree.NewSub(LhsDerivatives.ExpressionDdy, RhsDerivatives.ExpressionDdy);
			break;
		case EBinaryOp::Mul:
			OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(LhsDerivatives.ExpressionDdx, Rhs), Tree.NewMul(RhsDerivatives.ExpressionDdx, Lhs));
			OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(LhsDerivatives.ExpressionDdy, Rhs), Tree.NewMul(RhsDerivatives.ExpressionDdy, Lhs));
			break;
		case EBinaryOp::Div:
		{
			FExpression* Denom = Tree.NewRcp(Tree.NewMul(Rhs, Rhs));
			FExpression* dFdA = Tree.NewMul(Rhs, Denom);
			FExpression* dFdB = Tree.NewNeg(Tree.NewMul(Lhs, Denom));
			OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(dFdA, LhsDerivatives.ExpressionDdx), Tree.NewMul(dFdB, RhsDerivatives.ExpressionDdx));
			OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(dFdA, LhsDerivatives.ExpressionDdy), Tree.NewMul(dFdB, RhsDerivatives.ExpressionDdy));
			break;
		}
		default:
			checkNoEntry();
			break;
		}
	}
}

bool FExpressionBinaryOp::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = PrepareExpressionValue(Context, Lhs, RequestedType);
	const FPreparedType& RhsType = PrepareExpressionValue(Context, Rhs, RequestedType);
	if (LhsType.IsVoid() || RhsType.IsVoid())
	{
		return false;
	}

	if (!LhsType.IsNumeric() || !RhsType.IsNumeric())
	{
		return Context.Errors->AddError(TEXT("Invalid arithmetic between non-numeric types"));
	}

	if (LhsType.GetNumComponents() != 1 && RhsType.GetNumComponents() != 1 && LhsType.GetNumComponents() != RhsType.GetNumComponents())
	{
		return Context.Errors->AddErrorf(TEXT("Invalid arithmetic between %s and %s"), LhsType.GetType().GetName(), RhsType.GetType().GetName());
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
		ResultType.SetEvaluation(EExpressionEvaluation::Shader);
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
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

void FExpressionBinaryOp::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FRequestedType InputType = Private::GetBinaryOpInputType(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const Shader::EValueComponentType InputComponentType = InputType.ValueComponentType;
	Shader::FType ResultType = InputType.GetType();
	FEmitShaderCode* LhsValue = Lhs->GetValueShader(Context, InputType);
	FEmitShaderCode* RhsValue = Rhs->GetValueShader(Context, InputType);

	switch (Op)
	{
	case EBinaryOp::Add:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCAdd(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("(% + %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Sub:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCSubtract(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("(% - %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Mul:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCMultiply(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("(% * %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Div:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCDivide(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("(% / %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Less:
		ResultType = Shader::MakeValueType(Shader::EValueComponentType::Bool, ResultType.GetNumComponents());
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("LWCLess(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitCode(ResultType, TEXT("(% < %)"), LhsValue, RhsValue);
		}
		break;
	default:
		checkNoEntry();
		break;
	}
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

void FExpressionSwizzle::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives InputDerivatives = Tree.GetAnalyticDerivatives(Input);
	if (InputDerivatives.IsValid())
	{
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSwizzle>(Parameters, InputDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSwizzle>(Parameters, InputDerivatives.ExpressionDdy);
	}
}

bool FExpressionSwizzle::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	const FPreparedType& InputType = PrepareExpressionValue(Context, Input, RequestedInputType);

	FPreparedType ResultType(InputType.ValueComponentType);
	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
		ResultType.SetComponentEvaluation(ComponentIndex, InputType.GetComponentEvaluation(SwizzledComponentIndex));
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
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

	FEmitShaderCode* InputValue = Input->GetValueShader(Context, RequestedInputType);
	if (bHasSwizzleReorder)
	{
		OutResult.Code = Context.EmitInlineCode(RequestedType.GetType(), TEXT("%.%"),
			InputValue,
			Swizzle);
	}
	else
	{
		OutResult.Code = InputValue;
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

bool FExpressionAppend::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = PrepareExpressionValue(Context, Lhs, RequestedType);
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min(LhsType.GetNumComponents(), NumRequestedComponents);

	FRequestedType RhsRequestedType;
	for (int32 Index = NumLhsComponents; Index < NumRequestedComponents; ++Index)
	{
		RhsRequestedType.SetComponentRequest(Index - NumLhsComponents, RequestedType.IsComponentRequested(Index));
	}

	const FPreparedType& RhsType = PrepareExpressionValue(Context, Rhs, RhsRequestedType);
	const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);

	if (LhsType.ValueComponentType != RhsType.ValueComponentType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponentEvaluation(Index, LhsType.GetComponentEvaluation(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		ResultType.SetComponentEvaluation(NumLhsComponents + Index, LhsType.GetComponentEvaluation(Index));
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

namespace Private
{
struct FAppendTypes
{
	Shader::FType ResultType;
	Shader::EValueComponentType ResultComponentType;
	FRequestedType LhsType;
	FRequestedType RhsType;
};
FAppendTypes GetAppendTypes(const FRequestedType& RequestedType, const FRequestedType& LhsType, const FRequestedType& RhsType)
{
	const int32 NumResultComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min<int32>(NumResultComponents, LhsType.GetNumComponents());
	const int32 NumRhsComponents = FMath::Min<int32>(NumResultComponents - NumLhsComponents, RhsType.GetNumComponents());

	FAppendTypes Types;
	Types.ResultComponentType = RequestedType.ValueComponentType;
	Types.ResultType = Shader::MakeValueType(RequestedType.ValueComponentType, NumLhsComponents + NumRhsComponents);
	Types.LhsType.ValueComponentType = RequestedType.ValueComponentType;
	Types.RhsType.ValueComponentType = RequestedType.ValueComponentType;
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		Types.LhsType.SetComponentRequest(Index, RequestedType.IsComponentRequested(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		Types.RhsType.SetComponentRequest(Index, RequestedType.IsComponentRequested(NumLhsComponents + Index));
	}
	return Types;
}
} // namespace Private

void FExpressionAppend::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	FEmitShaderCode* LhsValue = Lhs->GetValueShader(Context, Types.LhsType);

	if (Types.RhsType.IsVoid())
	{
		OutResult.Code = LhsValue;
	}
	else
	{
		FEmitShaderCode* RhsValue = Rhs->GetValueShader(Context, Types.RhsType);
		if (Types.ResultComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitInlineCode(Types.ResultType, TEXT("MakeLWCVector(%, %)"),
				LhsValue,
				RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitInlineCode(Types.ResultType, TEXT("%(%, %)"),
				Types.ResultType.GetName(),
				LhsValue,
				RhsValue);
		}
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

bool FExpressionReflectionVector::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.bReadMaterialNormal = true;
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float3);
}

void FExpressionReflectionVector::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
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
	ParentScope->EmitStatementf(Context, TEXT("return %s;"), Expression->GetValueShader(Context)->Reference);
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
			FEmitShaderCode* ConditionValue = ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1);
			ParentScope->EmitNestedScopef(Context, ThenScope, TEXT("if (%s)"), ConditionValue->Reference);
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
