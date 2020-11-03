// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include "ComputeKernelSource.generated.h"

class FComputeKernelResource;

UCLASS(Abstract, EditInlineNew)
class UComputeKernelSource : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelPermutationSet PermutationSet;

	UPROPERTY(VisibleAnywhere, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelDefinitionsSet DefinitionsSet;

	/* Named input types to the kernel. */
	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = "Parameters")
	TArray<FShaderParamTypeDefinition> InputParams;

	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = "Parameters")
	TArray<FShaderParamTypeDefinition> InputSRVs;

	/* Named output types to the kernel. */
	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = "Parameters")
	TArray<FShaderParamTypeDefinition> Outputs;

	virtual FString GetEntryPoint() const PURE_VIRTUAL(UComputeKernelSource::GetEntryPoint, return {};);
	virtual FString GetSource() const PURE_VIRTUAL(UComputeKernelSource::GetSource, return {};);
	virtual uint64 GetSourceHashCode() const PURE_VIRTUAL(UComputeKernelSource::GetSourceHashCode, return 0;);
};
