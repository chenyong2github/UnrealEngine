// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeGraph.h"
#include "OptimusShaderText.h"

#include "OptimusComputeGraph.generated.h"

class UOptimusDeformer;
class UOptimusNode;
class UOptimusNode_ConstantValue;

DECLARE_DELEGATE_ThreeParams(FOptimusKernelCompilationComplete, UComputeGraph* InComputeGraph, int32 InKernelIndex, const TArray<FString>& InCompileErrors);

UCLASS()
class UOptimusComputeGraph :
	public UComputeGraph
{
	GENERATED_BODY()

public:
	// UObject overrides
	void Serialize(FArchive& Ar) override;
	void PostLoad() override;

	// UComputeGraph overrides
	void OnKernelCompilationComplete(int32 InKernelIndex, const TArray<FString>& InCompileErrors) override;

	FOptimusCompilerDiagnostic ProcessCompilationMessage(
			UOptimusDeformer* InOwner,
			const UOptimusNode* InKernelNode,
			const FString& InMessage
			);
	
	FOptimusKernelCompilationComplete OnKernelCompilationCompleteDelegate;

	// Lookup into Graphs array from the UComputeGraph kernel index. 
	UPROPERTY()
	TArray<TWeakObjectPtr<const UOptimusNode>> KernelToNode;

protected:
	friend class UOptimusDeformer;
};
