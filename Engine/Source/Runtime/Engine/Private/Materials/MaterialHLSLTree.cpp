// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialHLSLTree.h"

#if WITH_EDITOR

#include "MaterialHLSLGenerator.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "HLSLTree/HLSLTreeCommon.h"

FMaterialHLSLTree::FMaterialHLSLTree() : Allocator(0)
{
}

static UE::HLSLTree::FExpression* CompileMaterialInput(FMaterialHLSLGenerator& Generator,
	UE::HLSLTree::FScope& Scope,
	EMaterialProperty InputProperty,
	UMaterial* Material,
	UE::HLSLTree::FExpression* AttributesExpression)
{
	UE::HLSLTree::FExpression* Expression = nullptr;
	if (Material->IsPropertyActive(InputProperty))
	{
		FMaterialInputDescription InputDescription;
		if (Material->GetExpressionInputDescription(InputProperty, InputDescription))
		{
			if (InputDescription.bUseConstant)
			{
				UE::Shader::FValue DefaultValue = FMaterialAttributeDefinitionMap::GetDefaultValue(InputProperty);
				DefaultValue.NumComponents = UE::Shader::GetValueTypeDescription(InputDescription.Type).NumComponents;
				if (InputDescription.ConstantValue != DefaultValue)
				{
					Expression = Generator.NewConstant(Scope, InputDescription.ConstantValue);
				}
			}
			else
			{
				check(InputDescription.Input);
				if (InputProperty >= MP_CustomizedUVs0 && InputProperty <= MP_CustomizedUVs7)
				{
					const int32 TexCoordIndex = (int32)InputProperty - MP_CustomizedUVs0;
					if (TexCoordIndex < Material->NumCustomizedUVs)
					{
						Expression = InputDescription.Input->AcquireHLSLExpressionWithCast(Generator, Scope, InputDescription.Type);
					}
					if (!Expression)
					{
						Expression = Generator.NewTexCoord(Scope, TexCoordIndex);
					}
				}
				else
				{
					Expression = InputDescription.Input->AcquireHLSLExpressionWithCast(Generator, Scope, InputDescription.Type);
				}
			}
		}
	}

	if (Expression)
	{
		UE::HLSLTree::FExpressionSetMaterialAttribute* SetAttributeExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSetMaterialAttribute>(Scope);
		SetAttributeExpression->AttributeID = FMaterialAttributeDefinitionMap::GetID(InputProperty);
		SetAttributeExpression->AttributesExpression = AttributesExpression;
		SetAttributeExpression->ValueExpression = Expression;
		return SetAttributeExpression;
	}

	return AttributesExpression;
}

void FMaterialHLSLTree::InitializeForMaterial(const FMaterialCompileTargetParameters& InCompilerTarget, FMaterial& InOutMaterial)
{
	UMaterialInterface* MaterialInterface = InOutMaterial.GetMaterialInterface();
	UMaterial* BaseMaterial = MaterialInterface->GetMaterial();

	HLSLTree = UE::HLSLTree::FTree::Create(Allocator);
	FMaterialHLSLGenerator Generator(InCompilerTarget, *HLSLTree);
	UE::HLSLTree::FScope& RootScope = HLSLTree->GetRootScope();

	if (BaseMaterial->IsCompiledWithExecutionFlow())
	{
		UMaterialExpression* BaseExpression = BaseMaterial->ExpressionExecBegin;
		Generator.AcquireStatement(RootScope, BaseExpression);
	}
	else
	{
		UE::HLSLTree::FExpression* AttributesExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionDefaultMaterialAttributes>(RootScope);
		for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
		{
			const EMaterialProperty Property = (EMaterialProperty)PropertyIndex;
			AttributesExpression = CompileMaterialInput(Generator, RootScope, Property, BaseMaterial, AttributesExpression);
		}

		UE::HLSLTree::FStatementReturn* ReturnStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementReturn>(RootScope);
		ReturnStatement->Expression = AttributesExpression;
	}

	Generator.AcquireErrors(InOutMaterial.CompileErrors, InOutMaterial.ErrorExpressions);
}

void FMaterialHLSLTree::InitializeForFunction(const FMaterialCompileTargetParameters& InCompilerTarget, UMaterialFunctionInterface* InOutFunction)
{
	TArray<FFunctionExpressionInput> FunctionInputs;
	TArray<FFunctionExpressionOutput> FunctionOutputs;
	InOutFunction->GetInputsAndOutputs(FunctionInputs, FunctionOutputs);

	HLSLTree = UE::HLSLTree::FTree::Create(Allocator);
	UE::HLSLTree::FScope& RootScope = HLSLTree->GetRootScope();

	NumFunctionInputs = FunctionInputs.Num();
	NumFunctionOutputs = FunctionOutputs.Num();
	FunctionOutputExpressions = New<UE::HLSLTree::FExpression*>(Allocator, NumFunctionOutputs);

	FMaterialHLSLGenerator Generator(InCompilerTarget, *HLSLTree);
	for (int32 InputIndex = 0; InputIndex < FunctionInputs.Num(); ++InputIndex)
	{
		const FFunctionExpressionInput& Input = FunctionInputs[InputIndex];
		Generator.NewFunctionInput(RootScope, InputIndex, Input.ExpressionInput);
	}

	for (int32 OutputIndex = 0; OutputIndex < FunctionOutputs.Num(); ++OutputIndex)
	{
		const FFunctionExpressionOutput& Output = FunctionOutputs[OutputIndex];
		UMaterialExpressionFunctionOutput* ExpressionOutput = Output.ExpressionOutput;
		FunctionOutputExpressions[OutputIndex] = ExpressionOutput->A.AcquireHLSLExpression(Generator, RootScope);
	}
}

UE::HLSLTree::FFunctionCall* FMaterialHLSLTree::GenerateFunctionCall(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, TArrayView<UE::HLSLTree::FExpression*> Inputs) const
{
	check(Inputs.Num() == NumFunctionInputs);

	return HLSLTree->NewFunctionCall(Scope,
		HLSLTree->GetRootScope(),
		Inputs.GetData(),
		FunctionOutputExpressions,
		NumFunctionInputs,
		NumFunctionOutputs);
}

#endif // WITH_EDITOR
