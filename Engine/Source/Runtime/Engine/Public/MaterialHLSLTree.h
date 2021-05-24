// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include "Misc/MemStack.h"

class FMaterial;
class UMaterialFunctionInterface;
struct FMaterialCompileTargetParameters;
class FMaterialHLSLGenerator;

namespace UE
{
namespace HLSLTree
{
class FTree;
class FScope;
class FExpression;
class FFunctionCall;
}
}

class FMaterialHLSLTree
{
public:
	FMaterialHLSLTree();
	~FMaterialHLSLTree();

	bool InitializeForMaterial(const FMaterialCompileTargetParameters& InCompilerTarget, FMaterial& InOutMaterial);

	bool InitializeForFunction(const FMaterialCompileTargetParameters& InCompilerTarget, UMaterialFunctionInterface* InOutFunction);

	UE::HLSLTree::FTree& GetTree() const { return *HLSLTree; }
	int32 GetNumFunctionInputs() const { return NumFunctionInputs; }
	int32 GetNumFunctionOutputs() const { return NumFunctionOutputs; }

	UE::HLSLTree::FFunctionCall* GenerateFunctionCall(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, TArrayView<UE::HLSLTree::FExpression*> Inputs) const;
private:
	FMemStackBase Allocator;
	UE::HLSLTree::FTree* HLSLTree = nullptr;
	UE::HLSLTree::FExpression** FunctionOutputExpressions = nullptr;
	int32 NumFunctionInputs = 0;
	int32 NumFunctionOutputs = 0;
};

#endif // WITH_EDITOR
