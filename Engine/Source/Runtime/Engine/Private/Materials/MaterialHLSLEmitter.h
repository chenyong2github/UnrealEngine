// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"

class FMaterial;
class FMaterialCompilationOutput;
struct FSharedShaderCompilerEnvironment;
struct FMaterialCompileTargetParameters;
struct FStaticParameterSet;

namespace UE
{
namespace HLSLTree
{
class FTree;
}
}

bool MaterialEmitHLSL(const FMaterialCompileTargetParameters& InCompilerTarget,
	const FMaterial& InOutMaterial,
	const FStaticParameterSet& InStaticParameters,
	const UE::HLSLTree::FTree& InTree,
	FMaterialCompilationOutput& OutCompilationOutput,
	TRefCountPtr<FSharedShaderCompilerEnvironment>& OutMaterialEnvironment);

#endif // WITH_EDITOR
