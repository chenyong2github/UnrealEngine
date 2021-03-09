// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"

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


void UE::HLSLTree::FExpressionConstant::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.EvaluationType = EExpressionEvaluationType::Constant;
	OutResult.Preshader.WriteOpcode(EMaterialPreshaderOpcode::Constant);
	OutResult.Preshader.Write(Value.ToLinearColor());
}

void UE::HLSLTree::FExpressionLocalVariable::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue&DeclarationRef = Context.AcquireValue(Declaration);
	
	OutResult.bInline = true;
	OutResult.Writer.Writef(TEXT("%s"), Context.GetCode(DeclarationRef));
}

void UE::HLSLTree::FExpressionParameter::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.EvaluationType = EExpressionEvaluationType::Preshader;

	EMaterialPreshaderOpcode Opcode = EMaterialPreshaderOpcode::Nop;
	int32 ParameterIndex = INDEX_NONE;
	if (Declaration->DefaultValue.Type == UE::HLSLTree::EExpressionType::Float1)
	{
		ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddScalarParameter(Declaration->Name, Declaration->DefaultValue.Float[0]);
		Opcode = EMaterialPreshaderOpcode::ScalarParameter;
	}
	else
	{
		ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddVectorParameter(Declaration->Name, Declaration->DefaultValue.ToLinearColor());
		Opcode = EMaterialPreshaderOpcode::VectorParameter;
	}

	check(ParameterIndex >= 0 && ParameterIndex <= 0xffff);
	OutResult.Preshader.WriteOpcode(Opcode);
	OutResult.Preshader.Write((uint16)ParameterIndex);
}

void UE::HLSLTree::FExpressionExternalInput::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const int32 TypeIndex = (int32)InputType;
	const int32 TexCoordIndex = TypeIndex - (int32)UE::HLSLTree::EExternalInputType::TexCoord0;

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
}

void UE::HLSLTree::FExpressionTextureSample::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
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

	const FEmitValue& TexCoordRef = Context.AcquireValue(TexCoordExpression);
	OutResult.Writer.Writef(TEXT("%s(%s(%s, %s, %s))"), SamplerTypeFunction, SampleFunctionName, *TextureName, *SamplerStateCode, Context.GetCode(TexCoordRef));
}

void UE::HLSLTree::FExpressionDefaultMaterialAttributes::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	OutResult.bInline = true;
	OutResult.Writer.Write(TEXT("DefaultMaterialAttributes"));
}

void UE::HLSLTree::FExpressionSetMaterialAttribute::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const EMaterialProperty Property = FMaterialAttributeDefinitionMap::GetProperty(AttributeID);
	const FString PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
	const EMaterialValueType PropertyType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
	const EShaderFrequency Frequency = FMaterialAttributeDefinitionMap::GetShaderFrequency(AttributeID);

	const FEmitValue&AttributresRef = Context.AcquireValue(AttributesExpression);
	const FEmitValue&ValueRef = Context.AcquireValue(ValueExpression);

	OutResult.Writer.Writef(TEXT("FMaterialAttributes_Set%s(%s, %s)"), *PropertyName, Context.GetCode(AttributresRef), Context.GetCode(ValueRef));
}

void UE::HLSLTree::FExpressionBinaryOp::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue& LhsRef = Context.AcquireValue(Lhs);
	const FEmitValue& RhsRef = Context.AcquireValue(Rhs);

	OutResult.EvaluationType = CombineEvaluationTypes(LhsRef.GetEvaluationType(), RhsRef.GetEvaluationType());
	if (OutResult.EvaluationType == EExpressionEvaluationType::Shader)
	{
		OutResult.Writer.Writef(TEXT("(%s + %s)"), Context.GetCode(LhsRef), Context.GetCode(RhsRef));
	}
	else
	{
		EMaterialPreshaderOpcode Opcode = EMaterialPreshaderOpcode::Nop;
		switch (Op)
		{
		case UE::HLSLTree::EBinaryOp::Add: Opcode = EMaterialPreshaderOpcode::Add; break;
		default: checkNoEntry(); break;
		}

		Context.AppendPreshader(LhsRef, OutResult.Preshader);
		Context.AppendPreshader(RhsRef, OutResult.Preshader);
		OutResult.Preshader.WriteOpcode(Opcode);
	}
}

void UE::HLSLTree::FExpressionSwizzle::EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	for (int32 i = 0; i < Parameters.NumComponents; ++i)
	{
		const int32 ComponentIndex = Parameters.ComponentIndex[i];
		check(ComponentIndex >= 0 && ComponentIndex < 4);
		Swizzle[i] = ComponentName[ComponentIndex];
	}

	const FEmitValue& InputRef = Context.AcquireValue(Input);

	OutResult.bInline = true;
	OutResult.EvaluationType = InputRef.GetEvaluationType();
	if (OutResult.EvaluationType == EExpressionEvaluationType::Shader)
	{
		OutResult.Writer.Writef(TEXT("%s.%s"), Context.GetCode(InputRef), Swizzle);
	}
	else
	{
		Context.AppendPreshader(InputRef, OutResult.Preshader);
		OutResult.Preshader.WriteOpcode(EMaterialPreshaderOpcode::ComponentSwizzle)
			.Write((uint8)Parameters.NumComponents)
			.Write((uint8)Parameters.ComponentIndex[0])
			.Write((uint8)Parameters.ComponentIndex[1])
			.Write((uint8)Parameters.ComponentIndex[2])
			.Write((uint8)Parameters.ComponentIndex[3]);
	}
}

void UE::HLSLTree::FExpressionCast::EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const EExpressionType InputType = Input->Type;
	const FEmitValue& InputRef = Context.AcquireValue(Input);
	const TCHAR* InputCode = Context.GetCode(InputRef);

	OutResult.bInline = true;
	// TODO - preshader

	if (InputType == Type)
	{
		// No cast required (nop)
		OutResult.Writer.Writef(TEXT("%s"), InputCode);
	}

	const FExpressionTypeDescription OutputTypeDesc = GetExpressionTypeDescription(Type);
	const FExpressionTypeDescription InputTypeDesc = GetExpressionTypeDescription(InputType);
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
			OutResult.Writer.Writef(TEXT("%s(%s"), OutputTypeDesc.Name, InputCode);
			for (int32 i = InputTypeDesc.NumComponents; i < OutputTypeDesc.NumComponents; ++i)
			{
				// TODO - worth making these 0s the appropriate type? 0u, 0.0f, etc
				OutResult.Writer.Write(TEXT(", 0"));
			}
			OutResult.Writer.Write(TEXT(")"));
		}
	}
}

void UE::HLSLTree::FExpressionFunctionInput::EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitContext::FFunctionStackEntry& StackEntry = Context.FunctionStack.Last();

	check(InputIndex >= 0 && InputIndex < StackEntry.FunctionCall->NumInputs);
	FExpression* InputExpression = StackEntry.FunctionCall->Inputs[InputIndex];
	const FEmitValue& Ref = Context.AcquireValue(InputExpression);

	OutResult.bInline = true;
	OutResult.EvaluationType = Ref.GetEvaluationType();
	if (OutResult.EvaluationType == EExpressionEvaluationType::Shader)
	{
		OutResult.Writer.Writef(TEXT("%s"), Context.GetCode(Ref));
	}
	else
	{
		Context.AppendPreshader(Ref, OutResult.Preshader);
	}
}

void UE::HLSLTree::FExpressionFunctionOutput::EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	const FEmitValue& Ref = Context.AcquireValue(FunctionCall, OutputIndex);
	
	OutResult.bInline = true;
	OutResult.EvaluationType = Ref.GetEvaluationType();
	if (OutResult.EvaluationType == EExpressionEvaluationType::Shader)
	{
		OutResult.Writer.Writef(TEXT("%s"), Context.GetCode(Ref));
	}
	else
	{
		Context.AppendPreshader(Ref, OutResult.Preshader);
	}
}

void UE::HLSLTree::FStatementSetFunctionOutput::EmitHLSL(FEmitContext& Context, FCodeWriter& OutWriter) const
{
	const FEmitContext::FFunctionStackEntry& StackEntry = Context.FunctionStack.Last();
	const FEmitValue& ExpressionRef = Context.AcquireValue(Expression);

	check(OutputIndex >= 0 && OutputIndex < StackEntry.FunctionCall->NumOutputs);
	FEmitValue& OutputRef = StackEntry.OutputRef[OutputIndex];
	OutputRef.Assign(ExpressionRef);
	if (OutputRef.GetEvaluationType() == EExpressionEvaluationType::Shader)
	{
		OutWriter.WriteLinef(TEXT("%s = %s;"), Context.GetCode(OutputRef), Context.GetCode(ExpressionRef));
	}
}

void UE::HLSLTree::FStatementReturn::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& OutWriter) const
{
	const FEmitValue& Ref = Context.AcquireValue(Expression);
	OutWriter.WriteLinef(TEXT("return %s;"), Context.GetCode(Ref));
}

void UE::HLSLTree::FStatementSetLocalVariable::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& OutWriter) const
{
	const FEmitValue& DeclarationRef = Context.AcquireValue(Declaration);
	const FEmitValue& ExpressionRef = Context.AcquireValue(Expression);
	OutWriter.WriteLinef(TEXT("%s = %s;"), Context.GetCode(DeclarationRef), Context.GetCode(ExpressionRef));
}

void UE::HLSLTree::FStatementIf::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& OutWriter) const
{
	const FEmitValue& ConditionRef = Context.AcquireValue(ConditionExpression);
	OutWriter.WriteLinef(TEXT("if (%s)"), Context.GetCode(ConditionRef));
	ThenScope->EmitHLSL(Context, OutWriter);
	if (ElseScope)
	{
		OutWriter.WriteLine(TEXT("else"));
		ElseScope->EmitHLSL(Context, OutWriter);
	}
}

void UE::HLSLTree::FStatementFor::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& OutWriter) const
{
	
}
