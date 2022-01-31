// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusShaderText.h"

#include "ComputeFramework/ComputeGraph.h"

#include "OptimusComputeGraph.generated.h"


class UOptimusDeformer;
class UOptimusNode;


DECLARE_DELEGATE_ThreeParams(FOptimusKernelCompilationComplete, UComputeGraph* InComputeGraph, int32 InKernelIndex, const TArray<FString>& InCompileErrors);


// FIXME: Rename to FOptimusKernelParameterBinding
USTRUCT()
struct FOptimus_ShaderParameterBinding
{
	GENERATED_BODY()
	
	UPROPERTY()
	TObjectPtr<const UOptimusNode> ValueNode = nullptr;
	
	UPROPERTY()
	int32 KernelIndex = INDEX_NONE;
	
	UPROPERTY()
	int32 ParameterIndex = INDEX_NONE;
};




UCLASS()
class UOptimusComputeGraph :
	public UComputeGraph
{
	GENERATED_BODY()

public:
	// UComputeGraph overrides
	void GetKernelBindings(int32 InKernelIndex, TMap<int32, TArray<uint8>>& OutBindings) const override;
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

	// List of parameter bindings and which value nodes they map to.
	UPROPERTY()
	TArray<FOptimus_ShaderParameterBinding> KernelParameterBindings;
	
protected:
	friend class UOptimusDeformer;
};
