// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"

#if WITH_EDITOR

#include "MaterialHLSLGenerator.h"
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeCommon.h"

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"
#include "Materials/MaterialExpressionGenericConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionStaticBool.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionDoubleVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionGetLocal.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionBinaryOp.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionSetLocal.h"
#include "Materials/MaterialExpressionIfThenElse.h"
#include "Materials/MaterialExpressionForLoop.h"
#include "Materials/MaterialExpressionWhileLoop.h"
#include "Materials/MaterialFunctionInterface.h"

EMaterialGenerateHLSLStatus UMaterialExpression::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	return Generator.Error(TEXT("Node does not support expressions"));
}

EMaterialGenerateHLSLStatus UMaterialExpression::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	return Generator.Error(TEXT("Node does not support statements"));
}

EMaterialGenerateHLSLStatus UMaterialExpression::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	return Generator.Error(TEXT("Node does not support textures"));
}

EMaterialGenerateHLSLStatus UMaterialExpressionGenericConstant::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(GetConstantValue());
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(R);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant2Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(UE::Shader::FValue(R, G));
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant3Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(UE::Shader::FValue(Constant.R, Constant.G, Constant.B));
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionConstant4Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(UE::Shader::FValue(Constant.R, Constant.G, Constant.B, Constant.A));
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticBool::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant((bool)Value);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticSwitch::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ConditionExpression = Value.GetTracedInput().Expression ? Value.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant((bool)DefaultValue);
	UE::HLSLTree::FExpression* TrueExpression = A.AcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* FalseExpression = B.AcquireHLSLExpression(Generator, Scope);

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSelect>(ConditionExpression, TrueExpression, FalseExpression);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionGetLocal::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().AcquireLocal(Scope, LocalName);
	if (!OutExpression)
	{
		return Generator.Error(TEXT("Local accessed before assigned"));
	}
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionVectorParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::Vector, ParameterName, DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionDoubleVectorParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::DoubleVector, ParameterName, DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionScalarParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::Scalar, ParameterName, DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticBoolParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::StaticSwitch, ParameterName, (bool)DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}
EMaterialGenerateHLSLStatus UMaterialExpressionWorldPosition::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::EExternalInput InputType = UE::HLSLTree::EExternalInput::None;

	switch (WorldPositionShaderOffset)
	{
	case WPT_Default: InputType = UE::HLSLTree::EExternalInput::WorldPosition; break;
	case WPT_ExcludeAllShaderOffsets: InputType = UE::HLSLTree::EExternalInput::WorldPosition_NoOffsets; break;
	case WPT_CameraRelative: InputType = UE::HLSLTree::EExternalInput::TranslatedWorldPosition; break;
	case WPT_CameraRelativeNoOffsets: InputType = UE::HLSLTree::EExternalInput::TranslatedWorldPosition_NoOffsets; break;
	default: checkNoEntry(); break;
	}

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionExternalInput>(InputType);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureCoordinate::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	// TODO - tiling, mirroring
	OutExpression = Generator.NewTexCoord(CoordinateIndex);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureObject::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
	OutTexture = Generator.AcquireTextureDeclaration(TextureDesc);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureObjectParameter::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
	OutTexture = Generator.AcquireTextureParameterDeclaration(ParameterName, TextureDesc);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureSample::GenerateHLSLExpressionBase(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	if (!TextureDeclaration)
	{
		return Generator.Error(TEXT("Missing input texture"));
	}

	FExpression* TexCoordExpression = Coordinates.GetTracedInput().Expression ? Coordinates.AcquireHLSLExpression(Generator, Scope) : Generator.NewTexCoord(ConstCoordinate);
	FExpressionTextureSample* ExpressionTextureSample = Generator.GetTree().NewExpression<FExpressionTextureSample>(TextureDeclaration, TexCoordExpression);
	ExpressionTextureSample->SamplerSource = SamplerSource;
	ExpressionTextureSample->MipValueMode = MipValueMode;
	ExpressionTextureSample->TexCoordDerivatives = Generator.GetTree().GetAnalyticDerivatives(TexCoordExpression);

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
		TextureDeclaration = Generator.AcquireTextureDeclaration(TextureDesc);
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
		TextureDeclaration = Generator.AcquireTextureParameterDeclaration(ParameterName, TextureDesc);
	}

	return GenerateHLSLExpressionBase(Generator, Scope, TextureDeclaration, OutExpression);
}

EMaterialGenerateHLSLStatus UMaterialExpressionBinaryOp::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.GetTracedInput().Expression ? A.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstA);
	UE::HLSLTree::FExpression* Rhs = B.GetTracedInput().Expression ? B.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstB);
	if (!Lhs || !Rhs)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(GetBinaryOp(), Lhs, Rhs);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionAdd::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.GetTracedInput().Expression ? A.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstA);
	UE::HLSLTree::FExpression* Rhs = B.GetTracedInput().Expression ? B.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstB);
	if (!Lhs || !Rhs)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(UE::HLSLTree::EBinaryOp::Add, Lhs, Rhs);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionMultiply::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.GetTracedInput().Expression ? A.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstA);
	UE::HLSLTree::FExpression* Rhs = B.GetTracedInput().Expression ? B.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstB);
	if (!Lhs || !Rhs)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(UE::HLSLTree::EBinaryOp::Mul, Lhs, Rhs);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionDivide::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.GetTracedInput().Expression ? A.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstA);
	UE::HLSLTree::FExpression* Rhs = B.GetTracedInput().Expression ? B.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(ConstB);
	if (!Lhs || !Rhs)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(UE::HLSLTree::EBinaryOp::Div, Lhs, Rhs);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionAppendVector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpression(Generator, Scope);
	if (!Lhs || !Rhs)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionAppend>(Lhs, Rhs);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionGetMaterialAttributes::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* AttributesExpression = MaterialAttributes.AcquireHLSLExpression(Generator, Scope);
	if (!AttributesExpression)
	{
		return EMaterialGenerateHLSLStatus::Error;
	}
	if (OutputIndex == 0)
	{
		OutExpression = AttributesExpression;
		return EMaterialGenerateHLSLStatus::Success;
	}
	const int32 AttributeIndex = OutputIndex - 1;
	if (!AttributeGetTypes.IsValidIndex(AttributeIndex))
	{
		return Generator.Error(TEXT("Invalid attribute"));
	}

	const FGuid& AttributeID = AttributeGetTypes[AttributeIndex];
	const FString& AttributeName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionGetStructField>(Generator.GetMaterialAttributesType(), *AttributeName, AttributesExpression);

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
		AttributesExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionConstant>(Generator.GetMaterialAttributesDefaultValue());
	}

	for (int32 PinIndex = 0; PinIndex < AttributeSetTypes.Num(); ++PinIndex)
	{
		const FExpressionInput& AttributeInput = Inputs[PinIndex + 1];
		if (AttributeInput.GetTracedInput().Expression)
		{
			const FGuid& AttributeID = AttributeSetTypes[PinIndex];
			// Only compile code to set attributes of the current shader frequency
			const EShaderFrequency AttributeFrequency = FMaterialAttributeDefinitionMap::GetShaderFrequency(AttributeID);
			const FString& AttributeName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
			//if (AttributeFrequency == Compiler->GetCurrentShaderFrequency())
			{
				UE::HLSLTree::FExpression* ValueExpression = AttributeInput.AcquireHLSLExpression(Generator, Scope);
				if (ValueExpression)
				{
					AttributesExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSetStructField>(Generator.GetMaterialAttributesType(), *AttributeName, AttributesExpression, ValueExpression);
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

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionReflectionVector>();
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionFunctionInput::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	return Generator.Error(TEXT("Invalid"));
}

EMaterialGenerateHLSLStatus UMaterialExpressionMaterialFunctionCall::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GenerateFunctionCall(Scope, MaterialFunction, FunctionInputs, OutputIndex);
	if (OutExpression)
	{
		return EMaterialGenerateHLSLStatus::Success;
	}
	return EMaterialGenerateHLSLStatus::Error;
}

EMaterialGenerateHLSLStatus UMaterialExpressionExecBegin::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	Exec.GenerateHLSLStatements(Generator, Scope);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionExecEnd::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	Generator.GenerateResult(Scope);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionSetLocal::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FExpression* ValueExpression = Value.AcquireHLSLExpression(Generator, Scope);
	if (!ValueExpression)
	{
		return Generator.Error(TEXT("Missing value connection"));
	}

	Generator.GetTree().AssignLocal(Scope, LocalName, ValueExpression);
	Exec.GenerateHLSLStatements(Generator, Scope);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionIfThenElse::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FExpression* ConditionExpression = Condition.AcquireHLSLExpression(Generator, Scope);
	if (!ConditionExpression)
	{
		return Generator.Error(TEXT("Missing condition connection"));
	}

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(Scope);
	IfStatement->ConditionExpression = ConditionExpression;
	IfStatement->NextScope = Generator.NewJoinedScope(Scope);
	IfStatement->ThenScope = Then.NewOwnedScopeWithStatements(Generator, *IfStatement);
	IfStatement->ElseScope = Else.NewOwnedScopeWithStatements(Generator, *IfStatement);

	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionWhileLoop::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	if (!Condition.IsConnected())
	{
		return Generator.Error(TEXT("Missing condition connection"));
	}

	if (!LoopBody.GetExpression())
	{
		return Generator.Error(TEXT("Missing LoopBody connection"));
	}

	UE::HLSLTree::FStatementLoop* LoopStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementLoop>(Scope);
	LoopStatement->LoopScope = Generator.NewOwnedScope(*LoopStatement);

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(*LoopStatement->LoopScope);
	IfStatement->ThenScope = Generator.NewOwnedScope(*IfStatement);
	IfStatement->ElseScope = Generator.NewOwnedScope(*IfStatement);
	LoopStatement->NextScope = Generator.NewScope(Scope, EMaterialNewScopeFlag::NoPreviousScope);
	LoopStatement->LoopScope->AddPreviousScope(*IfStatement->ThenScope);
	LoopStatement->NextScope->AddPreviousScope(*IfStatement->ElseScope);

	Generator.GetTree().NewStatement<UE::HLSLTree::FStatementBreak>(*IfStatement->ElseScope);

	IfStatement->ConditionExpression = Condition.AcquireHLSLExpression(Generator, *LoopStatement->LoopScope);
	LoopBody.GenerateHLSLStatements(Generator, *IfStatement->ThenScope);
	Completed.GenerateHLSLStatements(Generator, *LoopStatement->NextScope);

	return EMaterialGenerateHLSLStatus::Success;
}

struct FGlobalExpressionDataForLoop
{
	int32 NumLoops = 0;
};
DECLARE_MATERIAL_HLSLGENERATOR_DATA(FGlobalExpressionDataForLoop);

struct FExpressionDataForLoop
{
	UE::HLSLTree::FScope* LoopScope = nullptr;
	FName LocalName;
};
DECLARE_MATERIAL_HLSLGENERATOR_DATA(FExpressionDataForLoop);

EMaterialGenerateHLSLStatus UMaterialExpressionForLoop::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	FExpressionDataForLoop* ExpressionData = Generator.FindExpressionData<FExpressionDataForLoop>(this);
	if (!ExpressionData || !Scope.HasParentScope(*ExpressionData->LoopScope))
	{
		return Generator.Error(TEXT("For loop index accessed outside loop scope"));
	}

	OutExpression = Generator.GetTree().AcquireLocal(Scope, ExpressionData->LocalName);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionForLoop::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	if (!LoopBody.GetExpression())
	{
		return Generator.Error(TEXT("Missing LoopBody connection"));
	}

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

	FGlobalExpressionDataForLoop* GlobalData = Generator.AcquireGlobalData<FGlobalExpressionDataForLoop>();
	FExpressionDataForLoop* ExpressionData = Generator.NewExpressionData<FExpressionDataForLoop>(this);
	ExpressionData->LocalName = *FString::Printf(TEXT("ForLoopControl%d"), GlobalData->NumLoops++);

	UE::HLSLTree::FExpression* StepExpression = IndexStep.GetTracedInput().Expression ? IndexStep.AcquireHLSLExpression(Generator, Scope) : Generator.NewConstant(1);

	Generator.GetTree().AssignLocal(Scope, ExpressionData->LocalName, StartExpression);

	UE::HLSLTree::FStatementLoop* LoopStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementLoop>(Scope);
	LoopStatement->LoopScope = Generator.NewOwnedScope(*LoopStatement);
	ExpressionData->LoopScope = LoopStatement->LoopScope;

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(*LoopStatement->LoopScope);
	IfStatement->ThenScope = Generator.NewOwnedScope(*IfStatement);
	IfStatement->ElseScope = Generator.NewOwnedScope(*IfStatement);
	LoopStatement->NextScope = Generator.NewScope(Scope, EMaterialNewScopeFlag::NoPreviousScope);
	LoopStatement->LoopScope->AddPreviousScope(*IfStatement->ThenScope);
	LoopStatement->NextScope->AddPreviousScope(*IfStatement->ElseScope);

	Generator.GetTree().NewStatement<UE::HLSLTree::FStatementBreak>(*IfStatement->ElseScope);

	UE::HLSLTree::FExpression* LocalExpression = Generator.GetTree().AcquireLocal(*LoopStatement->LoopScope, ExpressionData->LocalName);

	IfStatement->ConditionExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(UE::HLSLTree::EBinaryOp::Less, LocalExpression, EndExpression);
	LoopBody.GenerateHLSLStatements(Generator, *IfStatement->ThenScope);

	UE::HLSLTree::FExpression* NewLocalExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(UE::HLSLTree::EBinaryOp::Add, Generator.GetTree().AcquireLocal(*IfStatement->ThenScope, ExpressionData->LocalName), StepExpression);
	Generator.GetTree().AssignLocal(*IfStatement->ThenScope, ExpressionData->LocalName, NewLocalExpression);

	Completed.GenerateHLSLStatements(Generator, *LoopStatement->NextScope);

	return EMaterialGenerateHLSLStatus::Success;
}

#endif // WITH_EDITOR
