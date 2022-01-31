// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode_ComputeKernelBase.h"

#include "OptimusNode_ComputeKernelFunction.generated.h"

UCLASS()
class UOptimusNode_ComputeKernelFunctionGeneratorClass :
	public UClass
{
	GENERATED_BODY()
public:
	static UClass *CreateNodeClass(
		UObject* InPackage,
		FName InCategory,
		const FString& InKernelName,
		int32 InThreadCount,
		FOptimusDataDomain InExecutionDomain,
		const TArray<FOptimus_ShaderValuedBinding>& InParameters,
		const TArray<FOptimusParameterBinding>& InInputBindings,
		const TArray<FOptimusParameterBinding>& InOutputBindings,
		const FString& InShaderSource
		);

	// UClass overrides
	void InitPropertiesFromCustomList(uint8* InObjectPtr, const uint8* InCDOPtr) override;
	void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	
	UPROPERTY()
	FName Category;

	UPROPERTY()
	FString KernelName;

	UPROPERTY()
	int32 ThreadCount;

	UPROPERTY()
	FOptimusDataDomain ExecutionDomain;

	UPROPERTY()
	TArray<FOptimus_ShaderValuedBinding> Parameters;
	
	UPROPERTY()
	TArray<FOptimusParameterBinding> InputBindings;

	UPROPERTY()
	TArray<FOptimusParameterBinding> OutputBindings;

	UPROPERTY()
	FString ShaderSource;
};


/**
 * 
 */
UCLASS(Hidden)
class OPTIMUSCORE_API UOptimusNode_ComputeKernelFunction :
	public UOptimusNode_ComputeKernelBase
{
	GENERATED_BODY()

public:
	UOptimusNode_ComputeKernelFunction();

	
	// UOptimusNode overrides
	FText GetDisplayName() const override;
	FName GetNodeCategory() const override; 

	// UOptimusNode_ComputeKernelBase overrides
	FString GetKernelName() const override;

	/** Implement this to return the complete HLSL code for this kernel */
	FString GetKernelSourceText() const override;

	void ConstructNode() override;

	
	// IOptiusComputeKernelProvider overrides
	void SetCompilationDiagnostics(
		const TArray<FOptimusCompilerDiagnostic>& InDiagnostics
		) override;
	
	UPROPERTY(VisibleAnywhere, Category=KernelConfiguration)
	int32 ThreadCount;

	UPROPERTY(VisibleAnywhere, Category=KernelConfiguration)
	FOptimusDataDomain ExecutionDomain;

private:
	UOptimusNode_ComputeKernelFunctionGeneratorClass *GetGeneratorClass() const;
};
