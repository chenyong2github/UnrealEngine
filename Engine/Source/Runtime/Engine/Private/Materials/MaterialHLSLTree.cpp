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

FMaterialHLSLTree::FMaterialHLSLTree()
{
}

FMaterialHLSLTree::~FMaterialHLSLTree()
{
	UE::HLSLTree::FTree::Destroy(HLSLTree);
}


bool FMaterialHLSLTree::InitializeForMaterial(const FMaterialCompileTargetParameters& InCompilerTarget, FMaterial& InOutMaterial)
{
	UMaterialInterface* MaterialInterface = InOutMaterial.GetMaterialInterface();
	UMaterial* BaseMaterial = MaterialInterface->GetMaterial();

	HLSLTree = UE::HLSLTree::FTree::Create(Allocator);
	FMaterialHLSLGenerator Generator(BaseMaterial, InCompilerTarget, *HLSLTree);
	UE::HLSLTree::FScope& RootScope = HLSLTree->GetRootScope();

	bool bResult = false;
	if (BaseMaterial->IsCompiledWithExecutionFlow())
	{
		UMaterialExpression* BaseExpression = BaseMaterial->ExpressionExecBegin;
		bResult = Generator.GenerateStatements(RootScope, BaseExpression);
	}
	else
	{
		bResult = Generator.GenerateResult(RootScope);
	}

	if (!Generator.Finalize())
	{
		bResult = false;
	}

	Generator.AcquireErrors(InOutMaterial.CompileErrors, InOutMaterial.ErrorExpressions);

	return bResult;
}

#endif // WITH_EDITOR
