// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"

#if WITH_EDITOR

#include "MaterialHLSLGenerator.h"
#include "MaterialHLSLTree.h"
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeCommon.h"

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionStaticBool.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionGetLocal.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionSetLocal.h"
#include "Materials/MaterialExpressionIfThenElse.h"
#include "Materials/MaterialExpressionForLoop.h"
#include "Materials/MaterialFunctionInterface.h"

EMaterialGenerateHLSLStatus UMaterialExpression::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	return Generator.Error(TEXT("Node does not support expressions"));
}

EMaterialGenerateHLSLStatus UMaterialExpression::GenerateHLSLStatement(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FStatement*& OutStatement)
{
	return Generator.Error(TEXT("Node does not support statements"));
}

EMaterialGenerateHLSLStatus UMaterialExpression::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	return Generator.Error(TEXT("Node does not support textures"));
}

FMaterialHLSLTree& UMaterialFunctionInterface::AcquireHLSLTree(FMaterialHLSLGenerator& Generator)
{
	if (!CachedHLSLTree)
	{
		CachedHLSLTree = new FMaterialHLSLTree();
		CachedHLSLTree->InitializeForFunction(Generator.GetCompileTarget(), this);
	}
	return *CachedHLSLTree;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(Scope, R);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant2Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(Scope, UE::Shader::FValue(R, G));
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant3Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(Scope, UE::Shader::FValue(Constant.R, Constant.G, Constant.B));
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant4Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(Scope, UE::Shader::FValue(Constant.R, Constant.G, Constant.B, Constant.A));
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticBool::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(Scope, (bool)Value);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticSwitch::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ConditionExpression = Value.GetTracedInput().Expression ? Value.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(Scope, (bool)DefaultValue);
	UE::HLSLTree::FExpression* TrueExpression = A.AcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* FalseExpression = B.AcquireHLSLExpression(Generator, Scope);

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSelect>(Scope, ConditionExpression, TrueExpression, FalseExpression);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionGetLocal::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FLocalDeclaration* Declaration = Generator.AcquireLocalDeclaration(Scope, UE::Shader::EValueType::Float3, LocalName);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionLocalVariable>(Scope, Declaration);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionVectorParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FParameterDeclaration* Declaration = Generator.AcquireParameterDeclaration(Scope, ParameterName, DefaultValue);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionParameter>(Scope, Declaration);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionScalarParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FParameterDeclaration* Declaration = Generator.AcquireParameterDeclaration(Scope, ParameterName, DefaultValue);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionParameter>(Scope, Declaration);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticBoolParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FParameterDeclaration* Declaration = Generator.AcquireParameterDeclaration(Scope, ParameterName, (bool)DefaultValue);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionParameter>(Scope, Declaration);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureObject::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
	OutTexture = Generator.AcquireTextureDeclaration(Scope, TextureDesc);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureObjectParameter::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
	OutTexture = Generator.AcquireTextureParameterDeclaration(Scope, ParameterName, TextureDesc);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureSample::GenerateHLSLExpressionBase(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration, UE::HLSLTree::FExpression*& OutExpression)
{
	if (!TextureDeclaration)
	{
		return Generator.Error(TEXT("Missing input texture"));
	}

	UE::HLSLTree::FExpression* TexCoordExpression = Coordinates.GetTracedInput().Expression ? Coordinates.AcquireHLSLExpression(Generator, Scope) : Generator.NewTexCoord(Scope, ConstCoordinate);
	UE::HLSLTree::FExpressionTextureSample* ExpressionTextureSample = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionTextureSample>(Scope, TextureDeclaration, TexCoordExpression);
	ExpressionTextureSample->SamplerSource = SamplerSource;
	ExpressionTextureSample->MipValueMode = MipValueMode;
	OutExpression = ExpressionTextureSample;
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureSample::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration = nullptr;
	if (TextureObject.GetTracedInput().Expression)
	{
		TextureDeclaration = TextureObject.AcquireHLSLTexture(Generator, Scope);
	}
	else if (Texture)
	{
		const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
		TextureDeclaration = Generator.AcquireTextureDeclaration(Scope, TextureDesc);
	}

	return GenerateHLSLExpressionBase(Generator, Scope, TextureDeclaration, OutExpression);
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureSampleParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration = nullptr;
	if (ParameterName.IsNone() && TextureObject.GetTracedInput().Expression)
	{
		TextureDeclaration = TextureObject.AcquireHLSLTexture(Generator, Scope);
	}
	else if (Texture)
	{
		const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
		TextureDeclaration = Generator.AcquireTextureParameterDeclaration(Scope, ParameterName, TextureDesc);
	}

	return GenerateHLSLExpressionBase(Generator, Scope, TextureDeclaration, OutExpression);
}

EMaterialGenerateHLSLStatus UMaterialExpressionAdd::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.GetTracedInput().Expression ? A.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.GetTracedInput().Expression ? B.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}

	/*FString ErrorMessage;
	const UE::Shader::EValueType ResultType = UE::HLSLTree::MakeArithmeticResultType(Lhs->Type, Rhs->Type, ErrorMessage);
	if (ResultType == UE::Shader::EValueType::Void)
	{
		return Generator.Errorf(TEXT("%s"), *ErrorMessage);
	}*/

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(Scope, UE::HLSLTree::EBinaryOp::Add, Lhs, Rhs);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionSetMaterialAttributes::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* AttributesExpression = nullptr;
	if (Inputs[0].GetTracedInput().Expression)
	{
		AttributesExpression = Inputs[0].AcquireHLSLExpression(Generator, Scope);
	}
	else
	{
		AttributesExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionDefaultMaterialAttributes>(Scope);
	}

	for (int32 PinIndex = 0; PinIndex < AttributeSetTypes.Num(); ++PinIndex)
	{
		const FExpressionInput& AttributeInput = Inputs[PinIndex + 1];
		if (AttributeInput.GetTracedInput().Expression)
		{
			const FGuid& AttributeID = AttributeSetTypes[PinIndex];
			// Only compile code to set attributes of the current shader frequency
			const EShaderFrequency AttributeFrequency = FMaterialAttributeDefinitionMap::GetShaderFrequency(AttributeID);
			//if (AttributeFrequency == Compiler->GetCurrentShaderFrequency())
			{
				UE::HLSLTree::FExpression* ValueExpression = AttributeInput.AcquireHLSLExpression(Generator, Scope);
				if (ValueExpression)
				{
					UE::HLSLTree::FExpressionSetMaterialAttribute* SetAttributeExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSetMaterialAttribute>(Scope);
					SetAttributeExpression->AttributeID = AttributeID;
					SetAttributeExpression->AttributesExpression = AttributesExpression;
					SetAttributeExpression->ValueExpression = ValueExpression;
					AttributesExpression = SetAttributeExpression;
				}
			}
		}
	}

	OutExpression = AttributesExpression;
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionReflectionVectorWS::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	check(!CustomWorldNormal.GetTracedInput().Expression); // TODO

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionReflectionVector>(Scope);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionFunctionInput::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::Shader::EValueType ExpressionType = UE::Shader::EValueType::Void;
	switch (InputType)
	{
	case FunctionInput_Scalar: ExpressionType = UE::Shader::EValueType::Float1; break;
	case FunctionInput_Vector2: ExpressionType = UE::Shader::EValueType::Float2; break;
	case FunctionInput_Vector3: ExpressionType = UE::Shader::EValueType::Float3; break;
	case FunctionInput_Vector4: ExpressionType = UE::Shader::EValueType::Float4; break;
	case FunctionInput_Texture2D:
	case FunctionInput_TextureCube:
	case FunctionInput_Texture2DArray:
	case FunctionInput_VolumeTexture:
	case FunctionInput_StaticBool:
	case FunctionInput_MaterialAttributes:
	case FunctionInput_TextureExternal:
		break;
	}
	if (ExpressionType == UE::Shader::EValueType::Void)
	{
		return Generator.Error(TEXT("Invalid input connection"));
	}

	UE::HLSLTree::FLocalDeclaration* Declaration = Generator.AcquireLocalDeclaration(Scope, ExpressionType, InputName);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionLocalVariable>(Scope, Declaration);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionMaterialFunctionCall::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	if (!MaterialFunction)
	{
		return Generator.Error(TEXT("Missing function"));
	}

	TArray<UE::HLSLTree::FExpression*> InputExpressions;
	InputExpressions.Empty(FunctionInputs.Num());

	for (int32 InputIndex = 0; InputIndex < FunctionInputs.Num(); ++InputIndex)
	{
		const FFunctionExpressionInput& Input = FunctionInputs[InputIndex];
		UE::HLSLTree::FExpression* InputExpression = Input.Input.AcquireHLSLExpression(Generator, Scope);
		if (!InputExpression)
		{
			InputExpression = Generator.AcquireExpression(Scope, Input.ExpressionInput, 0);
		}
		check(InputExpression);
		InputExpressions.Add(InputExpression);
	}

	UE::HLSLTree::FFunctionCall* FunctionCall = Generator.AcquireFunctionCall(Scope, MaterialFunction, InputExpressions);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionFunctionOutput>(Scope, FunctionCall, OutputIndex);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionExecBegin::GenerateHLSLStatement(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FStatement*& OutStatement)
{
	OutStatement = Exec.AcquireHLSLStatement(Generator, Scope);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionExecEnd::GenerateHLSLStatement(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FStatement*& OutStatement)
{
	OutStatement = Generator.NewResult(Scope);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionSetLocal::GenerateHLSLStatement(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FStatement*& OutStatement)
{
	UE::HLSLTree::FExpression* ValueExpression = Value.AcquireHLSLExpressionWithCast(Generator, Scope, UE::Shader::EValueType::Float3);
	if (!ValueExpression)
	{
		return Generator.Error(TEXT("Missing value connection"));
	}

	UE::HLSLTree::FLocalDeclaration* Declaration = Generator.AcquireLocalDeclaration(Scope, UE::Shader::EValueType::Float3, LocalName);
	if (!Declaration)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}

	UE::HLSLTree::FStatementSetLocalVariable* Statement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementSetLocalVariable>(Scope);
	Statement->Declaration = Declaration;
	Statement->Expression = ValueExpression;

	Exec.AcquireHLSLStatement(Generator, Scope);

	OutStatement = Statement;
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionIfThenElse::GenerateHLSLStatement(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FStatement*& OutStatement)
{
	UE::HLSLTree::FExpression* ConditionExpression = Condition.AcquireHLSLExpression(Generator, Scope);
	if (!ConditionExpression)
	{
		return Generator.Error(TEXT("Missing condition connection"));
	}

	UE::HLSLTree::FScope* ThenScope = Then.NewScopeWithStatement(Generator, Scope);
	if (!ThenScope)
	{
		return Generator.Error(TEXT("Missing Then connection"));
	}

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(Scope);
	IfStatement->ConditionExpression = ConditionExpression;
	IfStatement->ThenScope = ThenScope;
	IfStatement->ElseScope = Else.NewLinkedScopeWithStatement(Generator, *ThenScope);

	OutStatement = IfStatement;
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionForLoop::GenerateHLSLStatement(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FStatement*& OutStatement)
{
	UE::HLSLTree::FExpression* StartExpression = StartIndex.AcquireHLSLExpression(Generator, Scope);
	if (!StartExpression)
	{
		return Generator.Error(TEXT("Missing StartIndex connection"));
	}

	UE::HLSLTree::FExpression* EndExpression = EndIndex.AcquireHLSLExpression(Generator, Scope);
	if (!EndExpression)
	{
		return Generator.Error(TEXT("Missing EndIndex connection"));
	}

	UE::HLSLTree::FScope* LoopScope = LoopBody.NewScopeWithStatement(Generator, Scope);
	if (!LoopScope)
	{
		return Generator.Error(TEXT("Missing LoopBody connection"));
	}

	UE::HLSLTree::FStatementFor* ForStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementFor>(Scope);
	ForStatement->StartExpression = StartExpression;
	ForStatement->EndExpression = EndExpression;
	ForStatement->LoopScope = LoopScope;

	Completed.AcquireHLSLStatement(Generator, Scope);

	OutStatement = ForStatement;
	return EMaterialGenerateHLSLStatus::Success;
}

#endif // WITH_EDITOR
