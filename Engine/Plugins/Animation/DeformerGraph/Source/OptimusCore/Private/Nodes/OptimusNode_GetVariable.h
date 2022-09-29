// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusValueProvider.h"
#include "OptimusNode.h"

#include "OptimusNode_GetVariable.generated.h"


class UOptimusVariableDescription;


UCLASS(Hidden)
class UOptimusNode_GetVariable : 
	public UOptimusNode,
	public IOptimusValueProvider
{
	GENERATED_BODY()

public:
	void SetVariableDescription(UOptimusVariableDescription* InVariableDesc);

	UOptimusVariableDescription* GetVariableDescription() const;
	
	// UOptimusNode overrides
	FName GetNodeCategory() const override 
	{
		return CategoryName::Variables;
	}

	TOptional<FText> ValidateForCompile() const override;
	
	// IOptimusValueProvider overrides 
	FString GetValueName() const override;
	FOptimusDataTypeRef GetValueType() const override;
	FShaderValueType::FValue GetShaderValue() const override;
	
protected:
	void ConstructNode() override;

	// UObject overrides
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	
private:
	UPROPERTY()
	TWeakObjectPtr<UOptimusVariableDescription> VariableDesc;
};
