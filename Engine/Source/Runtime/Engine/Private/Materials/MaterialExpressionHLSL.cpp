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
#include "Materials/MaterialExpressionGenericConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionStaticBool.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
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
#include "Materials/MaterialExpressionBinaryOp.h"
#include "Materials/MaterialExpressionAppendVector.h"
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

FMaterialHLSLTree& UMaterialFunctionInterface::AcquireHLSLTree(FMaterialHLSLGenerator& Generator)
{
	if (!CachedHLSLTree)
	{
		CachedHLSLTree = new FMaterialHLSLTree();
		CachedHLSLTree->InitializeForFunction(Generator.GetCompileTarget(), this);
	}
	return *CachedHLSLTree;
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

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSelect>(Scope, ConditionExpression, TrueExpression, FalseExpression);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionGetLocal::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.AcquireLocalValue(Scope, LocalName);
	if (!OutExpression)
	{
		return Generator.Error(TEXT("Local accessed before assigned"));
	}
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionVectorParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(Scope, EMaterialParameterType::Vector, ParameterName, DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionScalarParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(Scope, EMaterialParameterType::Scalar, ParameterName, DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionStaticBoolParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(Scope, EMaterialParameterType::StaticSwitch, ParameterName, (bool)DefaultValue);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionTextureCoordinate::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	// TODO - tiling, mirroring
	OutExpression = Generator.NewTexCoord(Scope, CoordinateIndex);
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

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(Scope, GetBinaryOp(), Lhs, Rhs);
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
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(Scope, UE::HLSLTree::EBinaryOp::Add, Lhs, Rhs);
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
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(Scope, UE::HLSLTree::EBinaryOp::Mul, Lhs, Rhs);
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
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionAppend>(Scope, Lhs, Rhs);
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
	return Generator.Error(TEXT("Invalid"));
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

	Generator.GenerateAssignLocal(Scope, LocalName, ValueExpression);
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
	IfStatement->ThenScope = Then.NewScopeWithStatements(Generator, Scope);
	IfStatement->ElseScope = Else.NewScopeWithStatements(Generator, Scope);

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
	LoopStatement->LoopScope = Generator.NewScope(Scope);

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(*LoopStatement->LoopScope);
	IfStatement->ThenScope = Generator.NewScope(*LoopStatement->LoopScope);
	IfStatement->ElseScope = Generator.NewScope(*LoopStatement->LoopScope);
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

	OutExpression = Generator.AcquireLocalValue(Scope, ExpressionData->LocalName);
	return EMaterialGenerateHLSLStatus::Success;
}

EMaterialGenerateHLSLStatus UMaterialExpressionForLoop::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	if (!LoopBody.GetExpression())
	{
		return Generator.Error(TEXT("Missing LoopBody connection"));
	}

	UE::HLSLTree::FExpression* StartExpression = StartIndex.AcquireHLSLExpressionWithCast(Generator, Scope, UE::Shader::EValueType::Int1);
	if (!StartExpression)
	{
		return Generator.Error(TEXT("Missing StartIndex connection"));
	}

	UE::HLSLTree::FExpression* EndExpression = EndIndex.AcquireHLSLExpressionWithCast(Generator, Scope, UE::Shader::EValueType::Int1);
	if (!EndExpression)
	{
		return Generator.Error(TEXT("Missing EndIndex connection"));
	}

	FGlobalExpressionDataForLoop* GlobalData = Generator.AcquireGlobalData<FGlobalExpressionDataForLoop>();
	FExpressionDataForLoop* ExpressionData = Generator.NewExpressionData<FExpressionDataForLoop>(this);
	ExpressionData->LocalName = *FString::Printf(TEXT("ForLoopControl%d"), GlobalData->NumLoops++);

	UE::HLSLTree::FExpression* StepExpression = IndexStep.GetTracedInput().Expression ? IndexStep.AcquireHLSLExpressionWithCast(Generator, Scope, UE::Shader::EValueType::Int1) : Generator.NewConstant(1);

	Generator.GenerateAssignLocal(Scope, ExpressionData->LocalName, StartExpression);

	UE::HLSLTree::FStatementLoop* LoopStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementLoop>(Scope);
	LoopStatement->LoopScope = Generator.NewScope(Scope);
	ExpressionData->LoopScope = LoopStatement->LoopScope;

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(*LoopStatement->LoopScope);
	IfStatement->ThenScope = Generator.NewScope(*LoopStatement->LoopScope);
	IfStatement->ElseScope = Generator.NewScope(*LoopStatement->LoopScope);
	LoopStatement->NextScope = Generator.NewScope(Scope, EMaterialNewScopeFlag::NoPreviousScope);
	LoopStatement->LoopScope->AddPreviousScope(*IfStatement->ThenScope);
	LoopStatement->NextScope->AddPreviousScope(*IfStatement->ElseScope);

	Generator.GetTree().NewStatement<UE::HLSLTree::FStatementBreak>(*IfStatement->ElseScope);

	UE::HLSLTree::FExpression* LocalExpression = Generator.AcquireLocalValue(*LoopStatement->LoopScope, ExpressionData->LocalName);

	IfStatement->ConditionExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(*LoopStatement->LoopScope, UE::HLSLTree::EBinaryOp::Less, LocalExpression, EndExpression);
	LoopBody.GenerateHLSLStatements(Generator, *IfStatement->ThenScope);

	UE::HLSLTree::FExpression* NewLocalExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionBinaryOp>(*IfStatement->ThenScope, UE::HLSLTree::EBinaryOp::Add, Generator.AcquireLocalValue(*IfStatement->ThenScope, ExpressionData->LocalName), StepExpression);
	Generator.GenerateAssignLocal(*IfStatement->ThenScope, ExpressionData->LocalName, NewLocalExpression);

	Completed.GenerateHLSLStatements(Generator, *LoopStatement->NextScope);

	return EMaterialGenerateHLSLStatus::Success;
}

#endif // WITH_EDITOR
