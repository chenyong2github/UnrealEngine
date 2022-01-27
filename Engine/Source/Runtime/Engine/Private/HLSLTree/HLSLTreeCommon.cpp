// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

namespace UE
{
namespace HLSLTree
{

struct FPreshaderLoopScope
{
	const FStatement* BreakStatement = nullptr;
	Shader::FPreshaderLabel BreakLabel;
};

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

bool FExpressionConstant::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Constant, Value.Type);
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	Context.PreshaderStackPosition++;
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

bool FExpressionMaterialParameter::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const EExpressionEvaluation Evaluation = IsStaticMaterialParameter(ParameterType) ? EExpressionEvaluation::Constant : EExpressionEvaluation::Preshader;
	return OutResult.SetType(Context, RequestedType, Evaluation, GetShaderValueType(ParameterType));
}

void FExpressionMaterialParameter::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	Context.PreshaderStackPosition++;
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

bool FExpressionExternalInput::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, InputDesc.Type);
}

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const int32 TypeIndex = (int32)InputType;
	if (IsTexCoord(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0;
		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float2, TEXT("Parameters.TexCoords[%].xy"), TexCoordIndex);
	}
	else if (IsTexCoord_Ddx(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0_Ddx;
		OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDX[%].xy"), TexCoordIndex);
	}
	else if (IsTexCoord_Ddy(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0_Ddy;
		OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDY[%].xy"), TexCoordIndex);
	}
	else
	{
		const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);

		// TODO - handle PrevFrame position
		switch (InputType)
		{
		case EExternalInput::WorldPosition:
			OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, TEXT("GetWorldPosition(Parameters)"));
			break;
		case EExternalInput::WorldPosition_NoOffsets:
			OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, TEXT("GetWorldPosition_NoMaterialOffsets(Parameters)"));
			break;
		case EExternalInput::TranslatedWorldPosition:
			OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, TEXT("GetTranslatedWorldPosition(Parameters)"));
			break;
		case EExternalInput::TranslatedWorldPosition_NoOffsets:
			OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, TEXT("GetTranslatedWorldPosition_NoMaterialOffsets(Parameters)"));
			break;
		case EExternalInput::WorldPosition_Ddx:
			OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, TEXT("Parameters.WorldPosition_DDX"));
			break;
		case EExternalInput::WorldPosition_Ddy:
			OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, TEXT("Parameters.WorldPosition_DDY"));
			break;
		default:
			checkNoEntry();
			break;
		}
	}
}

bool FExpressionTextureSample::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.PrepareExpression(TexCoordExpression, Scope, Shader::EValueType::Float2);
	Context.PrepareExpression(TexCoordDerivatives.ExpressionDdx, Scope, Shader::EValueType::Float2);
	Context.PrepareExpression(TexCoordDerivatives.ExpressionDdy, Scope, Shader::EValueType::Float2);

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
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

	FEmitShaderExpression* TexCoordValue = TexCoordExpression->GetValueShader(Context, Scope, Shader::EValueType::Float2);
	FEmitShaderExpression* TexCoordValueDdx = TexCoordDerivatives.ExpressionDdx->GetValueShader(Context, Scope, Shader::EValueType::Float2);
	FEmitShaderExpression* TexCoordValueDdy = TexCoordDerivatives.ExpressionDdy->GetValueShader(Context, Scope, Shader::EValueType::Float2);
	OutResult.Code = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%(%Grad(%, %, %, %, %))"),
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
		const Shader::FStructField* DerivativeField = DerivativeStructType->FindFieldByName(Field->Name);
		check(DerivativeField);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdy);
	}
}

bool FExpressionGetStructField::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	const FPreparedType& StructPreparedType = Context.PrepareExpression(StructExpression, Scope, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	return OutResult.SetType(Context, RequestedType, StructPreparedType.GetFieldType(Field));
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	FEmitShaderExpression* StructValue = StructExpression->GetValueShader(Context, Scope, RequestedStructType);

	OutResult.Code = Context.EmitInlineExpression(Scope, Field->Type, TEXT("%.%"),
		StructValue,
		Field->Name);
}

void FExpressionGetStructField::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(StructType, false);
	RequestedStructType.SetField(Field, RequestedType);

	StructExpression->GetValuePreshader(Context, Scope, RequestedStructType, OutPreshader);
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
		const Shader::FStructField* DerivativeField = DerivativeStructType->FindFieldByName(Field->Name);
		check(DerivativeField);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdx, FieldDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdy, FieldDerivatives.ExpressionDdy);
	}
}

bool FExpressionSetStructField::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);

	const FPreparedType& StructPreparedType = Context.PrepareExpression(StructExpression, Scope, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	const FPreparedType& FieldPreparedType = Context.PrepareExpression(FieldExpression, Scope, RequestedType.GetField(Field));
	if (FieldPreparedType.IsVoid())
	{
		return OutResult.SetForwardValue(Context, Scope, RequestedType, StructExpression);
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

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(Scope, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(Scope, RequestedFieldType);
	FEmitShaderExpression* FieldValue = FieldExpression->GetValueShader(Context, Scope, RequestedFieldType, Field->Type);

	if (StructEvaluation == EExpressionEvaluation::None)
	{
		check(FieldEvaluation != EExpressionEvaluation::None);

		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%((%)0, %)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldValue);
	}
	else
	{
		FEmitShaderExpression* StructValue = StructExpression->GetValueShader(Context, Scope, RequestedStructType);
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%(%, %)"),
			StructType->Name,
			Field->Name,
			StructValue,
			FieldValue);
	}
}

void FExpressionSetStructField::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(Scope, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(Scope, RequestedFieldType);

	if (StructEvaluation != EExpressionEvaluation::None)
	{
		StructExpression->GetValuePreshader(Context, Scope, RequestedStructType, OutPreshader);
	}
	else
	{
		Context.PreshaderStackPosition++;
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(Shader::FType(StructType));
	}

	FieldExpression->GetValuePreshader(Context, Scope, RequestedFieldType, OutPreshader);

	check(Context.PreshaderStackPosition > 0);
	Context.PreshaderStackPosition--;

	OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::SetField).Write(Field->ComponentIndex).Write(Field->GetNumComponents());
}

bool FExpressionSelect::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& ConditionType = Context.PrepareExpression(ConditionExpression, Scope, Shader::EValueType::Bool1);
	if (ConditionType.GetEvaluation(Scope, Shader::EValueType::Bool1) == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		return OutResult.SetForwardValue(Context, Scope, RequestedType, bCondition ? TrueExpression : FalseExpression);
	}

	const FPreparedType& LhsType = Context.PrepareExpression(FalseExpression, Scope, RequestedType);
	const FPreparedType& RhsType = Context.PrepareExpression(TrueExpression, Scope, RequestedType);
	
	if (LhsType.ValueComponentType != RhsType.ValueComponentType ||
		LhsType.StructType != RhsType.StructType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	return OutResult.SetType(Context, RequestedType, MergePreparedTypes(LhsType, RhsType));
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* TrueValue = TrueExpression->GetValueShader(Context, Scope, RequestedType);
	FEmitShaderExpression* FalseValue = FalseExpression->GetValueShader(Context, Scope, RequestedType);

	OutResult.Code = Context.EmitExpression(Scope, RequestedType.GetType(), TEXT("(% ? % : %)"),
		ConditionExpression->GetValueShader(Context, Scope, Shader::EValueType::Bool1),
		TrueValue,
		FalseValue);
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
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

bool FExpressionUnaryOp::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& InputType = Context.PrepareExpression(Input, Scope, RequestedType);
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

void FExpressionUnaryOp::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* InputValue = Input->GetValueShader(Context, Scope, RequestedType);
	Shader::FType ResultType = InputValue->Type;
	const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(ResultType);

	switch (Op)
	{
	case EUnaryOp::Neg:
		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCNegate(%)"), InputValue);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("(-%)"), InputValue);
		}
		break;
	case EUnaryOp::Rcp:
		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			ResultType = Shader::MakeValueType(Shader::EValueComponentType::Float, ResultType.GetNumComponents());
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCRcp(%)"), InputValue);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("rcp(%)"), InputValue);
		}
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FExpressionUnaryOp::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FUnaryOpDescription OpDesc = GetUnaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Input->GetValuePreshader(Context, Scope, RequestedType, OutPreshader);
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

bool FExpressionBinaryOp::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = Context.PrepareExpression(Lhs, Scope, RequestedType);
	const FPreparedType& RhsType = Context.PrepareExpression(Rhs, Scope, RequestedType);
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

void FExpressionBinaryOp::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FRequestedType InputType = Private::GetBinaryOpInputType(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const Shader::EValueComponentType InputComponentType = InputType.ValueComponentType;
	Shader::FType ResultType = InputType.GetType();
	FEmitShaderExpression* LhsValue = Lhs->GetValueShader(Context, Scope, InputType);
	FEmitShaderExpression* RhsValue = Rhs->GetValueShader(Context, Scope, InputType);

	switch (Op)
	{
	case EBinaryOp::Add:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCAdd(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("(% + %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Sub:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCSubtract(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("(% - %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Mul:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCMultiply(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("(% * %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Div:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCDivide(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("(% / %)"), LhsValue, RhsValue);
		}
		break;
	case EBinaryOp::Less:
		ResultType = Shader::MakeValueType(Shader::EValueComponentType::Bool, ResultType.GetNumComponents());
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("LWCLess(%, %)"), LhsValue, RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, ResultType, TEXT("(% < %)"), LhsValue, RhsValue);
		}
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FExpressionBinaryOp::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FRequestedType InputType = Private::GetBinaryOpInputType(RequestedType, Op, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	const FBinaryOpDescription OpDesc = GetBinaryOpDesription(Op);
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	Lhs->GetValuePreshader(Context, Scope, InputType, OutPreshader);
	Rhs->GetValuePreshader(Context, Scope, InputType, OutPreshader);

	check(Context.PreshaderStackPosition > 0);
	Context.PreshaderStackPosition--;

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

bool FExpressionSwizzle::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	const FPreparedType& InputType = Context.PrepareExpression(Input, Scope, RequestedInputType);

	FPreparedType ResultType(InputType.ValueComponentType);
	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
		ResultType.SetComponent(ComponentIndex, InputType.GetComponent(SwizzledComponentIndex));
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
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

	FEmitShaderExpression* InputValue = Input->GetValueShader(Context, Scope, RequestedInputType);
	if (bHasSwizzleReorder)
	{
		OutResult.Code = Context.EmitInlineExpression(Scope, RequestedType.GetType(), TEXT("%.%"),
			InputValue,
			Swizzle);
	}
	else
	{
		OutResult.Code = InputValue;
	}
}

void FExpressionSwizzle::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);

	Input->GetValuePreshader(Context, Scope, RequestedInputType, OutPreshader);
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

bool FExpressionAppend::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = Context.PrepareExpression(Lhs, Scope, RequestedType);
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min(LhsType.GetNumComponents(), NumRequestedComponents);

	FRequestedType RhsRequestedType;
	for (int32 Index = NumLhsComponents; Index < NumRequestedComponents; ++Index)
	{
		RhsRequestedType.SetComponentRequest(Index - NumLhsComponents, RequestedType.IsComponentRequested(Index));
	}

	const FPreparedType& RhsType = Context.PrepareExpression(Rhs, Scope, RhsRequestedType);
	const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);

	if (LhsType.ValueComponentType != RhsType.ValueComponentType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponent(Index, LhsType.GetComponent(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		ResultType.SetComponent(NumLhsComponents + Index, LhsType.GetComponent(Index));
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

void FExpressionAppend::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	FEmitShaderExpression* LhsValue = Lhs->GetValueShader(Context, Scope, Types.LhsType);

	if (Types.RhsType.IsVoid())
	{
		OutResult.Code = LhsValue;
	}
	else
	{
		FEmitShaderExpression* RhsValue = Rhs->GetValueShader(Context, Scope, Types.RhsType);
		if (Types.ResultComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("MakeLWCVector(%, %)"),
				LhsValue,
				RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("%(%, %)"),
				Types.ResultType.GetName(),
				LhsValue,
				RhsValue);
		}
	}
}

void FExpressionAppend::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetRequestedType(), Rhs->GetRequestedType());
	Lhs->GetValuePreshader(Context, Scope, Types.LhsType, OutPreshader);
	if (Types.RhsType.IsVoid())
	{
		Rhs->GetValuePreshader(Context, Scope, Types.RhsType, OutPreshader);

		check(Context.PreshaderStackPosition > 0);
		Context.PreshaderStackPosition--;

		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
	}
}

bool FExpressionReflectionVector::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.bReadMaterialNormal = true;
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float3);
}

void FExpressionReflectionVector::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float3, TEXT("Parameters.ReflectionVector"));
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
	const FPreparedType& ConditionType = Context.PrepareExpression(ConditionExpression, Scope, Shader::EValueType::Bool1);
	if (ConditionType.IsVoid())
	{
		return false;
	}

	const EExpressionEvaluation ConditionEvaluation = ConditionType.GetEvaluation(Scope, Shader::EValueType::Bool1);
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
	const EExpressionEvaluation ConditionEvaluation = ConditionExpression->GetEvaluation(Scope, Shader::EValueType::Bool1);
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
	ConditionExpression->GetValuePreshader(Context, Scope, Shader::EValueType::Bool1, OutPreshader);

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

} // namespace HLSLTree
} // namespace UE
