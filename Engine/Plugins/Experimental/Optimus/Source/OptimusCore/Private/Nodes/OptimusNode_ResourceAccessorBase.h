// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusDataInterfaceProvider.h"
#include "OptimusNode.h"

#include "OptimusNode_ResourceAccessorBase.generated.h"


class UOptimusComputeDataInterface;
class UOptimusResourceDescription;


UCLASS(Abstract)
class UOptimusNode_ResourceAccessorBase : 
	public UOptimusNode,
	public IOptimusDataInterfaceProvider
{
	GENERATED_BODY()

public:
	void SetResourceDescription(UOptimusResourceDescription* InResourceDesc);

	UOptimusResourceDescription* GetResourceDescription() const;
	
	// UOptimusNode overrides
	FName GetNodeCategory() const override 
	{
		return CategoryName::Resources;
	}

	// IOptimusDataInterfaceProvider implementations
	UOptimusComputeDataInterface* GetDataInterface(UObject *InOuter) const override;
	bool IsRetainedDataInterface() const override { return true; }
	int32 GetDataFunctionIndexFromPin(const UOptimusNodePin* InPin) const override { return INDEX_NONE; }
	
private:
	UPROPERTY()
	TWeakObjectPtr<UOptimusResourceDescription> ResourceDesc;
};
