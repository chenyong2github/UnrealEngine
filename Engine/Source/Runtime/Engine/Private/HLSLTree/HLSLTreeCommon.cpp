// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

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


bool UE::HLSLTree::FExpressionConstant::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.EvaluationType = EExpressionEvaluationType::Constant;
	OutResult.Type = Value.GetType();
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Constant);
	OutResult.Preshader.Write(Value);
	return true;
}

bool UE::HLSLTree::FExpressionMaterialParameter::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.Type = GetShaderValueType(ParameterType);
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
		OutResult.EvaluationType = EExpressionEvaluationType::Constant;
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Shader::FValue(bValue));
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
		OutResult.EvaluationType = EExpressionEvaluationType::Preshader;
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Parameter).Write((uint16)ParameterIndex); 
	}
	return true;
}

bool UE::HLSLTree::FExpressionExternalInput::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const int32 TypeIndex = (int32)InputType;
	const int32 TexCoordIndex = TypeIndex - (int32)UE::HLSLTree::EExternalInputType::TexCoord0;

	OutResult.EvaluationType = EExpressionEvaluationType::Shader;
	OutResult.Type = GetInputExpressionType(InputType);
	OutResult.bInline = true;
	if (TexCoordIndex >= 0 && TexCoordIndex < 8)
	{
		Context.NumTexCoords = FMath::Max(Context.NumTexCoords, TexCoordIndex + 1);
		OutResult.Writer.Writef(TEXT("Parameters.TexCoords[%u].xy"), TexCoordIndex);
	}
	else
	{
		checkNoEntry();
	}
	return true;
}

bool UE::HLSLTree::FExpressionTextureSample::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* TexCoordValue = Context.AcquireValue(TexCoordExpression);
	if (!TexCoordExpression)
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

	OutResult.EvaluationType = EExpressionEvaluationType::Shader;
	OutResult.Type = Shader::EValueType::Float4;
	OutResult.Writer.Writef(TEXT("%s(%s(%s, %s, %s))"), SamplerTypeFunction, SampleFunctionName, *TextureName, *SamplerStateCode, Context.GetCode(TexCoordValue));
	return true;
}

bool UE::HLSLTree::FExpressionDefaultMaterialAttributes::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.EvaluationType = EExpressionEvaluationType::Shader;
	OutResult.Type = Shader::EValueType::MaterialAttributes;
	OutResult.bInline = true;
	OutResult.Writer.Write(TEXT("DefaultMaterialAttributes"));
	return true;
}

bool UE::HLSLTree::FExpressionSetMaterialAttribute::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* AttributresValue = Context.AcquireValue(AttributesExpression);
	const FEmitValue* Value = Context.AcquireValue(ValueExpression);
	if (!AttributresValue || !Value)
	{
		return false;
	}

	const FString PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
	OutResult.EvaluationType = EExpressionEvaluationType::Shader;
	OutResult.Type = Shader::EValueType::MaterialAttributes;
	OutResult.Writer.Writef(TEXT("FMaterialAttributes_Set%s(%s, %s)"), *PropertyName, Context.GetCode(AttributresValue), Context.GetCode(Value));
	return true;
}

bool UE::HLSLTree::FExpressionSelect::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* ConditionValue = Context.AcquireValue(ConditionExpression);
	if (!ConditionValue)
	{
		return false;
	}

	if (ConditionValue->GetEvaluationType() == EExpressionEvaluationType::Constant)
	{
		const bool bCondition = ConditionValue->GetConstantValue().AsBoolScalar();
		if (!bCondition)
		{
			const FEmitValue* Value = Context.AcquireValue(FalseExpression);
			OutResult.ForwardValue(Context, Value);
		}
		else
		{
			const FEmitValue* Value = Context.AcquireValue(TrueExpression);
			OutResult.ForwardValue(Context, Value);
		}
	}
	else
	{
		const FEmitValue* TrueValue = Context.AcquireValue(TrueExpression);
		const FEmitValue* FalseValue = Context.AcquireValue(FalseExpression);
		OutResult.EvaluationType = EExpressionEvaluationType::Shader; // TODO - preshader
		OutResult.Type = TrueValue->GetExpressionType(); // TODO - FalseValue?
		OutResult.Writer.Writef(TEXT("(%s ? %s : %s)"),
			Context.GetCode(ConditionValue),
			Context.GetCode(TrueValue),
			Context.GetCode(FalseValue));
	}
	return true;
}

bool UE::HLSLTree::FExpressionBinaryOp::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* LhsValue = Context.AcquireValue(Lhs);
	const FEmitValue* RhsValue = Context.AcquireValue(Rhs);
	if (!LhsValue || !RhsValue)
	{
		return false;
	}

	FString ErrorMessage;
	switch (Op)
	{
	case EBinaryOp::Less:
		OutResult.Type = Shader::MakeComparisonResultType(LhsValue->GetExpressionType(), RhsValue->GetExpressionType(), ErrorMessage);
		break;
	default:
		OutResult.Type = Shader::MakeArithmeticResultType(LhsValue->GetExpressionType(), RhsValue->GetExpressionType(), ErrorMessage);
		break;
	}

	const EExpressionEvaluationType EvaluationType = CombineEvaluationTypes(LhsValue->GetEvaluationType(), RhsValue->GetEvaluationType());
	Shader::EPreshaderOpcode Opcode = Shader::EPreshaderOpcode::Nop;
	if (EvaluationType != EExpressionEvaluationType::Shader)
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
		OutResult.EvaluationType = EExpressionEvaluationType::Shader;
		const TCHAR* LhsCode = Context.GetCode(LhsValue);
		const TCHAR* RhsCode = Context.GetCode(RhsValue);
		switch (Op)
		{
		case EBinaryOp::Add: OutResult.Writer.Writef(TEXT("(%s + %s)"), LhsCode, RhsCode); break;
		case EBinaryOp::Mul: OutResult.Writer.Writef(TEXT("(%s * %s)"), LhsCode, RhsCode); break;
		case EBinaryOp::Less: OutResult.Writer.Writef(TEXT("(%s < %s)"), LhsCode, RhsCode); break;
		default: checkNoEntry(); break;
		}
	}
	else
	{
		Context.AppendPreshader(LhsValue, OutResult.Preshader);
		Context.AppendPreshader(RhsValue, OutResult.Preshader);
		OutResult.EvaluationType = EvaluationType;
		OutResult.Preshader.WriteOpcode(Opcode);
	}
	return true;
}

bool UE::HLSLTree::FExpressionSwizzle::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* InputValue = Context.AcquireValue(Input);
	if (!InputValue)
	{
		return false;
	}

	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	for (int32 i = 0; i < Parameters.NumComponents; ++i)
	{
		const int32 ComponentIndex = Parameters.ComponentIndex[i];
		check(ComponentIndex >= 0 && ComponentIndex < 4);
		Swizzle[i] = ComponentName[ComponentIndex];
	}

	OutResult.bInline = true;
	OutResult.EvaluationType = InputValue->GetEvaluationType();
	OutResult.Type = Shader::MakeValueType(InputValue->GetExpressionType(), Parameters.NumComponents);
	if (OutResult.EvaluationType == EExpressionEvaluationType::Shader)
	{
		OutResult.Writer.Writef(TEXT("%s.%s"), Context.GetCode(InputValue), Swizzle);
	}
	else
	{
		Context.AppendPreshader(InputValue, OutResult.Preshader);
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ComponentSwizzle)
			.Write((uint8)Parameters.NumComponents)
			.Write((uint8)Parameters.ComponentIndex[0])
			.Write((uint8)Parameters.ComponentIndex[1])
			.Write((uint8)Parameters.ComponentIndex[2])
			.Write((uint8)Parameters.ComponentIndex[3]);
	}
	return true;
}

bool UE::HLSLTree::FExpressionAppend::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* LhsValue = Context.AcquireValue(Lhs);
	const FEmitValue* RhsValue = Context.AcquireValue(Rhs);
	if (!LhsValue || !RhsValue)
	{
		return false;
	}

	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsValue->GetExpressionType());
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsValue->GetExpressionType());
	if (LhsTypeDesc.ComponentType != RhsTypeDesc.ComponentType)
	{
		return false;
	}

	const int32 NumComponents = LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents;
	if (NumComponents > 4)
	{
		return false;
	}

	OutResult.EvaluationType = CombineEvaluationTypes(LhsValue->GetEvaluationType(), RhsValue->GetEvaluationType());
	OutResult.Type = Shader::MakeValueType(LhsTypeDesc.ComponentType, NumComponents);
	if (OutResult.EvaluationType == EExpressionEvaluationType::Shader)
	{
		const Shader::FValueTypeDescription ResultTypeDesc = Shader::GetValueTypeDescription(OutResult.Type);
		OutResult.Writer.Writef(TEXT("%s(%s, %s)"), ResultTypeDesc.Name, Context.GetCode(LhsValue), Context.GetCode(RhsValue));
	}
	else
	{
		Context.AppendPreshader(LhsValue, OutResult.Preshader);
		Context.AppendPreshader(RhsValue, OutResult.Preshader);
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
	}
	return true;
}

bool UE::HLSLTree::FExpressionCast::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* InputValue = Context.AcquireValue(Input);
	if (!InputValue)
	{
		return false;
	}

	const Shader::EValueType InputType = InputValue->GetExpressionType();
	if (InputType == Type)
	{
		// No cast required (nop)
		OutResult.ForwardValue(Context, InputValue);
		return true;
	}

	OutResult.bInline = true;
	OutResult.Type = Type;
	OutResult.EvaluationType = EExpressionEvaluationType::Shader; // TODO - preshader

	const TCHAR* InputCode = Context.GetCode(InputValue);
	const Shader::FValueTypeDescription OutputTypeDesc = Shader::GetValueTypeDescription(Type);
	const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputType);
	if (InputTypeDesc.NumComponents == OutputTypeDesc.NumComponents)
	{
		// Cast between different underlying types with the same number of components, use a C-style cast
		OutResult.Writer.Writef(TEXT("((%s)%s)"), OutputTypeDesc.Name, InputCode);
	}
	else if (OutputTypeDesc.NumComponents < InputTypeDesc.NumComponents)
	{
		static const TCHAR* SwizzleName[] = { TEXT("x"), TEXT("xy"), TEXT("xyz"), TEXT("xyzw") };
		// Masking off some input components, use a swizzle
		if (OutputTypeDesc.ComponentType == InputTypeDesc.ComponentType)
		{
			// underlying types are the same, no C-style cast needed
			OutResult.Writer.Writef(TEXT("%s.%s"), InputCode, SwizzleName[OutputTypeDesc.NumComponents]);
		}
		else
		{
			// Different underlying types, add a C-style cast as well
			OutResult.Writer.Writef(TEXT("((%s)%s.%s)"), OutputTypeDesc.Name, InputCode, SwizzleName[OutputTypeDesc.NumComponents]);
		}
	}
	else
	{
		// Padding out components
		if (InputTypeDesc.NumComponents == 1 && EnumHasAllFlags(Flags, ECastFlags::ReplicateScalar))
		{
			// Replicating a scalar value, use a swizzle
			static const TCHAR* ScalarSwizzleName[] = { TEXT("x"), TEXT("xx"), TEXT("xxx"), TEXT("xxxx") };
			OutResult.Writer.Writef(TEXT("%s.%s"), InputCode, ScalarSwizzleName[OutputTypeDesc.NumComponents]);
		}
		else
		{
			// Padding with 0s, use a constructor
			const Shader::FValue ZeroValue(OutputTypeDesc.ComponentType, 1);
			OutResult.Writer.Writef(TEXT("%s(%s"), OutputTypeDesc.Name, InputCode);
			for (int32 i = InputTypeDesc.NumComponents; i < OutputTypeDesc.NumComponents; ++i)
			{
				OutResult.Writer.Write(TEXT(", "));
				OutResult.Writer.WriteConstant(ZeroValue);
			}
			OutResult.Writer.Write(TEXT(")"));
		}
	}
	return true;
}

bool UE::HLSLTree::FExpressionReflectionVector::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.bInline = true;
	OutResult.Type = Shader::EValueType::Float3;
	OutResult.EvaluationType = EExpressionEvaluationType::Shader;
	OutResult.Writer.Write(TEXT("Parameters.ReflectionVector"));
	return true;
}

bool UE::HLSLTree::FExpressionFunctionInput::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitContext::FFunctionStackEntry& StackEntry = Context.FunctionStack.Last();

	check(InputIndex >= 0 && InputIndex < StackEntry.FunctionCall->NumInputs);
	FExpression* InputExpression = StackEntry.FunctionCall->Inputs[InputIndex];
	const FEmitValue* Value = Context.AcquireValue(InputExpression);
	OutResult.ForwardValue(Context, Value);
	return true;
}

bool UE::HLSLTree::FExpressionFunctionOutput::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue* Value = Context.AcquireValue(FunctionCall, OutputIndex);
	if (!Value)
	{
		return false;
	}

	OutResult.ForwardValue(Context, Value);
	return true;
}

bool UE::HLSLTree::FStatementBreak::EmitHLSL(FEmitContext& Context) const
{
	Context.WriteScopeTerminatorf(TEXT("break;"));
	return true;
}

bool UE::HLSLTree::FStatementReturn::EmitHLSL(FEmitContext& Context) const
{
	const FEmitValue* Value = Context.AcquireValue(Expression);
	if (!Value)
	{
		return false;
	}

	Context.WriteScopeTerminatorf(TEXT("return %s;"), Context.GetCode(Value));
	return true;
}

bool UE::HLSLTree::FStatementIf::EmitHLSL(FEmitContext& Context) const
{
	const FEmitValue* ConditionValue = Context.AcquireValue(ConditionExpression);
	if (!ConditionValue)
	{
		return false;
	}

	Context.WriteScopef(*ThenScope, TEXT("if (%s)"), Context.GetCode(ConditionValue));
	if (ElseScope)
	{
		Context.WriteScopef(*ElseScope, TEXT("else"));
	}
	if (NextScope)
	{
		Context.WriteScope(*NextScope);
	}
	return true;
}

bool UE::HLSLTree::FStatementLoop::EmitHLSL(FEmitContext& Context) const
{
	Context.WriteScopef(*LoopScope, TEXT("while (true)"));
	return Context.WriteScope(*NextScope);
}
