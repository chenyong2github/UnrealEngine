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
	UE::HLSLTree::FTree& GetTree() const { return *HLSLTree; }
private:
	FMemStackBase Allocator;
	UE::HLSLTree::FTree* HLSLTree = nullptr;
};

#endif // WITH_EDITOR
