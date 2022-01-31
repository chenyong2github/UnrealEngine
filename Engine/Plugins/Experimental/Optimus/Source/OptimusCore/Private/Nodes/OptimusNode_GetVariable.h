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

	// IOptimusValueProvider overrides 
	TArray<uint8> GetShaderValue() const override;
	
protected:
	void ConstructNode() override;

private:
	UPROPERTY()
	TWeakObjectPtr<UOptimusVariableDescription> VariableDesc;
};
