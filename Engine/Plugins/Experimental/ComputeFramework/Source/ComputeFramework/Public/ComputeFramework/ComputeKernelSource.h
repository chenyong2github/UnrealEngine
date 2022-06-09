// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeKernelSource.generated.h"

/** 
 * Class representing the source for a UComputeKernel 
 * We derive from this for each authoring mechanism. (HLSL text, VPL graph, ML Meta Lang, etc.)
 */
UCLASS(Abstract, EditInlineNew)
class COMPUTEFRAMEWORK_API UComputeKernelSource : public UObject
{
	GENERATED_BODY()

public:
	/** Base permutations exposed by the kernel. These will be extended by further permutations declared in any linked data providers. */
	UPROPERTY(VisibleAnywhere, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelPermutationSet PermutationSet;

	/** Base environment defines for kernel compilation. These will be extended by further defines declared in any linked data providers. */
	UPROPERTY(VisibleAnywhere, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelDefinitionSet DefinitionsSet;

	/* Named external inputs for the kernel. These must be fulfilled by linked data providers. */
	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = "External")
	TArray<FShaderFunctionDefinition> ExternalInputs;

	/* Named external outputs for the kernel. These must be fulfilled by linked data providers. */
	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = "External")
	TArray<FShaderFunctionDefinition> ExternalOutputs;

	/** Get kernel entry point name. */
	virtual FString GetEntryPoint() const PURE_VIRTUAL(UComputeKernelSource::GetEntryPoint, return {};);
	/** Get kernel group size. Note that this must be known and fixed for all permutations of the kernel. */
	virtual FIntVector GetGroupSize() const PURE_VIRTUAL(UComputeKernelSource::GetGroupSize, return {};);
	/** Get kernel source code. */
	virtual FString GetSource() const PURE_VIRTUAL(UComputeKernelSource::GetSource, return {};);
};
