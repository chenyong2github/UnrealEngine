#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"

class FMaterial;
class FMaterialCompilationOutput;
struct FSharedShaderCompilerEnvironment;
struct FMaterialCompileTargetParameters;

namespace UE
{
namespace HLSLTree
{
class FTree;
}
}

bool MaterialEmitHLSL(const FMaterialCompileTargetParameters& InCompilerTarget,
	const FMaterial& InOutMaterial,
	const UE::HLSLTree::FTree& InTree,
	FMaterialCompilationOutput& OutCompilationOutput,
	TRefCountPtr<FSharedShaderCompilerEnvironment>& OutMaterialEnvironment);
