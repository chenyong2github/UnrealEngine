// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_BlendMeshAttribute.generated.h"

class UOptimusMeshAttribute;

UCLASS()
class UOptimusNode_BlendMeshAttribute
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{
		return CategoryName::Attributes;
	}

	UPROPERTY(meta = (Input))
	UOptimusMeshAttribute* AttributeA = nullptr;

	UPROPERTY(meta = (Input))
	UOptimusMeshAttribute* AttributeB = nullptr;

	UPROPERTY(EditAnywhere, Category="Blend Settings", meta = (Input))
	float Alpha = 0.5f;

	UPROPERTY(meta = (Output))
	UOptimusMeshAttribute* Result = nullptr;
};
