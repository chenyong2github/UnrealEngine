// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataDomain.h"
#include "OptimusNode_ComputeKernelBase.h"
#include "OptimusShaderText.h"
#include "IOptimusShaderTextProvider.h"

#include "OptimusNode_CustomComputeKernel.generated.h"


enum class EOptimusNodePinDirection : uint8;


UCLASS()
class UOptimusNode_CustomComputeKernel :
	public UOptimusNode_ComputeKernelBase,
	public IOptimusShaderTextProvider
{
	GENERATED_BODY()

public:
	UOptimusNode_CustomComputeKernel();

	// UOptimusNode overrides
	FName GetNodeCategory() const override 
	{
		return CategoryName::Deformers;
	}

	// UOptimusNode_ComputeKernelBase overrides
	FString GetKernelName() const override;

	/** Implement this to return the complete HLSL code for this kernel */
	FString GetKernelSourceText() const override;

	// IOptiusComputeKernelProvider overrides
	void SetCompilationDiagnostics(
		const TArray<FOptimusCompilerDiagnostic>& InDiagnostics
		) override;

	// IOptimusShaderTextProvider overrides
	virtual FString GetNameForShaderTextEditor() const override;
	
	virtual FString GetDeclarations() const override;
	
	virtual FString GetShaderText() const override;

	virtual void SetShaderText(const FString& NewText) override;

	virtual const TArray<FOptimusCompilerDiagnostic>& GetCompilationDiagnostics() const override;

	// FIXME: Use drop-down with a preset list + allow custom entry.
	UPROPERTY(EditAnywhere, Category=Settings)
	FName Category = CategoryName::Deformers;
	
	UPROPERTY(EditAnywhere, Category=KernelConfiguration)
	FString KernelName = "MyKernel";

	UPROPERTY(EditAnywhere, Category = KernelConfiguration, meta=(Min=1))
	int32 ThreadCount = 64;

	UPROPERTY(EditAnywhere, Category = KernelConfiguration)
	FOptimusDataDomain ExecutionDomain;

	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderBinding> Parameters;
	
	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimusParameterBinding> InputBindings;

	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimusParameterBinding> OutputBindings;

	UPROPERTY(EditAnywhere, Category = ShaderSource)
	FOptimusShaderText ShaderSource;

#if WITH_EDITOR
	// IOptimusShaderTextProvider overrides
	FOnDiagnosticsUpdated OnDiagnosticsUpdatedEvent;
	virtual FOnDiagnosticsUpdated& OnDiagnosticsUpdated() override {return OnDiagnosticsUpdatedEvent; };
	
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void PostLoad() override;
	
protected:
	void ConstructNode() override;;

private:
	void UpdatePinTypes(
		EOptimusNodePinDirection InPinDirection
		);

	void UpdatePinNames(
	    EOptimusNodePinDirection InPinDirection);

	void UpdatePinDataDomains(
		EOptimusNodePinDirection InPinDirection
		);
	
	void UpdatePreamble();

	TArray<UOptimusNodePin *> GetKernelPins(
		EOptimusNodePinDirection InPinDirection
		) const;
};
