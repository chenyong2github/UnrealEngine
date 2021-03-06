#include "MaterialHLSLTree.h"
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

void FMaterialHLSLTree::InitializeForMaterial(const FMaterialCompileTargetParameters& InCompilerTarget, FMaterial& InOutMaterial)
{
	const UMaterialInterface* MaterialInterface = InOutMaterial.GetMaterialInterface();
	const UMaterial* BaseMaterial = MaterialInterface->GetMaterial();
	UMaterialExpression* BaseExpression = BaseMaterial->ExpressionExecBegin;

	HLSLTree = UE::HLSLTree::FTree::Create(Allocator);

	FMaterialHLSLGenerator Generator(InCompilerTarget, *HLSLTree);
	Generator.AcquireStatement(HLSLTree->GetRootScope(), BaseExpression);
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
	FunctionOutputTypes = New<UE::HLSLTree::EExpressionType>(Allocator, NumFunctionOutputs);

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

		UE::HLSLTree::FExpression* Expression = ExpressionOutput->A.AcquireHLSLExpression(Generator, RootScope);
		UE::HLSLTree::EExpressionType OutputType = UE::HLSLTree::EExpressionType::Void;
		if (Expression)
		{
			UE::HLSLTree::FStatementSetFunctionOutput* OutputStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementSetFunctionOutput>(RootScope);
			OutputStatement->Name = ExpressionOutput->OutputName;
			OutputStatement->OutputIndex = OutputIndex;
			OutputStatement->Expression = Expression;
			OutputType = Expression->Type;
		}
		FunctionOutputTypes[OutputIndex] = OutputType;
	}
}

UE::HLSLTree::FFunctionCall* FMaterialHLSLTree::GenerateFunctionCall(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, TArrayView<UE::HLSLTree::FExpression*> Inputs) const
{
	check(Inputs.Num() == NumFunctionInputs);

	return HLSLTree->NewFunctionCall(Scope,
		HLSLTree->GetRootScope(),
		Inputs.GetData(),
		FunctionOutputTypes,
		NumFunctionInputs,
		NumFunctionOutputs);
}
