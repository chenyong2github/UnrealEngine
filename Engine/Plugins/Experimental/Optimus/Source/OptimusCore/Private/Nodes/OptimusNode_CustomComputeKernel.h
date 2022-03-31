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
	FIntVector GetGroupSize() const override;
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
	
	/** Name of kernel. This is also used as the entry point function name in generated code. */
	UPROPERTY(EditAnywhere, Category=Settings)
	FString KernelName = "MyKernel";

	/** 
	 * Number of threads in a thread group. 
	 * Thread groups have 3 dimensions. 
	 * It's better to have the total threads (X*Y*Z) be a value divisible by 32. 
	 */
	UPROPERTY(EditAnywhere, Category=Settings, meta=(Min=1))
	FIntVector GroupSize = FIntVector(64, 1, 1);

	/** Parameter bindings. Parameters are uniform values. */
	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderBinding> Parameters;
	
	/** Input bindings. Each one is a function that should be connected to an implementation in a data interface. */
	UPROPERTY(EditAnywhere, Category = Bindings)
	TArray<FOptimusParameterBinding> InputBindings;

	/** Output bindings. Each one is a function that should be connected to an implementation in a data interface. */
	UPROPERTY(EditAnywhere, Category = Bindings)
	TArray<FOptimusParameterBinding> OutputBindings;

	/** 
	 * The kernel source code. 
	 * If the code contains more than just the kernel entry function, then place the kernel entry function inside a KERNEL {} block.
	 */
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
