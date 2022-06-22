// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataDomain.h"
#include "OptimusNode_ComputeKernelBase.h"
#include "OptimusShaderText.h"
#include "IOptimusShaderTextProvider.h"
#include "OptimusBindingTypes.h"
#include "IOptimusParameterBindingProvider.h"
#include "IOptimusNodeAdderPinProvider.h"

#include "OptimusNode_CustomComputeKernel.generated.h"


enum class EOptimusNodePinDirection : uint8;


UCLASS()
class UOptimusNode_CustomComputeKernel :
	public UOptimusNode_ComputeKernelBase,
	public IOptimusShaderTextProvider,
	public IOptimusParameterBindingProvider,
	public IOptimusNodeAdderPinProvider
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
	FString GetKernelName() const override { return KernelName.ToString(); }
	FIntVector GetGroupSize() const override { return GroupSize; }
	FString GetKernelSourceText() const override;
	TArray<TObjectPtr<UComputeSource>> GetAdditionalSources() const override { return AdditionalSources; }

#if WITH_EDITOR
	// IOptimusShaderTextProvider overrides
	FString GetNameForShaderTextEditor() const override;
	FString GetDeclarations() const override;
	FString GetShaderText() const override;
	void SetShaderText(const FString& NewText) override;
	// IOptimusShaderTextProvider overrides
#endif
	
	// IOptimusParameterBindingProvider
	virtual FString GetBindingDeclaration(FName BindingName) const override;

	// IOptimusNodeAdderPinProvider
	virtual bool CanAddPinFromPin(const UOptimusNodePin* InSourcePin, EOptimusNodePinDirection InNewPinDirection, FString* OutReason = nullptr) const override;

	virtual UOptimusNodePin* TryAddPinFromPin(UOptimusNodePin* InSourcePin, FName InNewPinName) override;
	
	virtual bool RemoveAddedPin(UOptimusNodePin* InAddedPinToRemove) override;

	virtual FName GetSanitizedNewPinName(FName InPinName) override;
	
	// FIXME: Use drop-down with a preset list + allow custom entry.
	UPROPERTY(EditAnywhere, Category=Settings)
	FName Category = CategoryName::Deformers;
	
	/** Name of kernel. This is also used as the entry point function name in generated code. */
	UPROPERTY(EditAnywhere, Category=Settings)
	FName KernelName;

	/** 
	 * Number of threads in a thread group. 
	 * Thread groups have 3 dimensions. 
	 * It's better to have the total threads (X*Y*Z) be a value divisible by 32. 
	 */
	UPROPERTY(EditAnywhere, Category=Settings, meta=(Min=1))
	FIntVector GroupSize = FIntVector(64, 1, 1);

	/** Parameter bindings. Parameters are uniform values. */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FOptimus_ShaderBinding> Parameters_DEPRECATED;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	/** Input bindings. Each one is a function that should be connected to an implementation in a data interface. */
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FOptimusParameterBinding> InputBindings_DEPRECATED;

	/** Output bindings. Each one is a function that should be connected to an implementation in a data interface. */
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FOptimusParameterBinding> OutputBindings_DEPRECATED;	

	/** Input bindings. Each one is a function that should be connected to an implementation in a data interface. */
	UPROPERTY(EditAnywhere, Category = Bindings, DisplayName = "Input Bindings", meta=(AllowParameters))
	FOptimusParameterBindingArray InputBindingArray;

	/** Output bindings. Each one is a function that should be connected to an implementation in a data interface. */
	UPROPERTY(EditAnywhere, Category = Bindings, DisplayName = "Output Bindings")
	FOptimusParameterBindingArray OutputBindingArray;

	/** Additional source includes. */
	UPROPERTY(EditAnywhere, Category = Source)
	TArray<TObjectPtr<UComputeSource>> AdditionalSources;

	/** 
	 * The kernel source code. 
	 * If the code contains more than just the kernel entry function, then place the kernel entry function inside a KERNEL {} block.
	 */
	UPROPERTY(EditAnywhere, Category = Source, meta = (DisplayName = "Kernel Source"))
	FOptimusShaderText ShaderSource;

#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void PostLoad() override;
	
protected:
	void ConstructNode() override;

private:


	static bool IsParameterBinding(FName InBindingPropertyName);
	
	void RefreshBindingPins(FName InBindingPropertyName);
	void ClearBindingPins(FName InBindingPropertyName);
	
	void UpdatePinTypes(
		EOptimusNodePinDirection InPinDirection
		);

	void UpdatePinNames(
	    EOptimusNodePinDirection InPinDirection);

	void UpdatePinDataDomains(
		EOptimusNodePinDirection InPinDirection
		);
	
	void UpdatePreamble();

	static FString GetDeclarationForBinding(const FOptimusParameterBinding& Binding, bool bIsInput);

	TArray<UOptimusNodePin *> GetPinsByDirection(
		EOptimusNodePinDirection InPinDirection
		) const;
	
	TMap<FName, UOptimusNodePin*> GetNamedPinsByDirection(
		EOptimusNodePinDirection InDirection
		) const;
};
