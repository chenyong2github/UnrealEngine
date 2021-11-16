// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

static UE::Shader::EValueType GetShaderType(EMaterialValueType MaterialType)
{
	switch (MaterialType)
	{
	case MCT_Float1: return UE::Shader::EValueType::Float1;
	case MCT_Float2: return UE::Shader::EValueType::Float2;
	case MCT_Float3: return UE::Shader::EValueType::Float3;
	case MCT_Float4: return UE::Shader::EValueType::Float4;
	case MCT_Float: return UE::Shader::EValueType::Float1;
	case MCT_StaticBool: return UE::Shader::EValueType::Bool1;
	case MCT_MaterialAttributes: return UE::Shader::EValueType::MaterialAttributes;
	case MCT_ShadingModel: return UE::Shader::EValueType::Int1;
	case MCT_LWCScalar: return UE::Shader::EValueType::Double1;
	case MCT_LWCVector2: return UE::Shader::EValueType::Double2;
	case MCT_LWCVector3: return UE::Shader::EValueType::Double3;
	case MCT_LWCVector4: return UE::Shader::EValueType::Double4;
	default: checkNoEntry(); return UE::Shader::EValueType::Void;
	}
}

UE::HLSLTree::FBinaryOpDescription UE::HLSLTree::GetBinaryOpDesription(EBinaryOp Op)
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

UE::HLSLTree::FSwizzleParameters::FSwizzleParameters(int8 InR, int8 InG, int8 InB, int8 InA) : NumComponents(0)
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

UE::HLSLTree::FSwizzleParameters UE::HLSLTree::MakeSwizzleMask(bool bInR, bool bInG, bool bInB, bool bInA)
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


bool UE::HLSLTree::FExpressionConstant::PrepareValue(FEmitContext& Context)
{
	return SetValueConstant(Context, Value);
}

bool UE::HLSLTree::FExpressionMaterialParameter::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	//return Shader::MakeValueTypeWithRequestedNumComponents(GetShaderValueType(ParameterType), InRequestedNumComponents);
	return SetType(Context, GetShaderValueType(ParameterType));
}

bool UE::HLSLTree::FExpressionMaterialParameter::PrepareValue(FEmitContext& Context)
{
	if (ParameterType == EMaterialParameterType::StaticSwitch)
	{
		const FMaterialParameterInfo ParameterInfo(ParameterName);
		bool bValue = DefaultValue.Component[0].AsBool();
		for (const FStaticSwitchParameter& Parameter : Context.StaticParameters->StaticSwitchParameters)
		{
			if (Parameter.ParameterInfo == ParameterInfo)
			{
				bValue = Parameter.Value;
				break;
			}
		}

		return SetValueConstant(Context, bValue);
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
	return true;
}

bool UE::HLSLTree::FExpressionExternalInput::PrepareValue(FEmitContext& Context)
{
	const int32 TypeIndex = (int32)InputType;
	const int32 TexCoordIndex = TypeIndex - (int32)UE::HLSLTree::EExternalInputType::TexCoord0;

	if (TexCoordIndex >= 0 && TexCoordIndex < 8)
	{
		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		return SetValueInlineShaderf(Context, TEXT("Parameters.TexCoords[%u].xy"), TexCoordIndex);
	}
	else
	{
		return Context.Errors.AddError(this, TEXT("Invalid texcoord"));
	}
	return true;
}

bool UE::HLSLTree::FExpressionTextureSample::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	if (RequestExpressionType(Context, TexCoordExpression, 2) == Shader::EValueType::Void)
	{
		return false;
	}
	return SetType(Context, Shader::EValueType::Float4);
}

bool UE::HLSLTree::FExpressionTextureSample::PrepareValue(FEmitContext& Context)
{
	if (PrepareExpressionValue(Context, TexCoordExpression) == EExpressionEvaluationType::None)
	{
		return false;
	}

	const UE::HLSLTree::FTextureDescription& Desc = Declaration->Description;
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

bool UE::HLSLTree::FExpressionDefaultMaterialAttributes::PrepareValue(FEmitContext& Context)
{
	return SetValueInlineShaderf(Context, TEXT("DefaultMaterialAttributes"));
}

bool UE::HLSLTree::FExpressionSetMaterialAttribute::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	const EMaterialValueType AttributeType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
	const uint32 NumComponents = GetNumComponents(AttributeType);
	if (RequestExpressionType(Context, AttributesExpression, 4) != Shader::EValueType::MaterialAttributes)
	{
		return Context.Errors.AddError(this, TEXT("Expected type MaterialAttributes"));
	}
	if (RequestExpressionType(Context, ValueExpression, NumComponents) == Shader::EValueType::Void)
	{
		return false;
	}
	return SetType(Context, Shader::EValueType::MaterialAttributes);
}

bool UE::HLSLTree::FExpressionSetMaterialAttribute::PrepareValue(FEmitContext& Context)
{
	if (PrepareExpressionValue(Context, AttributesExpression) == EExpressionEvaluationType::None)
	{
		return false;
	}
	if (PrepareExpressionValue(Context, ValueExpression) == EExpressionEvaluationType::None)
	{
		return false;
	}

	const EMaterialValueType AttributeType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
	const FString PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
	return SetValueShaderf(Context, TEXT("FMaterialAttributes_Set%s(%s, %s)"),
		*PropertyName,
		AttributesExpression->GetValueShader(Context, Shader::EValueType::MaterialAttributes),
		ValueExpression->GetValueShader(Context, GetShaderType(AttributeType)));
}

bool UE::HLSLTree::FExpressionSelect::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	const Shader::EValueType ConditionType = RequestExpressionType(Context, ConditionExpression, 1);
	const Shader::EValueType LhsType = RequestExpressionType(Context, FalseExpression, InRequestedNumComponents);
	const Shader::EValueType RhsType = RequestExpressionType(Context, TrueExpression, InRequestedNumComponents);
	if (ConditionType == Shader::EValueType::Void || LhsType == Shader::EValueType::Void || RhsType == Shader::EValueType::Void)
	{
		return false;
	}

	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	const int8 NumComponents = FMath::Max(LhsTypeDesc.NumComponents, RhsTypeDesc.NumComponents);
	return SetType(Context, Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents));
}

bool UE::HLSLTree::FExpressionSelect::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType ConditionEvaluation = PrepareExpressionValue(Context, ConditionExpression);
	if (ConditionEvaluation == EExpressionEvaluationType::None)
	{
		return false;
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
			return false;
		}

		// TODO - preshader
		return SetValueShaderf(Context, TEXT("(%s ? %s : %s)"),
			ConditionExpression->GetValueShader(Context, Shader::EValueType::Bool1),
			TrueExpression->GetValueShader(Context, GetValueType()),
			FalseExpression->GetValueShader(Context, GetValueType()));
	}
	return true;
}

bool UE::HLSLTree::FExpressionBinaryOp::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	const Shader::EValueType LhsType = RequestExpressionType(Context, Lhs, InRequestedNumComponents);
	const Shader::EValueType RhsType = RequestExpressionType(Context, Rhs, InRequestedNumComponents);
	if (LhsType == Shader::EValueType::Void || RhsType == Shader::EValueType::Void)
	{
		return false;
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

bool UE::HLSLTree::FExpressionBinaryOp::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType LhsEvaluation = PrepareExpressionValue(Context, Lhs);
	const EExpressionEvaluationType RhsEvaluation = PrepareExpressionValue(Context, Rhs);
	if (LhsEvaluation == EExpressionEvaluationType::None || RhsEvaluation == EExpressionEvaluationType::None)
	{
		return false;
	}

	const EExpressionEvaluationType Evaluation = CombineEvaluationTypes(LhsEvaluation, RhsEvaluation);
	Shader::EPreshaderOpcode Opcode = Shader::EPreshaderOpcode::Nop;
	if (Evaluation != EExpressionEvaluationType::Shader)
	{
		switch (Op)
		{
		case UE::HLSLTree::EBinaryOp::Add: Opcode = Shader::EPreshaderOpcode::Add; break;
		case UE::HLSLTree::EBinaryOp::Mul: Opcode = Shader::EPreshaderOpcode::Mul; break;
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
		case EBinaryOp::Mul: return SetValueShaderf(Context, TEXT("(%s * %s)"), LhsCode, RhsCode);
		case EBinaryOp::Less: return SetValueShaderf(Context, TEXT("(%s < %s)"), LhsCode, RhsCode);
		default: checkNoEntry(); return false;
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

bool UE::HLSLTree::FExpressionSwizzle::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	const int32 NumSwizzleComponents = FMath::Min<int32>(InRequestedNumComponents, Parameters.NumComponents);
	int32 MaxSwizzleIndex = 0;
	for (int32 i = 0; i < NumSwizzleComponents; ++i)
	{
		MaxSwizzleIndex = FMath::Max<int32>(MaxSwizzleIndex, Parameters.ComponentIndex[i]);
	}

	const Shader::EValueType InputType = RequestExpressionType(Context, Input, MaxSwizzleIndex + 1);
	return SetType(Context, Shader::MakeValueType(InputType, NumSwizzleComponents));
}

bool UE::HLSLTree::FExpressionSwizzle::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType InputEvaluation = PrepareExpressionValue(Context, Input);
	if (InputEvaluation == EExpressionEvaluationType::None)
	{
		return false;
	}

	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(GetValueType());
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
			const Shader::EValueType InputType = Shader::MakeValueType(GetValueType(), NumSwizzleComponents);
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

bool UE::HLSLTree::FExpressionAppend::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	const Shader::EValueType LhsType = RequestExpressionType(Context, Lhs, 4);
	const Shader::EValueType RhsType = RequestExpressionType(Context, Rhs, 4);
	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return Context.Errors.AddError(this, TEXT("Type mismatch"));
	}

	const int32 NumComponents = FMath::Min<int32>(LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents, InRequestedNumComponents);
	return SetType(Context, Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents));
}

bool UE::HLSLTree::FExpressionAppend::PrepareValue(FEmitContext& Context)
{
	const EExpressionEvaluationType LhsEvaluation = PrepareExpressionValue(Context, Lhs);
	const EExpressionEvaluationType RhsEvaluation = PrepareExpressionValue(Context, Rhs);
	if (LhsEvaluation == EExpressionEvaluationType::None || RhsEvaluation == EExpressionEvaluationType::None)
	{
		return false;
	}

	const EExpressionEvaluationType Evaluation = CombineEvaluationTypes(LhsEvaluation, RhsEvaluation);
	if (Evaluation == EExpressionEvaluationType::Shader)
	{
		const Shader::FValueTypeDescription ResultTypeDesc = Shader::GetValueTypeDescription(GetValueType());
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

bool UE::HLSLTree::FExpressionReflectionVector::PrepareValue(FEmitContext& Context)
{
	return SetValueInlineShaderf(Context, TEXT("Parameters.ReflectionVector"));
}

void UE::HLSLTree::FStatementBreak::EmitHLSL(FEmitContext& Context) const
{
	ParentScope->MarkLive();
	ParentScope->EmitStatementf(Context, TEXT("break;"));
}

void UE::HLSLTree::FStatementReturn::RequestTypes(FUpdateTypeContext& Context) const
{
	RequestExpressionType(Context, Expression, 4);
}

void UE::HLSLTree::FStatementReturn::EmitHLSL(FEmitContext& Context) const
{
	if (PrepareExpressionValue(Context, Expression) == EExpressionEvaluationType::None)
	{
		return;
	}

	ParentScope->MarkLiveRecursive();
	ParentScope->EmitStatementf(Context, TEXT("return %s;"), Expression->GetValueShader(Context));
}

void UE::HLSLTree::FStatementIf::RequestTypes(FUpdateTypeContext& Context) const
{
	RequestExpressionType(Context, ConditionExpression, 1);
	RequestScopeTypes(Context, ThenScope);
	RequestScopeTypes(Context, ElseScope);
	RequestScopeTypes(Context, NextScope);
}

void UE::HLSLTree::FStatementIf::EmitHLSL(FEmitContext& Context) const
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

void UE::HLSLTree::FStatementLoop::RequestTypes(FUpdateTypeContext& Context) const
{
	RequestScopeTypes(Context, LoopScope);
	RequestScopeTypes(Context, NextScope);
}

void UE::HLSLTree::FStatementLoop::EmitHLSL(FEmitContext& Context) const
{
	if (LoopScope->IsLive())
	{
		ParentScope->EmitNestedScopef(Context, LoopScope, TEXT("while (true)"));
	}
	ParentScope->EmitNestedScope(Context, NextScope);
}
