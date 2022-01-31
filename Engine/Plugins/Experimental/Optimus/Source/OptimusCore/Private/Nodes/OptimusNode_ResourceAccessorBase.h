// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_ResourceAccessorBase.generated.h"


class UOptimusResourceDescription;


UCLASS(Abstract)
class UOptimusNode_ResourceAccessorBase
	: public UOptimusNode
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

private:
	UPROPERTY()
	TWeakObjectPtr<UOptimusResourceDescription> ResourceDesc;
};
